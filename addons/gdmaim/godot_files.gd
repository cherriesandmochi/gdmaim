extends RefCounted


var _files : Dictionary


func clear() -> void:
	_files.clear()


func flush() -> void:
	for path in _files:
		var file := FileAccess.open(path, FileAccess.WRITE)
		file.store_buffer(_files[path].export)
		file.close()


func restore() -> void:
	for path in _files:
		var file := FileAccess.open(path, FileAccess.WRITE)
		file.store_buffer(_files[path].source)
		file.close()


func edit(path : String, source : PackedByteArray, export : PackedByteArray) -> void:
	_files[path] = ExportFile.new(source, export)


class ExportFile:
	var source : PackedByteArray
	var export : PackedByteArray
	
	func _init(source : PackedByteArray, export : PackedByteArray) -> void:
		self.source = source
		self.export = export
