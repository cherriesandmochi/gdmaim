@tool
extends EditorPlugin


var cfg := ConfigFile.new()
var script_processor : EditorExportPlugin
var dock : Control


# Called when entering scene tree
func _enter_tree() -> void:
	name = "GDMaim"
	
	cfg.set_value("obfuscator", "inline_consts", true)
	cfg.set_value("obfuscator", "inline_enums", true)
	cfg.set_value("obfuscator", "export_vars", true)
	cfg.set_value("obfuscator", "signals", true)
	cfg.set_value("id", "prefix", "__")
	cfg.set_value("id", "character_list", "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_")
	cfg.set_value("id", "target_length", 4)
	cfg.set_value("id", "seed", 0)
	cfg.set_value("id", "dynamic_seed", false)
	cfg.set_value("post_process", "strip_comments", true)
	cfg.set_value("post_process", "strip_empty_lines", true)
	cfg.set_value("post_process", "feature_filters", true)
	cfg.set_value("debug", "debug_scripts", "")
	cfg.set_value("debug", "debug_resources", "")
	cfg.set_value("debug", "obfuscate_debug_only", false)
	_load_cfg()
	
	script_processor = preload("export_plugin.gd").new()
	script_processor.cfg = cfg
	add_export_plugin(script_processor)
	
	dock = preload("ui/dock.tscn").instantiate()
	dock.set_cfg(cfg)
	dock.changed.connect(_save_cfg)
	add_control_to_dock(DOCK_SLOT_LEFT_BR, dock)


# Called when exiting scene tree
func _exit_tree() -> void:
	remove_export_plugin(script_processor)
	
	dock._write_cfg(true)
	dock.queue_free()


# Returns the target config directory
func _get_cfg_dir() -> String:
	return get_script().resource_path.get_base_dir()


# Loads the export cfg
func _load_cfg() -> void:
	cfg.load(_get_cfg_dir() + "/export.cfg")


# Loads the export cfg
func _save_cfg() -> void:
	if !DirAccess.dir_exists_absolute(_get_cfg_dir()):
		DirAccess.make_dir_recursive_absolute(_get_cfg_dir())
	cfg.save(_get_cfg_dir() + "/export.cfg")
