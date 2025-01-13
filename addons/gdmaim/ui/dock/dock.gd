@tool
extends Panel


signal source_map_viewer_requested()

const _Settings := preload("../../settings.gd")

var settings : _Settings

var _settings_nodes : Array[Node]
var _write_queued : bool = false


func _ready() -> void:
	if !settings:
		return
	
	for category in settings.get_categories():
		var label : Label = preload("dock_category.tscn").instantiate()
		label.text = category.visible_name
		$ScrollContainer/VBoxContainer.add_child(label)
		for entry in category.entries:
			if entry.visible_name:
				label = preload("dock_entry.tscn").instantiate()
				label.text = entry.visible_name
				if entry.visible_name:
					label.tooltip_text = entry.visible_name + "\n" + entry.tooltip
				$ScrollContainer/VBoxContainer.add_child(label)
			
			if entry.custom_type != entry.CustomType.NONE:
				match entry.custom_type:
					entry.CustomType.OPTIONS:
						var options : OptionButton = preload("dock_options.tscn").instantiate()
						options.settings_var = entry.var_name
						options.disabled = entry.disabled
						for option in entry.custom_data:
							options.add_item(option)
						options.item_selected.connect(_on_options_button_item_selected)
						label.add_child(options)
						register_setting(options)
			else:
				match typeof(settings.get(entry.var_name)):
					TYPE_BOOL:
						var checkbox : CheckBox = preload("dock_checkbox.tscn").instantiate()
						checkbox.settings_var = entry.var_name
						checkbox.disabled = entry.disabled
						checkbox.toggled.connect(_on_check_box_toggled)
						label.add_child(checkbox)
						register_setting(checkbox)
					TYPE_INT:
						var spinbox : SpinBox = preload("dock_spinbox.tscn").instantiate()
						spinbox.settings_var = entry.var_name
						spinbox.editable = !entry.disabled
						spinbox.value_changed.connect(_on_spin_box_value_changed)
						label.add_child(spinbox)
						register_setting(spinbox)
					TYPE_STRING:
						var lineedit : LineEdit = preload("dock_lineedit.tscn").instantiate()
						lineedit.settings_var = entry.var_name
						lineedit.editable = !entry.disabled
						lineedit.text_changed.connect(_on_line_edit_text_changed)
						if entry.visible_name:
							label.add_child(lineedit)
						else:
							$ScrollContainer/VBoxContainer.add_child(lineedit)
							lineedit.position = Vector2(0.0, -4.0)
							lineedit.set_anchors_preset(Control.PRESET_HCENTER_WIDE)
							lineedit.placeholder_text = entry.tooltip
						register_setting(lineedit)


# Registers a new settings entry
func register_setting(node : Node) -> void:
	_settings_nodes.append(node)
	node.set_settings(settings)


# Writes values to assigned config file
func _write_cfg(force : bool = false) -> void:
	if (_write_queued and !force):
		return
	
	_write_queued = true
	
	await get_tree().process_frame
	
	for setting in _settings_nodes:
		setting.serialize()
	
	settings.serialize()
	
	_write_queued = false


func _on_check_box_toggled(toggled_on : bool) -> void:
	_write_cfg()


func _on_line_edit_text_changed(new_text : String) -> void:
	_write_cfg()


func _on_spin_box_value_changed(value : float) -> void:
	_write_cfg()


func _on_options_button_item_selected(index : int) -> void:
	_write_cfg()


func _on_view_source_map_pressed() -> void:
	source_map_viewer_requested.emit()
