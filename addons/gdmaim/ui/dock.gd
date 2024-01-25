@tool
extends Panel


signal changed()

var cfg : ConfigFile

var _write_queued : bool = false

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
@onready var feature_filters : CheckBox = $ScrollContainer/VBoxContainer/FeatureFilters/CheckBox
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
	debug_scripts.text = cfg.get_value("debug", "debug_scripts", "")
	debug_resources.text = cfg.get_value("debug", "debug_resources", "")
	obfuscate_debug_only.button_pressed = cfg.get_value("debug", "obfuscate_debug_only", false)


# Writes values to assigned config file
func _write_cfg(force : bool = false) -> void:
	if !cfg or (_write_queued and !force):
		return
	
	_write_queued = true
	
	await get_tree().process_frame
	
	cfg.set_value("obfuscator", "inline_consts", inline_consts.button_pressed)
	cfg.set_value("obfuscator", "inline_enums", inline_enums.button_pressed)
	cfg.set_value("obfuscator", "export_vars", export_vars.button_pressed)
	cfg.set_value("obfuscator", "signals", signals.button_pressed)
	cfg.set_value("id", "prefix", id_prefix.text)
	cfg.set_value("id", "character_list", id_characters.text)
	cfg.set_value("id", "target_length", id_target_length.value)
	cfg.set_value("id", "seed", generator_seed.value)
	cfg.set_value("id", "dynamic_seed", dynamic_seed.button_pressed)
	cfg.set_value("post_process", "strip_comments", strip_comments.button_pressed)
	cfg.set_value("post_process", "strip_empty_lines", strip_empty_lines.button_pressed)
	cfg.set_value("post_process", "feature_filters", feature_filters.button_pressed)
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
