extends EditorExportPlugin

const _Settings := preload("settings.gd")
const _Logger := preload("logger.gd")
const ScriptObfuscator := preload("obfuscator/script/script_obfuscator.gd")
const ResourceObfuscator := preload("obfuscator/resource/resource_obfuscator.gd")
const SymbolTable := preload("obfuscator/symbol_table.gd")
const Tokenizer := preload("obfuscator/script/tokenizer/tokenizer.gd")
const Token := preload("obfuscator/script/tokenizer/token.gd")

const GodotCache := preload("dotgd/cache.gd")
const Lazy2Res := preload("dotgd/lazyres.gd")

var dot_cache : GodotCache = null
var lazy2res : Lazy2Res = null

const SOURCE_MAP_EXT : String = ".gd.map"

var settings : _Settings

var _enabled : bool
var _features : PackedStringArray
var _convert_text_resources_to_binary : bool
var _export_path : String
var _source_map_filename : String
var _scripts_last_modification : Dictionary
var _autoloads : Dictionary
var _symbols : SymbolTable
var _src_obfuscators : Dictionary
var _res_obfuscators : Dictionary
var _inject_autoload : String
var _exported_script_count : int
var _rgx_embedded : Array[RegEx] = []

var environment : String = "res://" #BASE FOLDER FILES TO SEARCH #TODO: put visible in GDMaim GUI x friendly workspace
var _addon_dir : String = "res://gdmaim" #Used in _export_file

func _get_name() -> String:
	return "gdmaim"

func _export_begin(features : PackedStringArray, is_debug : bool, path : String, flags : int) -> void:
	## Twister: Do not put unnecesary trash, again ;) (only work if cfg not overrided! gd < 4.3)
	var dir : String = _addon_dir
	var sc : GDScript = get_script()
	if sc:
		var nd : String = sc.resource_path.get_base_dir()
		if !nd.is_empty():
			dir = nd
			_addon_dir = nd
	if FileAccess.file_exists("res://export_presets.cfg"):
		var cfg : ConfigFile = ConfigFile.new()
		var dirty : bool = false
		cfg.load("res://export_presets.cfg")
		for section in cfg.get_sections():
			if cfg.has_section_key(section, "exclude_filter"):
				var _str : String = cfg.get_value(section, "exclude_filter", "")
				var _dir : String = str(dir, "/*")
				if _str.find(_dir) == -1:
					if !_str.is_empty():
						_str = str(_str,",", _dir)
					else:
						_str = _dir
					dirty = true
					cfg.set_value(section, "exclude_filter", _str)
		if dirty:
			cfg.save("res://export_presets.cfg")

	_features = features
	_export_path = path
	_source_map_filename = _export_path.get_file().get_basename() + Time.get_datetime_string_from_system().replace(":", ".") + ".gd.map"
	_exported_script_count = 0
	_enabled = !features.has("no_gdmaim")
	if !_enabled:
		return

	_convert_text_resources_to_binary = ProjectSettings.get_setting("editor/export/convert_text_resources_to_binary", false)
	if _convert_text_resources_to_binary:
		#push_warning("GDMaim: The project setting 'editor/export/convert_text_resources_to_binary' being enabled might significantly affect the time it takes to export")
		#_build_data_path(get_script().resource_path.get_base_dir() + "/cache")
		push_warning("GDMaim: The project setting 'editor/export/convert_text_resources_to_binary' is enabled, but will be ignored during export.")

	if settings.symbol_seed == 0 and !settings.symbol_dynamic_seed:
		push_warning("GDMaim - The ID generation seed is still set to the default value of 0. Please choose another one.")

	var scripts : PackedStringArray = _get_files("res://", ".gd")

	#_godot_data.clear()
	_autoloads.clear()
	_src_obfuscators.clear()
	_res_obfuscators.clear()
	_rgx_embedded.clear()

	_symbols = SymbolTable.new(settings)

	_inject_autoload = ""
	if settings.source_map_inject_name:
		var cfg : ConfigFile = ConfigFile.new()
		cfg.load("res://project.godot")
		for autoload : String in (cfg.get_section_keys("autoload") if cfg.has_section("autoload") else []):
			_autoloads[cfg.get_value("autoload", autoload).replace("*", "")] = autoload
			if !_inject_autoload and cfg.get_value("autoload", autoload).begins_with("*"):
				_inject_autoload = cfg.get_value("autoload", autoload).replace("*", "")
			_symbols.lock_symbol_name(autoload)
		if !_inject_autoload:
			push_warning("GDMaim - No valid autoload found! GDMaim will not be able to print the source map filename to the console on the exported build.")

	# Gather built-in variant and global symbols
	var builtins : Script = preload("builtins.gd")
	for global in builtins.GLOBALS:
		_symbols.lock_symbol_name(global)
	for variant in builtins.VARIANTS:
		if variant.has("class"):
			_symbols.lock_symbol_name(variant["class"])
		for signal_ in variant.get("signals", []):
			_symbols.lock_symbol_name(signal_)
		for constant_ in variant.get("constants", []):
			_symbols.lock_symbol_name(constant_)
		for var_ in variant.get("properties", []):
			_symbols.lock_symbol_name(var_)
		for func_ in variant.get("methods", []):
			_symbols.lock_symbol_name(func_)

	# Gather built-in class symbols
	for class_ in ClassDB.get_class_list():
		for symbol in _get_class_symbols(class_):
			_symbols.lock_symbol_name(symbol)

	# Parse scripts and gather their symbols
	if lazy2res == null:
		lazy2res = Lazy2Res.new()
	lazy2res.clear()
	for ext : String in [".gd", ".tscn", ".tres", ".scn", ".res"]:
		for script_path in _get_files(environment, ext):
			_parse_script(script_path)

	_symbols.resolve_symbol_paths()
	if settings.obfuscation_enabled:
		_symbols.obfuscate_symbols()

	# Modify class cache
	#var class_cache : ConfigFile = ConfigFile.new()
	#class_cache.load("res://.godot/global_script_class_cache.cfg")
	#var classes : Array[Dictionary] = class_cache.get_value("", "list")
	#for class_data : Dictionary in classes:
		#if _global_classes.has(class_data.class):
			#class_data.class = StringName(_global_classes[class_data.class])
		#if _global_classes.has(class_data.base):
			#class_data.base = StringName(_global_classes[class_data.base])
	#class_cache.set_value("", "list", classes)
	#_godot_data["res://.godot/global_script_class_cache.cfg"] = class_cache.encode_to_text().to_utf8_buffer()

	#CACHE PLAYDOH
	if dot_cache == null:
		dot_cache = GodotCache.new()
	dot_cache.reset()
	dot_cache.obfuscate_token_callback = _obfuscate_token_callback
	dot_cache.obfuscate_path_callback = _obfuscate_path_callback
	dot_cache.parse()

func _obfuscate_token_callback(token : StringName) -> StringName:
	#DIRTIER
	#return _symbols.get_symbol(token) #TODO X: Next push
	return token

func _obfuscate_path_callback(path : StringName) -> StringName:
	#TODO X: FUTURE USE
	return path

func _export_end() -> void:
	if !_enabled:
		return

	lazy2res.clear()
	lazy2res = null
	dot_cache.clear()
	dot_cache = null

	if _exported_script_count == 0:
		push_error('GDMaim - No scripts have been exported! Please set the export mode of scripts to "Text" in the current export template.')
		return

	_build_data_path(settings.source_map_path)
	var files : PackedStringArray
	for filepath in DirAccess.get_files_at(settings.source_map_path):
		if filepath.begins_with(_export_path.get_file().get_basename()) and filepath.length() == _source_map_filename.length():
			files.append(filepath)
	files.sort()
	files.reverse()
	for i in range(files.size() - 1, maxi(-1, settings.source_map_max_files - 2), -1):
		DirAccess.remove_absolute(settings.source_map_path + "/" + files[i])

	var source_map : Dictionary = {
		"version": "2.0",
		"symbols": { "source": {}, "export": {}, },
		"scripts": {},
		"resources": {},
	}
	for symbol : SymbolTable.Symbol in _symbols._global_symbols.values() + _symbols._local_symbols:
		source_map["symbols"]["source"][symbol.get_source_name()] = symbol.get_name()
		source_map["symbols"]["export"][symbol.get_name()] = symbol.get_source_name()
	for path in _src_obfuscators:
		var obfuscator : ScriptObfuscator = _src_obfuscators[path]
		var mappings : Array[Dictionary] = obfuscator.generate_line_mappings()
		var data : Dictionary = {
			"source_code": obfuscator.source_code,
			"export_code": obfuscator.generated_code,
			"source_mappings": mappings[0],
			"export_mappings": mappings[1],
			"log": _Logger.get_log(obfuscator),
		}
		source_map["scripts"][obfuscator.path] = data
	for path in _res_obfuscators:
		var obfuscator : ResourceObfuscator = _res_obfuscators[path]
		var data : Dictionary = {
			"source_code": obfuscator.get_source_data(),
			"export_code": obfuscator.get_data(),
			"log": _Logger.get_log(obfuscator),
		}
		source_map["resources"][obfuscator.path] = data
	var full_source_map_path : String = settings.source_map_path + "/" + _source_map_filename
	var file := FileAccess.open(full_source_map_path, FileAccess.WRITE)
	if file:
		var data : PackedByteArray = JSON.stringify(source_map, "\t").to_utf8_buffer()
		if settings.source_map_compress:
			file.store_8(FileAccess.COMPRESSION_GZIP)
			file.store_64(data.size())
			file.store_buffer(data.compress(FileAccess.COMPRESSION_GZIP))
		else:
			file.store_8(255)
			file.store_buffer(data)
		file.close()
		print("GDMaim - A source map has been saved to '" + full_source_map_path + "'")
	else:
		push_warning("GDMaim - Failed to write source map to '" + full_source_map_path + "'!")

	_autoloads.clear()
	_symbols = null
	_src_obfuscators.clear()
	_res_obfuscators.clear()
	_Logger.clear_all()


func _export_file(path : String, type : String, features : PackedStringArray) -> void:
	if path.begins_with(_addon_dir):
		#skip don't put the plugin version in the client safe and clear build.
		skip()
		return
	if !_enabled:
		return

	#HACK for some reason, this only works on the last (few?) exported files
	#TODO instead of doing it for every file, try to predict the amount of files which will be exported and only do this when the export is expected to end, with a margin of error
	#for data_path : String in _godot_data:
		#add_file(data_path, _godot_data[data_path], false)

	var data_file : PackedByteArray
	var ext : String = path.get_extension()
	if ext == "csv":
		skip() #HACK
	elif ext == "ico":
		skip() #HACK
		#add_file(path, FileAccess.get_file_as_bytes(path), true) #HACK
		data_file = FileAccess.get_file_as_bytes(path)
	elif ext == "scn":
		if _src_obfuscators.has(str(path,":",0)):
			_exported_script_count += 1
		var data : Array = lazy2res.get_data(path)
		var new_data : String = _obfuscate_resource(path,data[1])
		data_file = lazy2res.parse_to_binary(path, new_data)
	elif ext == "res":
		if _src_obfuscators.has(str(path,":",0)):
			_exported_script_count += 1
		var data : Array = lazy2res.get_data(path)
		var new_data : String = _obfuscate_resource(path,data[1])
		data_file = lazy2res.parse_to_binary(path, new_data)
	elif ext == "tres" or ext == "tscn":
		var data : String = ""
		if _src_obfuscators.has(str(path,":",0)):
			_exported_script_count += 1
			data = _obfuscate_resource(path, FileAccess.get_file_as_string(path))
		elif settings.obfuscate_export_vars or ext == "tscn":
			data = _obfuscate_resource(path, FileAccess.get_file_as_string(path))

			#add_file(path, data.to_utf8_buffer(), true)
			#var binary_data : PackedByteArray = _convert_text_to_binary_resource(ext, data) if _convert_text_resources_to_binary and path.contains("MapPractice") else PackedByteArray()
			#if !binary_data:
				#add_file(path, data.to_utf8_buffer(), true)
			#else:
				#var binary_path : String = "res://.godot/exported/gdmaim/" + _generate_uuid(path)
				#binary_path += "-" + path.get_file().replace(".tres", ".res").replace(".tscn", ".scn")
				#add_file(binary_path, binary_data, true)


			#Twister: If you want text2binary, can use lazyres like:

			#path = str(path.trim_suffix(str(".",path.get_extension())),".scn") ## or ".res"
			#_res_obfuscators[path].path = path ##Update path!

			# > with the remap enabled, auto point to new path, now end with:
			#data_file = lazy2res.parse_to_binary(path, new_data)

		data_file = data.to_utf8_buffer()
	elif ext == "gd":
		var code : String = _obfuscate_script(path)
		#add_file(path, code.to_utf8_buffer(), true)
		data_file = code.to_utf8_buffer()
		_exported_script_count += 1

	if data_file.size() > 0:
		add_file(path, data_file, true)


func _get_class_symbols(class_ : String) -> PackedStringArray:
	var symbols : PackedStringArray

	symbols.append(class_)

	for signal_ in ClassDB.class_get_signal_list(class_, true):
		symbols.append(signal_.name)

	for const_ in ClassDB.class_get_integer_constant_list(class_, true):
		symbols.append(const_)

	for enum_ in ClassDB.class_get_enum_list(class_, true):
		symbols.append(enum_)
		for key_ in ClassDB.class_get_enum_constants(class_, enum_, true):
			symbols.append(key_)

	for var_ in ClassDB.class_get_property_list(class_, true):
		const EXCLUDE_USAGES : PackedInt32Array = [64]
		if !EXCLUDE_USAGES.has(var_.usage):
			symbols.append(var_.name)

	for func_ in ClassDB.class_get_method_list(class_, true):
		symbols.append(func_.name)

	return symbols


func _parse_script(path : String) -> void:
	if path.ends_with(".gd"):
		var script : Script = load(path)
		var source_code : String = script.source_code
		if source_code.is_empty():return
		var obfuscator := ScriptObfuscator.new(path)
		_src_obfuscators[path] = obfuscator

		_Logger.swap(obfuscator)
		_Logger.clear()
		_Logger.write("Export log for '" + path + "'\n")
		_Logger.write("---------- " + " Parsing script " + path + " ----------")

		obfuscator.parse(source_code, _symbols, _symbols.create_global_symbol(_autoloads[path]) if _autoloads.has(path) else null)

		_Logger.write("\nAbstract Syntax Tree\n" + obfuscator._ast.print_tree(-1))

		_Logger.write("\n---------- " + " Resolving symbols " + path + " ----------\n")

	elif path.ends_with(".tscn") or path.ends_with(".tres"):
		var file : FileAccess = FileAccess.open(path, FileAccess.READ)
		var data : String = file.get_as_text()
		file.close()
		_parse_resource(path, data)
	elif path.ends_with(".res") or path.ends_with(".scn"):
		var new_data : Array = lazy2res.get_data(path)
		_parse_resource(path, new_data[1])

func _parse_resource(path : String, data : String) -> void:
	var source_codes : Array[String] = []
	if _rgx_embedded.size() == 0:
		var _rgx : RegEx = RegEx.new()
		var _rgx_sanitizer : RegEx = RegEx.new()
		var _rgx_rebuilder : RegEx = RegEx.new()
		_rgx.compile("script\\/source\\s*=\\s*\\\"([.|\\W|\\S]*?)[^\\\\]\\\"")
		_rgx_sanitizer.compile("([^\\\\])\\\\\\\"|^\\\\\\\"")
		_rgx_rebuilder.compile("([^\\\\])\\\"|^\\\"")
		_rgx_embedded = [_rgx, _rgx_sanitizer, _rgx_rebuilder]
	var ar_match : Array[RegExMatch] = _rgx_embedded[0].search_all(data)
	for x : int in range(ar_match.size()):
		var r_match : RegExMatch = ar_match[x]
		if null != r_match and r_match.strings.size() > 1:
			source_codes.append(_rgx_embedded[1].sub(r_match.strings[1], "$1\"", true))

	if source_codes.size() == 0:return
	for x : int in range(source_codes.size()):
		var source_code : String = source_codes[x]
		var embedded_path : String = str(path,":",x)
		var obfuscator := ScriptObfuscator.new(embedded_path)
		_src_obfuscators[embedded_path] = obfuscator

		_Logger.swap(obfuscator)
		_Logger.clear()
		_Logger.write("Export log for '" + embedded_path + "'\n")
		_Logger.write("---------- " + " Parsing script embedded " + embedded_path + " ----------")


		obfuscator.parse(source_code, _symbols, _symbols.create_global_symbol(_autoloads[embedded_path]) if _autoloads.has(embedded_path) else null)

		_Logger.write("\nAbstract Syntax Tree\n" + obfuscator._ast.print_tree(-1))

		_Logger.write("\n---------- " + " Resolving symbols " + embedded_path + " ----------\n")



func _obfuscate_script(path : String) -> String:
	var obfuscator : ScriptObfuscator = _src_obfuscators[path]

	_Logger.swap(obfuscator)
	_Logger.write("\n---------- " + " Obfuscating script " + path + " ----------")

	obfuscator.run(_features)

	# Inject startup code into the first autoload
	if path == _inject_autoload:
		var injection_code : String = 'print("GDMaim - Source map \'' + _source_map_filename + '\'\\n");'
		var did_inject : bool = false

		var found_func : bool = false
		for line in obfuscator.tokenizer.get_output_lines():
			for i in line.tokens.size():
				var token : Token = line.tokens[i]
				if token.get_value() == "_enter_tree":
					found_func = true
					break
				if found_func and token.type == Token.Type.IDENTATION:
					line.insert_token(i + 1, Token.new(Token.Type.LITERAL, injection_code, -1, -1))
					did_inject = true
					break
			if did_inject:
				break

		if !did_inject:
			obfuscator.tokenizer.insert_output_line(obfuscator.tokenizer.get_output_lines().size(), Tokenizer.Line.new([Token.new(Token.Type.LITERAL, 'func _enter_tree() -> void:\n\t' + injection_code, -1, -1)]))

	return obfuscator.generate_source_code()

func _obfuscate_resource(path : String, source_data : String) -> String:
	var obfuscator := ResourceObfuscator.new(path)
	_res_obfuscators[path] = obfuscator

	var codes : Array[String] = []
	var index : int = 0
	var embedded_path : String = str(path, ":", index)
	while _src_obfuscators.has(embedded_path):
		codes.append(_obfuscate_script(embedded_path))
		_src_obfuscators.erase(embedded_path) #Resource map consumed
		index += 1
		embedded_path = str(path, ":", index)


	_Logger.swap(obfuscator)
	_Logger.write("---------- " + " Obfuscating resource " + path + " ----------\n")

	obfuscator.run(source_data, _symbols)

	var data : String = obfuscator.get_data()
	if codes.size() > 0:
		for code : String in codes:
			data = _rgx_embedded[0].sub(data, str("[__SRC__] = \"",_rgx_embedded[2].sub(code, "$1\\\"", true),"\n\""), false)

	obfuscator.set_data(data.replace("[__SRC__]", "script/source"))
	return obfuscator.get_data()


func _multi_split(source : String, delimeters : String) -> PackedStringArray:
	var splits := PackedStringArray()

	var i : int = 0
	var last : int = 0
	while i < source.length():
		for d in delimeters:
			if source[i] == d:
				var split : String = source.substr(last, i - last)
				if split:
					splits.append(split)
				last = i + 1
				break
		i += 1

	if last < i:
		splits.append(source.substr(last, i - last))

	return splits


func _get_files(path : String, ext : String) -> PackedStringArray:
	var files : PackedStringArray
	var dirs : Array[String] = [path]
	while dirs:
		var dir : String = dirs.pop_front()
		for sub_dir in DirAccess.get_directories_at(dir):
			if !sub_dir.begins_with("."):
				dirs.append(dir.path_join(sub_dir))
		for file in DirAccess.get_files_at(dir):
			if file.replace(".remap", "").ends_with(ext):
				files.append(dir.path_join(file))
	files.sort()

	return files


func _write_file_str(path : String, text : String) -> bool:
	var file := FileAccess.open(path, FileAccess.WRITE)
	if file:
		file.store_string(text)
		file.close()
		return true
	return false


func _build_data_path(path : String) -> void:
	if !DirAccess.dir_exists_absolute(path):
		DirAccess.make_dir_recursive_absolute(path)
	_write_file_str(path + "/.gdignore", "")
	_write_file_str(get_script().resource_path.get_base_dir() + "/.gitignore", "cache/\nsource_maps/")


func _convert_text_to_binary_resource(extension : String, text_data : String) -> PackedByteArray:
	return PackedByteArray() # does NOT work right now, as obfuscated expors vars will not get serialized

	var path : String = get_script().resource_path.get_base_dir() + "/cache/convert."
	var binary_ext : String = "scn" if extension == "tscn" else "res"

	_write_file_str(path + extension, text_data)
	var resource : Resource = ResourceLoader.load(path + extension, "", ResourceLoader.CACHE_MODE_IGNORE)
	if !resource:
		return PackedByteArray()

	ResourceSaver.save(resource, path + binary_ext)

	return FileAccess.get_file_as_bytes(path + binary_ext)


func _generate_uuid(path : String) -> String:
	var bytes : PackedByteArray
	var idx : int = 0
	for i in 16: # I have no idea how well this actually works
		var byte : int = hash(idx) % 256
		for j in int(ceil(path.length() / 4)):
			byte = (byte + path.unicode_at(idx)) % 256
			idx = posmod(idx + 1, path.length())
		bytes.append(byte)
	bytes[6] = (bytes[6] & 0x0f) | 0x40
	bytes[8] = (bytes[8] & 0x3f) | 0x80
	return "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x" % (bytes as Array)
