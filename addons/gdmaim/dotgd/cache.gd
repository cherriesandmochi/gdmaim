## ~/CodeNameTwister $
## ** GodotCache **
extends RefCounted
const DEFAULT_ENGINE_CACHE_FOLDER : String ="res://.godot/" #res://.aquadot/ | res://.redot/

var _cache_paths : Array[String] = []
var obfuscate_token_callback : Callable
var obfuscate_path_callback : Callable

func reset() -> void:
	_cache_paths.clear()
	obfuscate_token_callback = Callable()
	obfuscate_path_callback = Callable()

func parse() -> void:
	parse_global_script_class_cache()
	#TODO: Parse functions

func parse_global_script_class_cache(path : String = "") -> void:
	if !obfuscate_token_callback.is_valid():return
	if path.is_empty():
		path = ProjectSettings.globalize_path(str(DEFAULT_ENGINE_CACHE_FOLDER, "global_script_class_cache.cfg"))
	if !FileAccess.file_exists(path):
		push_warning("Cache file not exit!")
		return

	#Restore uncleaned tmp file on force close by unstable engine version!
	restore(path)

	var cfg : ConfigFile = ConfigFile.new()
	cfg.load(path)

	var data : Array[Dictionary] = cfg.get_value("", "list", [])
	if data.size() > 0:
		#SAVE TEMP
		cfg.save(str(path, ".tmp"))

		if obfuscate_path_callback.is_valid():
			for x in range(data.size()):
				var value : Dictionary = data[x]
				#DATA EXPECTED
				var base : StringName = value["base"]
				var src_class : StringName = value["class"]
				var rs_path : StringName = value["path"]
				#OBFUSCATE
				value["base"] = obfuscate_token_callback.call(base)
				value["class"] = obfuscate_token_callback.call(src_class)
				value["path"] = obfuscate_path_callback.call(rs_path)
		else:
			for x in range(data.size()):
				var value : Dictionary = data[x]
				#DATA EXPECTED
				var base : StringName = value["base"]
				var src_class : StringName = value["class"]
				#OBFUSCATE
				value["base"] = obfuscate_token_callback.call(base)
				value["class"] = obfuscate_token_callback.call(src_class)

		#HACKTHIS
		cfg.set_value("", "list", data)
		cfg.save(path)
		_cache_paths.append(path)

func restore(path : String) -> void:
	var temp : String = str(path, ".tmp")
	if FileAccess.file_exists(temp):
		if FileAccess.file_exists(path):
			DirAccess.remove_absolute(path)
		DirAccess.rename_absolute(temp, path)

func clear() -> void:
	for path : String in _cache_paths:
		restore(path)
	_cache_paths.clear()

func _notification(what: int) -> void:
	if what == NOTIFICATION_PREDELETE:
		#INLINE weakref clear
		for path : String in _cache_paths:
			var temp : String = str(path, ".tmp")
			if FileAccess.file_exists(temp):
				if FileAccess.file_exists(path):
					DirAccess.remove_absolute(path)
				DirAccess.rename_absolute(temp, path)
		_cache_paths.clear()
