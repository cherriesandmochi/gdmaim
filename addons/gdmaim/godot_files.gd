extends RefCounted


const GodotFiles := preload("godot_files.gd")
const _ExportPlugin := preload("export_plugin.gd")

const BACKUP_PATH : String = "res://addons/gdmaim/backup/godot_files.backup"

var _files : Dictionary


static func check_backups() -> void:
	if FileAccess.file_exists(BACKUP_PATH):
		var msg := ConfirmationDialog.new()
		msg.size = Vector2i(400, 50)
		msg.dialog_close_on_escape = false
		msg.title = "GDMaim - Backup found!"
		msg.dialog_autowrap = true
		msg.dialog_text = "A backup for godot configuration files has been found.\nThis usually means, that the editor crashed or otherwise exited before finishing export.\nDo you want to restore the backup now?"
		msg.ok_button_text = "Restore"
		msg.cancel_button_text = "Close editor"
		EditorInterface.get_base_control().add_child(msg)
		msg.popup_centered()
		msg.confirmed.connect(func() -> void:
			_restore_backup()
			OS.set_restart_on_exit(true, OS.get_cmdline_args())
			EditorInterface.get_base_control().get_tree().quit())
		msg.canceled.connect(func() -> void:
			EditorInterface.get_base_control().get_tree().quit())


func clear() -> void:
	_files.clear()


func flush() -> void:
	# Create a backup for all files
	_ExportPlugin._build_data_path(BACKUP_PATH.get_base_dir())
	var backups : Dictionary
	for path in _files:
		backups[path] = _files[path].source
	var backup_file := FileAccess.open(BACKUP_PATH, FileAccess.WRITE)
	backup_file.store_var(backups)
	backup_file.close()
	
	for path in _files:
		var file := FileAccess.open(path, FileAccess.WRITE)
		file.store_buffer(_files[path].export)
		file.close()


func restore() -> void:
	for path in _files:
		var file := FileAccess.open(path, FileAccess.WRITE)
		file.store_buffer(_files[path].source)
		file.close()
	
	# Delete backup after having successfully restored all files
	if FileAccess.file_exists(BACKUP_PATH):
		DirAccess.remove_absolute(BACKUP_PATH)


func edit(path : String, source : PackedByteArray, export : PackedByteArray) -> void:
	_files[path] = ExportFile.new(source, export)


static func _restore_backup() -> void:
	var backups : Dictionary
	var backup_file := FileAccess.open(BACKUP_PATH, FileAccess.READ)
	backups = backup_file.get_var(false)
	backup_file.close()
	
	var godot_files := GodotFiles.new()
	for path in backups:
		godot_files._files[path] = ExportFile.new(backups[path], PackedByteArray())
	godot_files.restore()


class ExportFile:
	var source : PackedByteArray
	var export : PackedByteArray
	
	func _init(source : PackedByteArray, export : PackedByteArray) -> void:
		self.source = source
		self.export = export
