extends RefCounted

# Applies Elisa's portable quality profile to Godot-owned viewport and
# environment resources. Unsupported renderer features are reported per field;
# invalid profiles are rejected before any live resource is changed.

enum Status { OK, INVALID_PROFILE, INVALID_TARGET }

const _MIN_RENDER_SCALE := 0.25
const _MAX_RENDER_SCALE := 1.0
const _MAX_BLOOM_THRESHOLD := 100.0
const _MAX_RECEIVER_BIAS := 0.01
const _FINITE_LIMIT := 1.0e30

func capabilities() -> Dictionary:
	return capabilities_for_method(String(RenderingServer.get_current_rendering_method()))

func capabilities_for_method(method: String) -> Dictionary:
	var forward_plus := method == "forward_plus"
	var mobile := method == "mobile"
	var rendering_device := forward_plus or mobile
	return {
		"rendering_method": method,
		"tonemap": true,
		"bloom": true,
		"fog": true,
		"shadow_quality": true,
		"render_scale": rendering_device,
		"fsr1": rendering_device,
		"fsr2": forward_plus,
		"fxaa": rendering_device,
		"temporal_aa": forward_plus,
		"ambient_occlusion": not mobile,
		"screen_space_reflections": forward_plus,
		"depth_effects": rendering_device,
		"sun_shadow_receiver_bias": false,
	}

func apply_profile(
		profile: Dictionary,
		viewport: Viewport,
		environment: Environment,
		camera_attributes: CameraAttributesPractical = null
) -> Dictionary:
	if viewport == null or environment == null:
		return _result(Status.INVALID_TARGET)
	if not _profile_valid(profile):
		return _result(Status.INVALID_PROFILE)

	var caps := capabilities()
	var unsupported: Array[String] = []
	var approximations: Array[String] = []
	var upscaler_fallback := false
	var scale: float = float(profile["render_scale"])
	var bloom_threshold: float = float(profile["bloom_threshold"])
	var selected_upscaler: String = String(profile["upscaler"])
	var tonemap: String = String(profile["tonemap"])

	# All descriptors have been checked above. Apply supported state now; a
	# renderer fallback skips only that feature and keeps the other profile
	# settings useful.
	match tonemap:
		"reinhard":
			environment.tonemap_mode = Environment.TONE_MAPPER_REINHARDT
		"aces":
			environment.tonemap_mode = Environment.TONE_MAPPER_ACES
		"uchimura":
			# Godot has no Uchimura curve; FILMIC is the closest built-in curve.
			environment.tonemap_mode = Environment.TONE_MAPPER_FILMIC
			approximations.append("tonemap:filmic")

	environment.glow_enabled = bool(profile["bloom"])
	environment.glow_hdr_threshold = bloom_threshold
	environment.fog_enabled = bool(profile["fog"])

	var shadow_atlas_size := _shadow_atlas_size(String(profile["shadow_quality"]))
	viewport.positional_shadow_atlas_size = shadow_atlas_size
	RenderingServer.directional_shadow_atlas_set_size(shadow_atlas_size, false)

	if caps["render_scale"]:
		viewport.scaling_3d_scale = scale
		viewport.scaling_3d_mode = _upscaler_mode(selected_upscaler, caps)
	else:
		if not is_equal_approx(scale, 1.0):
			unsupported.append("render_scale")
		if selected_upscaler != "none":
			unsupported.append("upscaler")
			upscaler_fallback = true

	if selected_upscaler != "none" and caps["render_scale"]:
		if not caps[selected_upscaler]:
			unsupported.append("upscaler")
			upscaler_fallback = true
			viewport.scaling_3d_mode = Viewport.SCALING_3D_MODE_BILINEAR

	if caps["fxaa"]:
		viewport.screen_space_aa = Viewport.SCREEN_SPACE_AA_FXAA if bool(profile["fxaa"]) else Viewport.SCREEN_SPACE_AA_DISABLED
	elif bool(profile["fxaa"]):
		unsupported.append("fxaa")
	if caps["temporal_aa"]:
		viewport.use_taa = bool(profile["temporal_aa"])
	elif bool(profile["temporal_aa"]):
		unsupported.append("temporal_aa")

	if caps["ambient_occlusion"] or not bool(profile["ambient_occlusion"]):
		environment.ssao_enabled = bool(profile["ambient_occlusion"])
	else:
		unsupported.append("ambient_occlusion")
	if caps["screen_space_reflections"] or not bool(profile["screen_space_reflections"]):
		environment.ssr_enabled = bool(profile["screen_space_reflections"])
	else:
		unsupported.append("screen_space_reflections")
	if bool(profile["depth_effects"]):
		if caps["depth_effects"] and camera_attributes != null:
			camera_attributes.dof_blur_near_enabled = true
			camera_attributes.dof_blur_far_enabled = true
		else:
			unsupported.append("depth_effects")
	elif camera_attributes != null:
		camera_attributes.dof_blur_near_enabled = false
		camera_attributes.dof_blur_far_enabled = false

	if not is_zero_approx(float(profile["sun_shadow_receiver_bias"])):
		# Elisa's normalized reverse-Z receiver offset has no equivalent Godot
		# light-space value; copying it to shadow_bias would change units.
		unsupported.append("sun_shadow_receiver_bias")

	var outcome := "applied"
	if upscaler_fallback:
		outcome = "upscaler_fallback"
	elif not unsupported.is_empty():
		outcome = "feature_fallback"
	elif not approximations.is_empty():
		outcome = "approximation"
	return {
		"error": Status.OK,
		"outcome": outcome,
		"unsupported": unsupported,
		"approximations": approximations,
	}

func _upscaler_mode(upscaler: String, caps: Dictionary) -> int:
	match upscaler:
		"fsr1":
			return Viewport.SCALING_3D_MODE_FSR if caps["fsr1"] else Viewport.SCALING_3D_MODE_BILINEAR
		"fsr2":
			return Viewport.SCALING_3D_MODE_FSR2 if caps["fsr2"] else Viewport.SCALING_3D_MODE_BILINEAR
	return Viewport.SCALING_3D_MODE_BILINEAR

func _shadow_atlas_size(quality: String) -> int:
	match quality:
		"low":
			return 512
		"high":
			return 4096
	return 2048

func _profile_valid(profile: Dictionary) -> bool:
	var required := [
		"level", "tonemap", "upscaler", "render_scale", "bloom", "bloom_threshold",
		"fxaa", "temporal_aa", "ambient_occlusion", "screen_space_reflections",
		"fog", "depth_effects", "shadow_quality", "sun_shadow_receiver_bias",
	]
	for key in required:
		if not profile.has(key):
			return false
	if not ["low", "medium", "high"].has(profile["level"]):
		return false
	if not ["reinhard", "aces", "uchimura"].has(profile["tonemap"]):
		return false
	if not ["none", "fsr1", "fsr2"].has(profile["upscaler"]):
		return false
	if not ["low", "medium", "high"].has(profile["shadow_quality"]):
		return false
	for key in ["bloom", "fxaa", "temporal_aa", "ambient_occlusion", "screen_space_reflections", "fog", "depth_effects"]:
		if typeof(profile[key]) != TYPE_BOOL:
			return false
	for key in ["render_scale", "bloom_threshold", "sun_shadow_receiver_bias"]:
		if typeof(profile[key]) != TYPE_FLOAT and typeof(profile[key]) != TYPE_INT:
			return false
		var value := float(profile[key])
		if not is_finite(value) or absf(value) > _FINITE_LIMIT:
			return false
	return float(profile["render_scale"]) >= _MIN_RENDER_SCALE \
		and float(profile["render_scale"]) <= _MAX_RENDER_SCALE \
		and float(profile["bloom_threshold"]) >= 0.0 \
		and float(profile["bloom_threshold"]) <= _MAX_BLOOM_THRESHOLD \
		and float(profile["sun_shadow_receiver_bias"]) >= -_MAX_RECEIVER_BIAS \
		and float(profile["sun_shadow_receiver_bias"]) <= _MAX_RECEIVER_BIAS \
		and not (bool(profile["temporal_aa"]) and String(profile["upscaler"]) == "fsr2")

func _result(status: Status) -> Dictionary:
	return {"error": status, "outcome": "rejected", "unsupported": [], "approximations": []}
