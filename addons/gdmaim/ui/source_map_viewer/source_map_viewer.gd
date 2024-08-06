@tool
extends Window


const ExportPlugin := preload("../../export_plugin.gd")
const _Settings := preload("../../settings.gd")

var _as_plugin : bool = false
var _source_map : Dictionary
var _script_data : Dictionary
var _source_mappings : Dictionary
var _export_mappings : Dictionary
var _file_tree_items : Dictionary
var _caret_lock : bool = false
var _prev_preprocessor_hint : String

@onready var current_file : Label = $Panel/CurrentFile
@onready var console : TextEdit = %Console
@onready var file_tree : Tree = %FileTree
@onready var source_code : TextEdit = %SourceCode
@onready var source_code_search : Panel = %SourceCodeSearch
@onready var exported_code : TextEdit = %ExportedCode
@onready var exported_code_search : Panel = %ExportedCodeSearch
@onready var source_symbols : Tree = %SourceSymbols
@onready var export_symbols : Tree = %ExportSymbols
@onready var border : StyleBox = self["theme_override_styles/embedded_border"]
@onready var border_unfocused : StyleBox = self["theme_override_styles/embedded_unfocused_border"]


func _ready() -> void:
	if !_as_plugin:
		return
	
	_setup_syntax_highlighter()
	_load_script("")
	
	visibility_changed.connect(_on_visibility_changed)
	
	var popup : PopupMenu = $Panel/HBoxContainer/MenuButton.get_popup()
	popup.index_pressed.connect(_on_search_option_selected)


func _process(delta: float) -> void:
	if !_as_plugin:
		return
	
	self["theme_override_styles/embedded_border"] = border if has_focus() else border_unfocused


func _input(event : InputEvent) -> void:
	if !_as_plugin or !has_focus():
		return
	
	if not event is InputEventKey or !event.pressed or event.echo:
		return
	
	if event.keycode == KEY_ESCAPE:
		if source_code_search.visible and (source_code.has_focus() or source_code_search.is_search_focused()):
			source_code_search.close()
		elif exported_code_search.visible and (exported_code.has_focus() or exported_code_search.is_search_focused()):
			exported_code_search.close()
		else:
			_on_close_requested()
	elif event.keycode == KEY_F and event.ctrl_pressed:
		if source_code.has_focus():
			source_code_search.open()
		elif exported_code.has_focus():
			exported_code_search.open()


func _load_source_map(path : String) -> void:
	var file := FileAccess.open(path, FileAccess.READ)
	if !file:
		push_error("GDMaim - Failed to open source map '", path, "'!")
		return
	
	var json_text : String
	var compression : int = file.get_8()
	if compression != 255:
		var data_size : int = file.get_64()
		json_text = file.get_buffer(file.get_length() - file.get_position()).decompress(data_size, compression).get_string_from_utf8()
	else:
		json_text = file.get_buffer(file.get_length() - file.get_position()).get_string_from_utf8()
	file.close()
	
	var source_map = JSON.parse_string(json_text)
	if !source_map or not source_map is Dictionary:
		push_error("GDMaim - Invalid source map '", path, "'!")
		return
	
	_source_map = source_map
	_source_map["path"] = path
	_source_map["filename"] = path.get_file()
	
	_load_script("")
	
	file_tree.clear()
	_file_tree_items.clear()
	var root : TreeItem = file_tree.create_item()
	root.set_text(0, "res://")
	
	for p : String in _source_map.get("scripts", {}).keys() + _source_map.get("resources", {}).keys():
		var folders : PackedStringArray = p.trim_prefix("res://").split("/", false)
		var prev_branch : TreeItem = root
		var cur_path : String
		for i in folders.size():
			var is_file : bool = i + 1 == folders.size()
			var folder : String = folders[i]
			cur_path += "/" + folder
			var branch : TreeItem = _file_tree_items.get(cur_path)
			if !branch:
				branch = prev_branch.create_child(-1 if is_file else prev_branch.get_meta("folder_idx", 0))
				branch.set_text(0, folder)
				_file_tree_items[cur_path] = branch
				if !is_file:
					prev_branch.set_meta("folder_idx", prev_branch.get_meta("folder_idx", 0) + 1)
			if is_file:
				branch.set_meta("path", p)
			prev_branch = branch
	
	root.set_collapsed_recursive(true)
	root.collapsed = false
	for child in root.get_children():
		child.collapsed = false
	
	source_symbols.clear()
	root = source_symbols.create_item()
	var source_symbol_table : Dictionary = _source_map.get("symbols", {}).get("source")
	for symbol in source_symbol_table:
		root.create_child().set_text(0, symbol + ": " + source_symbol_table[symbol])
	
	export_symbols.clear()
	root = export_symbols.create_item()
	var export_symbol_table : Dictionary = _source_map.get("symbols", {}).get("export")
	for symbol in export_symbol_table:
		root.create_child().set_text(0, symbol + ": " + export_symbol_table[symbol])


func _load_script(path : String) -> void:
	current_file.text = _source_map.get("filename", "")
	console.text = ""
	source_code.text = ""
	source_code.highlight_current_line = !path.is_empty()
	exported_code.text = ""
	exported_code.highlight_current_line = !path.is_empty()
	
	if !path:
		return
	
	if _source_map.get("scripts", {}).has(path):
		_script_data = _source_map.get("scripts", {}).get(path)
	elif _source_map.get("resources", {}).has(path):
		_script_data = _source_map.get("resources", {}).get(path)
	
	_script_data["path"] = path
	
	_source_mappings = _script_data.get("source_mappings", {})
	_export_mappings = _script_data.get("export_mappings", {})
	
	current_file.text += " - " + path
	console.text = _script_data.get("log", "")
	source_code.text = _script_data.get("source_code", "")
	exported_code.text = _script_data.get("export_code", "")
	
	for i in source_code.get_line_count():
		source_code.set_line_as_executing(i, _source_mappings and _source_mappings.get(str(i), -1) == -1)
	for i in exported_code.get_line_count():
		exported_code.set_line_as_executing(i, _export_mappings and _export_mappings.get(str(i), -1) == -1)


func _on_close_requested() -> void:
	hide()


func _on_open_file_pressed() -> void:
	$FileDialog.popup_centered_clamped()


func _on_file_dialog_file_selected(path: String) -> void:
	_load_source_map(path)


func _on_file_tree_item_activated() -> void:
	var selected : TreeItem = file_tree.get_selected()
	if !selected:
		return
	
	if !selected.has_meta("path"):
		selected.collapsed = !selected.collapsed
	else:
		_load_script(selected.get_meta("path", ""))


func _setup_syntax_highlighter() -> void:
	if _prev_preprocessor_hint == _Settings.current.preprocessor_prefix:
		return
	
	_prev_preprocessor_hint = _Settings.current.preprocessor_prefix
	
	var syntax_highlighter := CodeHighlighter.new()
	syntax_highlighter.function_color = Color("#66e6ff")
	syntax_highlighter.member_variable_color = Color("#bce0ff")
	syntax_highlighter.number_color = Color("#a1ffe0")
	syntax_highlighter.symbol_color = Color("#abc9ff")
	
	syntax_highlighter.add_color_region("'", "'", Color("#ffeda1"))
	syntax_highlighter.add_color_region('"', '"', Color("#ffeda1"))
	
	syntax_highlighter.add_color_region("#", "", Color("#cdcfd280"))
	syntax_highlighter.add_color_region(_prev_preprocessor_hint, "", Color("#7945c1")) #99b3cccc
	
	for keyword in ["var", "func", "signal", "enum", "const", "class", "class_name", "extends", "static", "self", "await", "super", "and", "or", "not", "is", "true", "false", "null", "load", "preload", "print", "prints"]:
		syntax_highlighter.add_keyword_color(keyword, Color("#ff7085"))
	
	for keyword in ["if", "else", "elif", "for", "in", "while", "return", "continue", "break", "pass", "match", "case"]:
		syntax_highlighter.add_keyword_color(keyword, Color("#ff8ccc"))
	
	for keyword in ["tool", "export", "export_range", "onready", "rpc"]:
		syntax_highlighter.add_keyword_color(keyword, Color("#ffb373"))
	
	for keyword in ["void", "bool", "int", "float", "String", "Array", "Dictionary", "Vector2", "Vector2i", "Vector3", "Vector3i", "Vector4", "Vector4i", "Transform2D", "Transform3D", "Quaternion", "Basis"]:
		syntax_highlighter.add_keyword_color(keyword, Color("#42ffc2"))
	
	source_code.syntax_highlighter = syntax_highlighter
	exported_code.syntax_highlighter = syntax_highlighter.duplicate()


func _set_folder_visible_recursive(tree_item : TreeItem, visible : bool) -> void:
	if tree_item.has_meta("collapsed"):
		tree_item.collapsed = tree_item.get_meta("collapsed")
		tree_item.remove_meta("collapsed")
	tree_item.visible = visible
	for child in tree_item.get_children():
		_set_folder_visible_recursive(child, visible)


func _set_file_visible(tree_item : TreeItem, visible : bool, cache_collapsed : bool = true) -> void:
	if cache_collapsed and !tree_item.has_meta("collapsed"):
		tree_item.set_meta("collapsed", tree_item.collapsed)
	tree_item.collapsed = false
	tree_item.visible = visible
	if tree_item.get_parent():
		_set_file_visible(tree_item.get_parent(), visible)


func _try_lock_caret() -> bool:
	if _caret_lock:
		return false
	else:
		_caret_lock = true
		_unlock_caret()
		return true


func _unlock_caret() -> void:
	if is_inside_tree():
		await get_tree().process_frame
	_caret_lock = false


func _on_visibility_changed() -> void:
	if visible:
		_setup_syntax_highlighter()


func _on_source_code_caret_changed() -> void:
	if _try_lock_caret():
		var mapped_line : int = _source_mappings.get(str(source_code.get_caret_line()), -1) if _source_mappings else source_code.get_caret_line()
		exported_code.highlight_current_line = mapped_line != -1
		if exported_code.highlight_current_line:
			exported_code.set_caret_line(mapped_line)


func _on_exported_code_caret_changed() -> void:
	if _try_lock_caret():
		var mapped_line : int = _export_mappings.get(str(exported_code.get_caret_line()), -1) if _export_mappings else exported_code.get_caret_line()
		source_code.highlight_current_line = mapped_line != -1
		if source_code.highlight_current_line:
			source_code.set_caret_line(mapped_line)


func _on_source_code_gui_input(event: InputEvent) -> void:
	if event is InputEventMouseButton:
		source_code.highlight_current_line = true


func _on_exported_code_gui_input(event: InputEvent) -> void:
	if event is InputEventMouseButton:
		exported_code.highlight_current_line = true


func _on_file_filter_text_changed(new_text : String) -> void:
	if !new_text:
		var selected : TreeItem = file_tree.get_selected()
		_set_folder_visible_recursive(file_tree.get_root(), true)
		if selected:
			_set_file_visible(selected, true, false)
			file_tree.set_selected(selected, 0)
		return
	
	_set_folder_visible_recursive(file_tree.get_root(), false)
	for file in _file_tree_items:
		if file.get_file().findn(new_text) != -1:
			_set_file_visible(_file_tree_items[file], true)


func _on_search_option_selected(idx : int) -> void:
	match idx:
		0:
			source_code_search.open()
		1:
			exported_code_search.open()
