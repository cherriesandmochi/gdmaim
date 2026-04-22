@tool
extends EditorPlugin


const GodotFiles := preload("godot_files.gd")
const _Settings := preload("settings.gd")

var settings : _Settings
var script_processor : EditorExportPlugin
var dock : Control
var source_map_viewer : Window

var _cfg_hash : String = ""


# Called when entering scene tree
func _enter_tree() -> void:
	name = "GDMaim"
	
	GodotFiles.check_backups()
	settings = _Settings.new()
	
	script_processor = preload("export_plugin.gd").new()
	script_processor.settings = settings
	add_export_plugin(script_processor)
	
	dock = preload("ui/dock/dock.tscn").instantiate()
	dock.settings = settings
	dock.source_map_viewer_requested.connect(_open_source_map_viewer)
	add_control_to_dock(DOCK_SLOT_LEFT_BR, dock)
	
	source_map_viewer = preload("ui/source_map_viewer/source_map_viewer.tscn").instantiate()
	source_map_viewer._as_plugin = true
	source_map_viewer.hide()
	get_editor_interface().get_base_control().add_child(source_map_viewer)

	_setup()

# Called when exiting scene tree
func _exit_tree() -> void:
	remove_export_plugin(script_processor)
	
	if is_instance_valid(dock):
		dock._write_cfg(true)
		
		remove_control_from_docks(dock)
		dock.queue_free()
	
	if is_instance_valid(source_map_viewer):
		source_map_viewer.queue_free()
		
		
	var res : EditorFileSystem = get_editor_interface().get_resource_filesystem()
	if res and res.filesystem_changed.is_connected(_check_settings):
		res.filesystem_changed.disconnect(_check_settings)


# Opens the source map viewer
func _open_source_map_viewer() -> void:
	if source_map_viewer.visible:
		source_map_viewer.hide()
	else:
		source_map_viewer.popup_centered_clamped(source_map_viewer.size, 0.95)


# Pre-validations
func _setup() -> void:
	var file : String = settings.custom_token_ignore_file_path
		
	if !file.is_empty():
		var base : String = file.get_base_dir()
		
		if !DirAccess.dir_exists_absolute(base):
			DirAccess.make_dir_recursive_absolute(base)
			
		if !FileAccess.file_exists(file):
			var fs : FileAccess = FileAccess.open(file, FileAccess.WRITE)
			if is_instance_valid(fs):
				fs.store_string("# Here place you custom tokens, must be a list of tokens.\n# For example check the readme in 'https://github.com/cherriesandmochi/gdmaim' <3")
				fs.close()
				
	_check_settings()
		
	var res : EditorFileSystem = get_editor_interface().get_resource_filesystem()
	if res and !res.filesystem_changed.is_connected(_check_settings):
		res.filesystem_changed.connect(_check_settings)
	
	
func _check_settings() -> void:
	var expath : String = "res://export_presets.cfg"
	
	if !FileAccess.file_exists(expath):
		return
	
	var md5 : String = FileAccess.get_md5(expath)
	if md5 != _cfg_hash:
		if _export_init():
			var res : EditorFileSystem = get_editor_interface().get_resource_filesystem()
			res.update_file(expath)
			res.scan.call_deferred()
			md5 = FileAccess.get_md5(expath)
		_cfg_hash = md5


func _export_init() -> bool:
	var path : String = "res://export_presets.cfg"
	if !FileAccess.file_exists(path):
		return false
		
	var excl : String= "exclude_filter"
	var plug : String = get_script().resource_path
	
	if !plug.begins_with("res://"):
		false
		
	plug = plug.get_base_dir().path_join("*")
	
	var config : ConfigFile = ConfigFile.new()
	var err : int = config.load(path)
	var dirty : bool = false
	
	if err == OK:
		for sector : String in config.get_sections():
			var filters : String = config.get_value(sector, excl, "")
			if plug in filters:
				if !sector.ends_with(".options") and !config.has_section(sector + ".options"):
					config.set_value(sector + ".options", excl, plug)
				continue
			if !filters.is_empty():
				filters += ","
			filters += plug
			config.set_value(sector, excl, filters)
			dirty = true
			
		if dirty:
			config.save(path)
	config = null
	return dirty
