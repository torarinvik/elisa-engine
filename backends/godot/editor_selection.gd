extends RefCounted

const ScenePicking = preload("res://scene_picking.gd")

# Applies only the editor's outline material and restores the host material
# overlay if the selection changes or the selected node is destroyed.
var _selected_visual: WeakRef
var _previous_overlay: Material
var _outline_material: Material
var _gameplay_epoch := 0
var _gameplay_id := 0

func apply_pick(pick: Dictionary, outline_material: Material) -> bool:
	if pick.get("status") != ScenePicking.Status.HIT or outline_material == null:
		clear()
		return false
	var visual := pick.get("visual") as GeometryInstance3D
	if visual == null or not is_instance_valid(visual):
		clear()
		return false
	var epoch_value: Variant = pick.get("gameplay_epoch", 0)
	var entity_value: Variant = pick.get("gameplay_id", 0)
	if typeof(epoch_value) != TYPE_INT or typeof(entity_value) != TYPE_INT:
		clear()
		return false
	var epoch: int = epoch_value
	var entity_id: int = entity_value
	if epoch <= 0 or entity_id <= 0:
		clear()
		return false
	clear()
	_previous_overlay = visual.material_overlay
	_outline_material = outline_material
	_selected_visual = weakref(visual)
	_gameplay_epoch = epoch
	_gameplay_id = entity_id
	visual.material_overlay = outline_material
	return true

func clear() -> void:
	if _selected_visual != null:
		var visual := _selected_visual.get_ref() as GeometryInstance3D
		if is_instance_valid(visual) and visual.material_overlay == _outline_material:
			visual.material_overlay = _previous_overlay
	_selected_visual = null
	_previous_overlay = null
	_outline_material = null
	_gameplay_epoch = 0
	_gameplay_id = 0

func identity() -> Dictionary:
	if _selected_visual == null:
		return {"gameplay_epoch": 0, "gameplay_id": 0}
	if not is_instance_valid(_selected_visual.get_ref()):
		clear()
		return {"gameplay_epoch": 0, "gameplay_id": 0}
	return {"gameplay_epoch": _gameplay_epoch, "gameplay_id": _gameplay_id}
