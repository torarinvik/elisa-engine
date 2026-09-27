extends SceneTree

const QualityService = preload("res://quality_service.gd")

func _initialize() -> void:
	call_deferred("_run_probe")

func _run_probe() -> void:
	var service := QualityService.new()
	var viewport := SubViewport.new()
	var environment := Environment.new()
	var camera_attributes := CameraAttributesPractical.new()
	root.add_child(viewport)
	var actual_method := String(RenderingServer.get_current_rendering_method())
	var actual_caps: Dictionary = service.capabilities()
	if actual_caps["rendering_method"] != actual_method:
		_fail("active renderer capability report does not match Godot")
		return
	var compatibility: Dictionary = service.capabilities_for_method("gl_compatibility")
	var mobile: Dictionary = service.capabilities_for_method("mobile")
	var forward_plus: Dictionary = service.capabilities_for_method("forward_plus")
	if compatibility["temporal_aa"] or compatibility["fxaa"] or compatibility["render_scale"] \
			or compatibility["screen_space_reflections"] or compatibility["depth_effects"] \
			or not compatibility["bloom"] or not compatibility["fog"] \
			or not compatibility["ambient_occlusion"]:
		_fail("Compatibility capability matrix is inconsistent")
		return
	if mobile["temporal_aa"] or mobile["fsr2"] or mobile["ambient_occlusion"] \
			or not mobile["fxaa"] or not mobile["render_scale"] or not mobile["depth_effects"]:
		_fail("Mobile capability matrix is inconsistent")
		return
	if not forward_plus["temporal_aa"] or not forward_plus["fsr1"] or not forward_plus["fsr2"] \
			or not forward_plus["ambient_occlusion"] or not forward_plus["screen_space_reflections"]:
		_fail("Forward+ capability matrix is inconsistent")
		return

	var invalid := _profile()
	invalid["render_scale"] = 1.1
	var previous_tonemap := environment.tonemap_mode
	var rejected: Dictionary = service.apply_profile(invalid, viewport, environment, camera_attributes)
	if rejected["error"] != QualityService.Status.INVALID_PROFILE or environment.tonemap_mode != previous_tonemap:
		_fail("invalid profile was accepted or partially applied")
		return

	var profile := _profile()
	profile["tonemap"] = "reinhard"
	profile["render_scale"] = 0.5
	profile["bloom"] = true
	profile["bloom_threshold"] = 0.75
	profile["fxaa"] = true
	profile["temporal_aa"] = false
	profile["ambient_occlusion"] = true
	profile["screen_space_reflections"] = true
	profile["fog"] = true
	profile["depth_effects"] = true
	profile["shadow_quality"] = "high"
	profile["sun_shadow_receiver_bias"] = 0.001
	profile["upscaler"] = "fsr2"
	var applied: Dictionary = service.apply_profile(profile, viewport, environment, camera_attributes)
	if applied["error"] != QualityService.Status.OK or applied["outcome"] != "upscaler_fallback":
		_fail("valid profile did not apply with an explicit FSR2 fallback")
		return
	if not _has_all(applied["unsupported"], [
			"upscaler", "render_scale", "fxaa", "screen_space_reflections",
			"depth_effects", "sun_shadow_receiver_bias",
	]):
		_fail("Compatibility profile omitted unsupported settings from its result")
		return
	if environment.tonemap_mode != Environment.TONE_MAPPER_REINHARDT \
			or not environment.glow_enabled or not is_equal_approx(environment.glow_hdr_threshold, 0.75) \
			or not environment.fog_enabled or not environment.ssao_enabled:
		_fail("supported tone map, bloom, fog, or occlusion settings were not applied")
		return
	if environment.ssr_enabled or viewport.scaling_3d_scale != 1.0 or viewport.use_taa \
			or viewport.screen_space_aa != Viewport.SCREEN_SPACE_AA_DISABLED \
			or camera_attributes.dof_blur_near_enabled or camera_attributes.dof_blur_far_enabled:
		_fail("unsupported Compatibility settings changed live resources")
		return
	if viewport.positional_shadow_atlas_size != 4096:
		_fail("high shadow quality did not set the Godot positional shadow atlas")
		return
	profile["upscaler"] = "none"
	profile["temporal_aa"] = true
	var taa_fallback: Dictionary = service.apply_profile(profile, viewport, environment, camera_attributes)
	if taa_fallback["error"] != QualityService.Status.OK or taa_fallback["outcome"] != "feature_fallback" \
			or not taa_fallback["unsupported"].has("temporal_aa") \
			or viewport.use_taa:
		_fail("unsupported Compatibility TAA was not reported")
		return

	profile["tonemap"] = "uchimura"
	profile["render_scale"] = 1.0
	profile["fxaa"] = false
	profile["temporal_aa"] = false
	profile["ambient_occlusion"] = false
	profile["screen_space_reflections"] = false
	profile["depth_effects"] = false
	profile["sun_shadow_receiver_bias"] = 0.0
	var approximate: Dictionary = service.apply_profile(profile, viewport, environment, camera_attributes)
	if approximate["error"] != QualityService.Status.OK or approximate["outcome"] != "approximation" \
			or environment.tonemap_mode != Environment.TONE_MAPPER_FILMIC \
			or not approximate["approximations"].has("tonemap:filmic"):
		_fail("Uchimura did not report the supported filmic tonemap approximation")
		return
	if service.apply_profile(_profile(), null, environment)["error"] != QualityService.Status.INVALID_TARGET:
		_fail("invalid quality target was not rejected")
		return
	print("Godot quality profile validation, renderer capabilities, settings application, and fallback reporting passed.")
	quit(0)

func _profile() -> Dictionary:
	return {
		"level": "medium",
		"tonemap": "aces",
		"upscaler": "none",
		"render_scale": 1.0,
		"bloom": false,
		"bloom_threshold": 1.0,
		"fxaa": false,
		"temporal_aa": false,
		"ambient_occlusion": false,
		"screen_space_reflections": false,
		"fog": false,
		"depth_effects": false,
		"shadow_quality": "medium",
		"sun_shadow_receiver_bias": 0.0,
	}

func _has_all(values: Array, expected: Array[String]) -> bool:
	for value in expected:
		if not values.has(value):
			return false
	return true

func _fail(message: String) -> void:
	push_error(message)
	quit(1)
