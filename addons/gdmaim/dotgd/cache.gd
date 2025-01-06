## ~/CodeNameTwister $
## ** GodotCache **
extends RefCounted
const DEFAULT_ENGINE_CACHE_FOLDER : String ="res://.godot/" #res://.bridge/ | res://.redot/
const SETTINGS_PATH : String = "res://project.godot" # Text settings file
const UID_PATH : String = DEFAULT_ENGINE_CACHE_FOLDER + "uid_cache.bin"

#region DEVUSER_PREFERENCES
var parse_uid : bool = false #WARN: Make more time for export!!

## This prevent break uids if you set parse_uid as false and has not real obfuscate_path_callback or the logic depends only of remaps
var force_use_path_param : bool = false #Safe in the maintance workflow
#endregion

var _cache_paths : Array[String] = []
var obfuscate_token_callback : Callable
var obfuscate_path_callback : Callable

var _project_settings_origin : ConfigFile = null

func reset() -> void:
	_cache_paths.clear()
	obfuscate_token_callback = Callable()
	obfuscate_path_callback = Callable()

func parse() -> void:
	parse_global_script_class_cache()
	parse_project_settings()
	if parse_uid:
		parse_uid_cache()
	else:
		if force_use_path_param and obfuscate_path_callback.is_valid():
			#Force to use path param setted in exported proyects
			if FileAccess.file_exists(UID_PATH):
				DirAccess.rename_absolute(UID_PATH, str(UID_PATH, ".tmp"))
	#TODO: Parse functions

func parse_uid_cache(path : String = "") -> void:
	if !obfuscate_path_callback.is_valid():return
	if path.is_empty():
		path = ProjectSettings.globalize_path(UID_PATH)
	if !FileAccess.file_exists(path):
		push_warning("Cache file not exit! ", path)
		return

	var unique_ids : Dictionary = {}
	var file : FileAccess = FileAccess.open(path,FileAccess.READ)
	if !file:
		push_warning("Cache err, Can not open uid bin!")
		return
	var size : int = file.get_32()

	for x : int in range(size):
		var id : int = file.get_64()
		var len : int = file.get_32()
		var packed : PackedByteArray = []
		packed.resize(len)
		packed = file.get_buffer(packed.size())
		unique_ids[id] = packed
	file.close()

	DirAccess.copy_absolute(path, str(path, ".tmp"))
	file = FileAccess.open(path, FileAccess.WRITE)
	if !file:
		push_warning("Cache err, Can not open uid bin!")
		return
	file.store_32(unique_ids.size())
	for id in unique_ids.keys():
		var data : String = obfuscate_path_callback.call((unique_ids[id] as PackedByteArray).get_string_from_utf8())
		var packed : PackedByteArray = data.to_utf8_buffer()
		file.store_64(id)
		file.store_32(packed.size())
		file.store_buffer(packed)
	file.close()

func get_project_settings(path : String = "") -> ConfigFile:
	var cfg : ConfigFile = null
	if path.is_empty():
		path = ProjectSettings.globalize_path(str(SETTINGS_PATH))
	if obfuscate_path_callback.is_valid():
		if !FileAccess.file_exists(path):
			push_warning("Cache file not exit!")
			return
		cfg = ConfigFile.new()
		cfg.load(path)

		if cfg.get_sections().size() > 0:
			#var data : Array[Dictionary] = cfg.get_value("", "list", [])
			for s in cfg.get_sections():
				for k in cfg.get_section_keys(s):
					var value : Variant = cfg.get_value(s, k, null)
					if value is String:
						var buffer : String = value# value.is_absolute_path()
						var has_asteric : bool = false
						if buffer.begins_with("*"):
							has_asteric = true
							buffer = buffer.trim_prefix("*")
						if buffer.is_absolute_path():
							buffer = obfuscate_path_callback.call(buffer)
							if has_asteric:
								buffer = str("*", buffer)
							cfg.set_value(s, k, buffer)
					elif value is PackedStringArray:
						var new_packed : PackedStringArray = []
						var dirt : bool = false
						for v : String in value:
							var has_asteric : bool = false
							if v.begins_with("*"):
								has_asteric = true
								v = v.trim_prefix("*")
							if v.is_absolute_path():
								dirt = true
								v = obfuscate_path_callback.call(v)
								if has_asteric:
									v = str("*", v)
								new_packed.append(v)
							else:
								if has_asteric:
									v = str("*", v)
								new_packed.append(v)
						if dirt:
							cfg.set_value(s, k, new_packed)
			if cfg.has_section("editor_plugins"):
				cfg.erase_section("editor_plugins")
	return cfg

func _parse_config_settings(cfg : ConfigFile, force : bool) -> void:
	if cfg == null:
		push_error("Config null!! forcing: ", force)
		return
	for s in cfg.get_sections():
		for k in cfg.get_section_keys(s):
			var key : String = str(s,"/",k)
			if force or ProjectSettings.has_setting(key):
				ProjectSettings.set_setting(key, cfg.get_value(s, k))
	ProjectSettings.save()

func parse_project_settings(path : String = "") -> void:
	if !obfuscate_path_callback.is_valid():return
	if path.is_empty():
		path = ProjectSettings.globalize_path(str(SETTINGS_PATH))
	if !FileAccess.file_exists(path):
		push_warning("Cache file not exit!")
		return
	_project_settings_origin = ConfigFile.new()
	if _project_settings_origin.load(path) != OK:
		push_error("Can not open settings!")
		return
	var cfg : ConfigFile = ConfigFile.new()
	cfg.load(path)

	if cfg.get_sections().size() > 0:
		for s in cfg.get_sections():
			for k in cfg.get_section_keys(s):
				var value : Variant = cfg.get_value(s, k, null)
				if value is String:
					var buffer : String = value# value.is_absolute_path()
					var has_asteric : bool = false
					if buffer.begins_with("*"):
						has_asteric = true
						buffer = buffer.trim_prefix("*")
					if buffer.is_absolute_path():
						buffer = obfuscate_path_callback.call(buffer)
						if has_asteric:
							buffer = str("*", buffer)
						cfg.set_value(s, k, buffer)
				elif value is PackedStringArray or value is Array:
					if k == "config/features":
						#FIXME: Not working!
						for v in range(value.size()):
							var c : Variant = value[v]
							if c is String:
								if (c).is_valid_float():
									var nv : String = str(4 ,".",randi_range(2, 4))
									while c == nv:
										nv = str(4,".",randi_range(0, 4))
									value[v] = nv
									cfg.set_value(s, k, value)
									break
						continue
					var new_packed : PackedStringArray = []
					var dirt : bool = false
					for v : Variant in value:
						if v is String:
							var has_asteric : bool = false
							if v.begins_with("*"):
								has_asteric = true
								v = v.trim_prefix("*")
							if v.is_absolute_path():
								dirt = true
								v = obfuscate_path_callback.call(v)
								if has_asteric:
									v = str("*", v)
								new_packed.append(v)
							else:
								if has_asteric:
									v = str("*", v)
								new_packed.append(v)
					if dirt:
						cfg.set_value(s, k, new_packed)

		if cfg.has_section("editor_plugins"):
			cfg.erase_section("editor_plugins")
		if ProjectSettings.has_setting("editor_plugins/enabled"):
			_project_settings_origin.set_value("editor_plugins","enabled", ProjectSettings.get_setting("editor_plugins/enabled"))
			ProjectSettings.set_setting("editor_plugins/enabled", null)
		if ProjectSettings.has_setting("editor/version_control/plugin_name"):
			_project_settings_origin.set_value("editor","version_control/plugin_name", ProjectSettings.get_setting("editor/version_control/plugin_name"))
			ProjectSettings.set_setting("editor/version_control/plugin_name", null)
		#ProjectSettings.set_setting("application/config/features", PackedStringArray(["4.3", "Forward Plus"]))
		_parse_config_settings(cfg, false)

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
	reset()
	_parse_config_settings(_project_settings_origin, true)
	_project_settings_origin = null
	restore(UID_PATH)

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
		#INLINE SETTINGS
		if is_instance_valid(_project_settings_origin):
			for s in _project_settings_origin.get_sections():
				for k in _project_settings_origin.get_section_keys(s):
					var key : String = str(s,"/",k)
					ProjectSettings.set_setting(key, _project_settings_origin.get_value(s, k))
			ProjectSettings.save()
			_project_settings_origin = null
		#INLINE UID
		var temp : String = str(UID_PATH, ".tmp")
		if FileAccess.file_exists(temp):
			if FileAccess.file_exists(UID_PATH):
				DirAccess.remove_absolute(UID_PATH)
			DirAccess.rename_absolute(temp, UID_PATH)
