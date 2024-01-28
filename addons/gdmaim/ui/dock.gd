@tool
extends Panel


signal changed()
signal source_map_viewer_requested()

var cfg : ConfigFile

var _write_queued : bool = false

@onready var obfuscation_enabled : CheckBox = $ScrollContainer/VBoxContainer/EnableObfuscation/CheckBox
@onready var inline_consts : CheckBox = $ScrollContainer/VBoxContainer/InlineConstants/CheckBox
@onready var inline_enums : CheckBox = $ScrollContainer/VBoxContainer/InlineEnums/CheckBox
@onready var export_vars : CheckBox = $ScrollContainer/VBoxContainer/ObfuscateExportVars/CheckBox
@onready var signals : CheckBox = $ScrollContainer/VBoxContainer/ObfuscateSignals/CheckBox
@onready var id_prefix : LineEdit = $ScrollContainer/VBoxContainer/IDPrefix/LineEdit
@onready var id_characters : LineEdit = $ScrollContainer/VBoxContainer/IDCharacterList/LineEdit
@onready var id_target_length : SpinBox = $ScrollContainer/VBoxContainer/IDTargetLength/SpinBox
@onready var generator_seed : SpinBox = $ScrollContainer/VBoxContainer/GeneratorSeed/SpinBox
@onready var dynamic_seed : CheckBox = $ScrollContainer/VBoxContainer/DynamicSeed/CheckBox
@onready var strip_comments : CheckBox = $ScrollContainer/VBoxContainer/StripComments/CheckBox
@onready var strip_empty_lines : CheckBox = $ScrollContainer/VBoxContainer/StripEmptyLines/CheckBox
@onready var regex_filter_enabled : CheckBox = $ScrollContainer/VBoxContainer/RegExFilters/CheckBox
@onready var regex_filter : LineEdit = $ScrollContainer/VBoxContainer/RegExFilter
@onready var feature_filters : CheckBox = $ScrollContainer/VBoxContainer/FeatureFilters/CheckBox
@onready var source_map_path : LineEdit = $ScrollContainer/VBoxContainer/SourceMapPath/LineEdit
@onready var source_map_max_files : SpinBox = $ScrollContainer/VBoxContainer/SourceMapMaxFiles/SpinBox
@onready var source_map_max_compress : CheckBox = $ScrollContainer/VBoxContainer/SourceMapCompress/CheckBox
@onready var source_map_max_inject_name : CheckBox = $ScrollContainer/VBoxContainer/SourceMapInjectName/CheckBox
@onready var debug_scripts : LineEdit = $ScrollContainer/VBoxContainer/DebugScripts/LineEdit
@onready var debug_resources : LineEdit = $ScrollContainer/VBoxContainer/DebugResources/LineEdit
@onready var obfuscate_debug_only : CheckBox = $ScrollContainer/VBoxContainer/ObfuscateDebugOnly/CheckBox


# Called when ready
func _ready() -> void:
	_read_cfg()


# Assigns a cfg file
func set_cfg(cfg : ConfigFile) -> void:
	self.cfg = cfg


# Applies the values set in the assigned config file
func _read_cfg() -> void:
	if !cfg:
		return
	
	obfuscation_enabled.button_pressed = cfg.get_value("obfuscator", "enabled", false)
	inline_consts.button_pressed = cfg.get_value("obfuscator", "inline_consts", false)
	inline_enums.button_pressed = cfg.get_value("obfuscator", "inline_enums", false)
	export_vars.button_pressed = cfg.get_value("obfuscator", "export_vars", false)
	signals.button_pressed = cfg.get_value("obfuscator", "signals", false)
	id_prefix.text = cfg.get_value("id", "prefix", "")
	id_characters.text = cfg.get_value("id", "character_list", "")
	id_target_length.value = cfg.get_value("id", "target_length", 0)
	generator_seed.value = cfg.get_value("id", "seed", 0)
	dynamic_seed.button_pressed = cfg.get_value("id", "dynamic_seed", false)
	strip_comments.button_pressed = cfg.get_value("post_process", "strip_comments", false)
	strip_empty_lines.button_pressed = cfg.get_value("post_process", "strip_empty_lines", false)
	feature_filters.button_pressed = cfg.get_value("post_process", "feature_filters", false)
	regex_filter_enabled.button_pressed = cfg.get_value("post_process", "regex_filter_enabled", false)
	regex_filter.text = cfg.get_value("post_process", "regex_filter", "")
	source_map_path.text = cfg.get_value("source_mapping", "filepath", "")
	source_map_max_files.value = cfg.get_value("source_mapping", "max_files", 1)
	source_map_max_compress.button_pressed = cfg.get_value("source_mapping", "compress", false)
	source_map_max_inject_name.button_pressed = cfg.get_value("source_mapping", "inject_name", false)
	debug_scripts.text = cfg.get_value("debug", "debug_scripts", "")
	debug_resources.text = cfg.get_value("debug", "debug_resources", "")
	obfuscate_debug_only.button_pressed = cfg.get_value("debug", "obfuscate_debug_only", false)


# Writes values to assigned config file
func _write_cfg(force : bool = false) -> void:
	if !cfg or (_write_queued and !force):
		return
	
	_write_queued = true
	
	await get_tree().process_frame
	
	cfg.set_value("obfuscator", "enabled", obfuscation_enabled.button_pressed)
	cfg.set_value("obfuscator", "inline_consts", inline_consts.button_pressed)
	cfg.set_value("obfuscator", "inline_enums", inline_enums.button_pressed)
	cfg.set_value("obfuscator", "export_vars", export_vars.button_pressed)
	cfg.set_value("obfuscator", "signals", signals.button_pressed)
	cfg.set_value("id", "prefix", id_prefix.text)
	cfg.set_value("id", "character_list", id_characters.text)
	cfg.set_value("id", "target_length", int(id_target_length.value))
	cfg.set_value("id", "seed", int(generator_seed.value))
	cfg.set_value("id", "dynamic_seed", dynamic_seed.button_pressed)
	cfg.set_value("post_process", "strip_comments", strip_comments.button_pressed)
	cfg.set_value("post_process", "strip_empty_lines", strip_empty_lines.button_pressed)
	cfg.set_value("post_process", "feature_filters", feature_filters.button_pressed)
	cfg.set_value("post_process", "regex_filter_enabled", regex_filter_enabled.button_pressed)
	cfg.set_value("post_process", "regex_filter", regex_filter.text)
	cfg.set_value("source_mapping", "filepath", source_map_path.text)
	cfg.set_value("source_mapping", "max_files", int(source_map_max_files.value))
	cfg.set_value("source_mapping", "compress", source_map_max_compress.button_pressed)
	cfg.set_value("source_mapping", "inject_name", source_map_max_inject_name.button_pressed)
	cfg.set_value("debug", "debug_scripts", debug_scripts.text)
	cfg.set_value("debug", "debug_resources", debug_resources.text)
	cfg.set_value("debug", "obfuscate_debug_only", obfuscate_debug_only.button_pressed)
	
	changed.emit()
	
	_write_queued = false


func _on_check_box_toggled(toggled_on : bool) -> void:
	_write_cfg()


func _on_line_edit_text_changed(new_text : String) -> void:
	_write_cfg()


func _on_spin_box_value_changed(value : float) -> void:
	_write_cfg()


func _on_view_source_map_pressed() -> void:
	source_map_viewer_requested.emit()
