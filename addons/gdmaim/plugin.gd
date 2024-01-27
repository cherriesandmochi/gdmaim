@tool
extends EditorPlugin


var cfg := ConfigFile.new()
var script_processor : EditorExportPlugin
var dock : Control
var source_map_viewer : Window


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
	cfg.set_value("source_mapping", "filepath", get_script().resource_path.get_base_dir() + "/source_maps")
	cfg.set_value("source_mapping", "max_files", 10)
	cfg.set_value("source_mapping", "compress", true)
	cfg.set_value("source_mapping", "inject_name", true)
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
	dock.source_map_viewer_requested.connect(_open_source_map_viewer)
	add_control_to_dock(DOCK_SLOT_LEFT_BR, dock)
	
	source_map_viewer = preload("ui/source_map_viewer.tscn").instantiate()
	source_map_viewer._as_plugin = true
	source_map_viewer.hide()
	get_editor_interface().get_base_control().add_child(source_map_viewer)


# Called when exiting scene tree
func _exit_tree() -> void:
	remove_export_plugin(script_processor)
	
	dock._write_cfg(true)
	dock.queue_free()
	
	if is_instance_valid(source_map_viewer):
		source_map_viewer.queue_free()


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


# Opens the source map viewer
func _open_source_map_viewer() -> void:
	if source_map_viewer.visible:
		source_map_viewer.hide()
	else:
		source_map_viewer.popup_centered_clamped(source_map_viewer.size, 0.95)
