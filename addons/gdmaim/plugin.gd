@tool
extends EditorPlugin


const GodotFiles := preload("godot_files.gd")
const _Settings := preload("settings.gd")

var settings : _Settings
var script_processor : EditorExportPlugin
var dock : Control
var source_map_viewer : Window


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


# Called when exiting scene tree
func _exit_tree() -> void:
	remove_export_plugin(script_processor)
	
	dock._write_cfg(true)
	dock.queue_free()
	
	if is_instance_valid(source_map_viewer):
		source_map_viewer.queue_free()


# Opens the source map viewer
func _open_source_map_viewer() -> void:
	if source_map_viewer.visible:
		source_map_viewer.hide()
	else:
		source_map_viewer.popup_centered_clamped(source_map_viewer.size, 0.95)
