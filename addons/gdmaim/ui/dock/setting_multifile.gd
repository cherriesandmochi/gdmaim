@tool
extends "setting.gd"

var files : String:
	get = _get_files,
	set = _set_files

signal text_changed(txt : String)

@onready var line_edit: LineEdit = $LineEdit
@onready var button: Button = $Button
@onready var file_dialog: FileDialog = $FileDialog

func _get_files() -> String:
	if !line_edit:
		return ""
	return line_edit.text
	
func _set_files(value : String) -> void:
	if !line_edit:
		return
	line_edit.text = value.strip_edges()
	
func _open_dialog() -> void:
	if !file_dialog.visible:
		file_dialog.popup_centered_clamped()
		
func _on_text_change(txt : String) -> void:
	text_changed.emit(txt)

func set_files(value : String) -> void:
	if !line_edit:
		return
		
	var current : String = line_edit.text
	
	if current.length() == 0:
		current = value.strip_edges()
	else:
		value = value.strip_edges()
		
		var rgx : RegEx = RegEx.create_from_string("\\b{0}\\b".format([value]))
		if rgx and rgx.search(current):
			return
			
		current = str(current, ";", value.strip_edges())
		
	_set_files(current)

func _on_selection(value : Variant) -> void:
	var size : int = line_edit.text.length()
	
	if value is String:
		set_files(value)
	elif value is PackedStringArray:
		set_files(";".join(value))
	else:
		printerr("Error, not valid value type!")
	
	if size != line_edit.text.length():
		line_edit.caret_column = line_edit.text.length()
		
	text_changed.emit(_get_files())

func _ready() -> void:
	if !line_edit or !button:
		printerr("Error, can`t find main node/s!")
		return
	
	button.pressed.connect(_open_dialog)
	file_dialog.dir_selected.connect(_on_selection)
	file_dialog.file_selected.connect(_on_selection)
	file_dialog.files_selected.connect(_on_selection)
	line_edit.text_changed.connect(_on_text_change)

	var _this = self
	if _this is Control:
		_this.set_anchors_and_offsets_preset(Control.PRESET_TOP_RIGHT)
