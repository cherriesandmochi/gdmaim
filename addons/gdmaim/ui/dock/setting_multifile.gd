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
		file_dialog.popup_centered()
		
func _on_text_change() -> void:
	text_changed.emit(line_edit.text)

func _on_selection(value : Variant) -> void:
	if value is String:
		_set_files(value)
	elif value is PackedStringArray:
		_set_files(";".join(value))
	else:
		printerr("Error, not valid value type!")
		
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
