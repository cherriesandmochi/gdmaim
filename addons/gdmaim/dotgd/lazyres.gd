## ~/CodeNameTwister $
## ** LazyRes **
## Api Compatible Conversion
## Don`t ask just fun

## Class LazyRes
extends RefCounted
const FOLDER : String = "res://.bridgetmp/"

var save_flags : int = 0
var use_buffer_clear : bool = true

var _setup_dirty : bool = false
var _buffered : Dictionary = {}

static func _dir_clear(path : String):
	var dir : DirAccess = DirAccess.open(path)
	if dir:
		dir.list_dir_begin()
		var file_name : String = dir.get_next()
		while file_name != "":
			var file_path : String = str(path.trim_suffix("/"), "/", file_name)
			if dir.current_is_dir():
				_dir_clear(file_path)
			else:
				DirAccess.remove_absolute(file_path)
			file_name = dir.get_next()
	else:
		print("Lazy2 err: An error occurred when trying to access the tmp path. ")

static func grant_clear() -> void:
	if DirAccess.dir_exists_absolute(FOLDER):
		_dir_clear(FOLDER)

## Clear all buffer files
func clear() -> void:
	for b in _buffered.values():
		if FileAccess.file_exists(b):
			DirAccess.remove_absolute(b)
	_buffered.clear()
	_setup_dirty = false
	if DirAccess.dir_exists_absolute(FOLDER):
		#detailed clear
		_dir_clear(FOLDER)
		DirAccess.remove_absolute(FOLDER)

## return true if binary was proccesed, only work if defined use_buffer_clear is true.
func was_binary_desiarealized(binary_path : String) -> bool:
	return _buffered.has(binary_path)

func _notification(what: int) -> void:
	if what == NOTIFICATION_PREDELETE:
		#INLINE weakref clear
		for b in _buffered.values():
			if FileAccess.file_exists(b):
				DirAccess.remove_absolute(b)
		_buffered.clear()
		grant_clear()
		if DirAccess.dir_exists_absolute(FOLDER):
			DirAccess.remove_absolute(FOLDER)

func _lazy2(path : String, ext : String, res : Resource = null) -> Array:
	var data : String = ""
	if !FileAccess.file_exists(path):
		push_error("Lazy2 err: ",path, " Not valid!")
	else:
		if null == res:
			res = ResourceLoader.load(path, "", ResourceLoader.CACHE_MODE_IGNORE)
		if null != res:
			if !_setup_dirty:
				_setup_dirty = true
				if !DirAccess.dir_exists_absolute(FOLDER):
					DirAccess.make_dir_recursive_absolute(FOLDER)
			var p2 : String = str(FOLDER,path.get_file().trim_suffix(str(".", path.get_extension())), ".", ext)
			var index : int = 0
			while FileAccess.file_exists(p2):
				p2 = str(path, index, ".", ext)
				index += 1
			var err : int = ResourceSaver.save(res, p2, save_flags)
			if err != OK:
				push_error("Lazy2 err: ", err," An error on save ", p2)
			else:
				data = FileAccess.get_file_as_string(p2)
				if use_buffer_clear:
					_buffered[path] = p2 #DIRTY
				else:
					DirAccess.remove_absolute(p2)
				path = p2
	return [path, data]

## Parse text data to binary data
func parse_to_binary(path : String, data : String, out_type : String = "res") -> PackedByteArray:
	if data.is_empty():
		push_error("Lazy2 err: Data Empty!! ", path)
		return []
	var default_ext : String = path.get_extension()
	var tmp_ext : String = ""

	if default_ext == "scn" or default_ext == "tscn":
		tmp_ext = "tscn"
	elif default_ext == "res" or default_ext == "tres":
		tmp_ext = "tres"

	if tmp_ext != "tres" and tmp_ext != "tscn":
		push_warning("Lazy2 err: Not valid text file: ", path)
		return []

	var new_path : String = str(FOLDER, path.get_file().trim_suffix(str(".", default_ext)), ".", tmp_ext)
	var file : FileAccess = FileAccess.open(new_path,FileAccess.WRITE)
	if !file:
		push_warning("Lazy2 err: Can not create file: ", path)
		return []
	file.store_string(data)
	file.close()
	_buffered[path] = new_path
	var rs : Resource = ResourceLoader.load(new_path, "",ResourceLoader.CACHE_MODE_IGNORE)
	new_path = str(FOLDER, path.get_file().trim_suffix(str(".", default_ext)), ".", out_type)
	if  ResourceSaver.save(rs, new_path) != OK:
		push_error("Lazy2 err: Can not save file ", new_path)
		return []
	return FileAccess.get_file_as_bytes(new_path)

func _get_type(path : String) -> Array:
	var res : Resource = ResourceLoader.load(path, "", ResourceLoader.CACHE_MODE_IGNORE)
	if null == res:
		push_warning("Lazy2 err: Not vaild resource file: ", path)
		return []
	if res is PackedScene:
		return ["tscn", res]
	return ["tres", res]

## return Array([path, data])
## 	@path Locate text file.
## 	@data Text file data, if binary can not convert this return empty string.
func get_data(path : String) -> Array:
	var ext : String = path.get_extension()
	var data : String = ""
	match ext:
		"tscn":
			data = FileAccess.get_file_as_string(path)
		"scn":
			if use_buffer_clear and was_binary_desiarealized(path):
				path = _buffered[path]
				data = FileAccess.get_file_as_string(path)
			else:
				var lazy : Array = _lazy2(path, "tscn")
				path = lazy[0]
				data = lazy[1]
		"tres":
			data = FileAccess.get_file_as_string(path)
		"res":
			if use_buffer_clear and was_binary_desiarealized(path):
				path = _buffered[path]
				data = FileAccess.get_file_as_string(path)
			else:
				var lazy : Array = _get_type(path)
				if lazy.size() > 0:
					lazy = _lazy2(path, lazy[0], lazy[1])
					path = lazy[0]
					data = lazy[1]
	return [path, data]
