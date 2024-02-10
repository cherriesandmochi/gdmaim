extends EditorExportPlugin


const ScriptData := preload("script_data.gd")
const SymbolTable := preload("symbol_table.gd")
const Parser := preload("parser.gd")

const SOURCE_MAP_EXT : String = ".gd.map"

var cfg : ConfigFile

var _features : PackedStringArray
var _enabled : bool
var _convert_text_resources_to_binary : bool
var _export_path : String
var _source_map_filename : String
var _scripts_last_modification : Dictionary
var _autoloads : Dictionary
var _built_in_symbols : Dictionary
var _global_symbols : SymbolTable
var _constants : Dictionary
var _scripts_data : Dictionary
var _current_script : ScriptData
var _classes : Dictionary
var _global_classes : Dictionary
var _enums : Dictionary
var _godot_data : Dictionary
var _inject_autoload : String
var _obfuscation_enabled : bool
var _inline_constants : bool
var _inline_enums : bool
var _obfuscate_export_vars : bool
var _obfuscate_signals : bool
var _id_prefix : String
var _id_characters : String
var _id_target_length : int
var _id_seed : int
var _dynamic_id_seed : bool
var _strip_comments : bool
var _strip_empty_lines : bool
var _feature_filters : bool
var _regex_filter_enabled : bool
var _regex_filter : String
var _source_map_path : String
var _source_map_max_files : int
var _source_map_compress : bool
var _source_map_inject_name : bool
var _debug_scripts : PackedStringArray
var _debug_resources : PackedStringArray
var _obfuscate_debug_only : bool


func _get_name() -> String:
	return "gdmaim"


func _export_begin(features : PackedStringArray, is_debug : bool, path : String, flags : int) -> void:
	_features = features
	_export_path = path
	_source_map_filename = _export_path.get_file().get_basename() + Time.get_datetime_string_from_system().replace(":", ".") + ".gd.map"
	_enabled = !features.has("no_gdmaim")
	if !_enabled:
		return
	
	_obfuscation_enabled = cfg.get_value("obfuscator", "enabled", false)
	_inline_constants = cfg.get_value("obfuscator", "inline_consts", false)
	_inline_enums = cfg.get_value("obfuscator", "inline_enums", false)
	_obfuscate_export_vars = cfg.get_value("obfuscator", "export_vars", false)
	_obfuscate_signals = cfg.get_value("obfuscator", "signals", false)
	_id_prefix = cfg.get_value("id", "prefix", "")
	_id_characters = cfg.get_value("id", "character_list", "")
	_id_target_length = cfg.get_value("id", "target_length", 0)
	_dynamic_id_seed = cfg.get_value("id", "dynamic_seed", false)
	_id_seed = cfg.get_value("id", "seed", 0) if !_dynamic_id_seed else int(Time.get_unix_time_from_system())
	_strip_comments = cfg.get_value("post_process", "strip_comments", false)
	_strip_empty_lines = cfg.get_value("post_process", "strip_empty_lines", false)
	_regex_filter_enabled = cfg.get_value("post_process", "regex_filter_enabled", false)
	_regex_filter = cfg.get_value("post_process", "regex_filter", "")
	_feature_filters = cfg.get_value("post_process", "feature_filters", false)
	_source_map_path = cfg.get_value("source_mapping", "filepath", "")
	_source_map_max_files = cfg.get_value("source_mapping", "max_files", 1)
	_source_map_compress = cfg.get_value("source_mapping", "compress", false)
	_source_map_inject_name = cfg.get_value("source_mapping", "inject_name", false)
	_debug_scripts = cfg.get_value("debug", "debug_scripts", "").split(",", false)
	_debug_resources = cfg.get_value("debug", "debug_resources", "").split(",", false)
	_obfuscate_debug_only = cfg.get_value("debug", "obfuscate_debug_only", false)
	
	_convert_text_resources_to_binary = ProjectSettings.get_setting("editor/export/convert_text_resources_to_binary", false)
	if _convert_text_resources_to_binary:
		#push_warning("GDMaim: The project setting 'editor/export/convert_text_resources_to_binary' being enabled might significantly affect the time it takes to export")
		#_build_data_path(get_script().resource_path.get_base_dir() + "/cache")
		push_warning("GDMaim: The project setting 'editor/export/convert_text_resources_to_binary' is enabled, but will be ignored during export.")
	
	if _id_seed == 0 and !_dynamic_id_seed:
		push_warning("GDMaim - The ID generation seed is still set to the default value of 0. Please choose another one.")
	
	_prepare_obfuscation()


func _export_end() -> void:
	if !_enabled:
		return
	
	_build_data_path(_source_map_path)
	var files : PackedStringArray
	for filepath in DirAccess.get_files_at(_source_map_path):
		if filepath.begins_with(_export_path.get_file().get_basename()) and filepath.length() == _source_map_filename.length():
			files.append(filepath)
	files.sort()
	files.reverse()
	for i in range(files.size() - 1, maxi(-1, _source_map_max_files - 2), -1):
		DirAccess.remove_absolute(_source_map_path + "/" + files[i])
	
	var source_map : Dictionary = {
		"version": "1.0",
		"symbols": { "source": {}, "export": {}, },
		"scripts": {},
	}
	for symbol in _global_symbols.symbols:
		source_map["symbols"]["source"][symbol] = _global_symbols.symbols[symbol].name
		source_map["symbols"]["export"][_global_symbols.symbols[symbol].name] = symbol
	for script_data in _scripts_data.values():
		var mappings : Array[Dictionary] = script_data.generate_mappings()
		var data : Dictionary = {
			"source_code": script_data.source_code,
			"export_code": script_data.export_code,
			"source_mappings": mappings[0],
			"export_mappings": mappings[1],
			"log": script_data.debug_log,
		}
		source_map["scripts"][script_data.path] = data
	var full_source_map_path : String = _source_map_path + "/" + _source_map_filename
	var file := FileAccess.open(full_source_map_path, FileAccess.WRITE)
	if file:
		var data : PackedByteArray = JSON.stringify(source_map, "\t").to_utf8_buffer()
		if _source_map_compress:
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


func _export_file(path : String, type : String, features : PackedStringArray) -> void:
	if !_enabled:
		return
	
	#HACK for some reason, this only works on the last (few?) exported files
	#TODO instead of doing it for every file, try to predict the amount of files which will be exported and only do this when the export is expected to end, with a margin of error
	#for data_path : String in _godot_data:
		#add_file(data_path, _godot_data[data_path], false)
	
	var ext : String = path.get_extension()
	if ext == "csv":
		skip() #HACK
	elif ext == "ico":
		skip() #HACK
		add_file(path, FileAccess.get_file_as_bytes(path), true) #HACK
	elif ext == "tres" or ext == "tscn":
		if _obfuscate_export_vars or ext == "tscn":
			var data : String = _obfuscate_resource(path, FileAccess.get_file_as_string(path))
			add_file(path, data.to_utf8_buffer(), true)
			#var binary_data : PackedByteArray = _convert_text_to_binary_resource(ext, data) if _convert_text_resources_to_binary and path.contains("MapPractice") else PackedByteArray()
			#if !binary_data:
				#add_file(path, data.to_utf8_buffer(), true)
			#else:
				#var binary_path : String = "res://.godot/exported/gdmaim/" + _generate_uuid(path)
				#binary_path += "-" + path.get_file().replace(".tres", ".res").replace(".tscn", ".scn")
				#add_file(binary_path, binary_data, true)
	elif ext == "gd":
		var code : String = _obfuscate_script(path)
		code = _shuffle_top_level(code, path)
		add_file(path, code.to_utf8_buffer(), true)


func _prepare_obfuscation() -> void:
	var scripts : PackedStringArray = _get_files("res://", ".gd")
	
	_godot_data.clear()
	_autoloads.clear()
	_built_in_symbols.clear()
	
	_inject_autoload = ""
	if _source_map_inject_name:
		var cfg : ConfigFile = ConfigFile.new()
		cfg.load("res://project.godot")
		for autoload : String in (cfg.get_section_keys("autoload") if cfg.has_section("autoload") else []):
			_autoloads[cfg.get_value("autoload", autoload).replace("*", "")] = autoload
			if !_inject_autoload and cfg.get_value("autoload", autoload).begins_with("*"):
				_inject_autoload = cfg.get_value("autoload", autoload).replace("*", "")
		if !_inject_autoload:
			push_warning("GDMaim - No valid autoload found! GDMaim will not be able to print the source map filename to the console on the exported build.")
	
	# Gather built-in variant and global symbols
	var builtins : Script = preload("builtins.gd")
	for global in builtins.GLOBALS:
		_built_in_symbols[global] = global
	for variant in builtins.VARIANTS:
		if variant.has("class"):
			_built_in_symbols[variant["class"]] = variant["class"]
		for signal_ in variant.get("signals", []):
			_built_in_symbols[signal_] = signal_
		for var_ in variant.get("properties", []):
			_built_in_symbols[var_] = var_
		for func_ in variant.get("methods", []):
			_built_in_symbols[func_] = func_
	
	# Gather built-in class symbols
	for class_ in ClassDB.get_class_list():
		for symbol in _get_class_symbols(class_):
			_built_in_symbols[symbol] = symbol
	
	# Gather symbols
	_global_symbols = SymbolTable.new(hash(str(_id_seed)), true, _built_in_symbols)
	_global_symbols.exporter = self
	_scripts_data.clear()
	_constants.clear()
	_classes.clear()
	_enums.clear()
	_global_classes.clear()
	for path in scripts:
		_parse_script(path)
	
	# Fix global symbols cache
	for symbol in _global_symbols.symbols:
		var splits1 : PackedStringArray = symbol.split(".")
		var splits2 : PackedStringArray = _global_symbols.symbols[symbol].name.split(".")
		var new_name : String
		for i in splits1.size():
			if _built_in_symbols.has(splits1[i]):
				new_name += ("." if new_name else "") + _built_in_symbols[splits1[i]]
			elif splits1.size() == splits2.size():
				new_name += ("." if new_name else "") + splits2[i]
			else:
				new_name = _global_symbols.symbols[symbol].name
				break
		_global_symbols.symbols[symbol].name = new_name
	
	# Evaluate constants
	var constant_values : Dictionary
	for constant in _constants:
		_evaluate_constant(constant, _constants, constant_values)
	
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


func _evaluate_constant(constant : String, constants : Dictionary, results : Dictionary):
	if results.has(constant):
		return results[constant]
	
	var symbol : SymbolTable.Symbol = constants[constant]
	var const_script : ScriptData = symbol.get_meta("const_script")
	var class_path : String = _get_scope_class(constant)
	var expr_text : String = symbol.get_meta("value", "")
	var tokens : PackedStringArray = _multi_split(expr_text, "+-*/%(),")
	var input_names : PackedStringArray
	var inputs : Array
	
	_current_script = const_script
	
	if expr_text.contains(constant):
		results[constant] = expr_text
		return results[constant]
	
	var result
	var enum_or_class : bool = false
	if _classes.has(expr_text):
		result = _classes[expr_text].name
		enum_or_class = true
	elif _enums.has(expr_text):
		var enum_ : Dictionary = _enums[expr_text]
		var enum_symbol : SymbolTable.Symbol = enum_.get("symbol")
		result = expr_text
		enum_or_class = true
		if symbol.has_meta("local_path"):
			for key in enum_:
				if key == "symbol":
					continue
				var key_path : String = constant.trim_prefix(symbol.get_meta("scope_path") + ".").trim_suffix("." + symbol.get_meta("scope_id"))
				key_path += "." + key
				const_script.add_local_symbol(key_path, symbol.get_meta("scope_path"), symbol.get_meta("scope_id"), "", str(enum_[key]))
		else:
			for key in enum_:
				if key == "symbol":
					continue
				var key_path : String = (constant + "." + key).trim_prefix(class_path + ".")
				const_script.add_member_symbol(key_path, SymbolTable.Symbol.new(key_path, str(enum_[key])))
	else:
		for token in tokens:
			var token_raw : String = token
			if token and !token.contains(".") and !"01234567890".contains(token[0]):
				token = class_path + "." + token
			var local_constant : SymbolTable.Symbol = const_script.get_local_symbol_to(token_raw, symbol) if const_script else null
			if local_constant:
				token = local_constant.get_meta("local_path")
			if constants.has(token) and token != constant:
				input_names.append(token_raw)
				inputs.append(_evaluate_constant(token, constants, results))
		
		var expr := Expression.new()
		if expr.parse(expr_text, input_names) == OK:
			result = expr.execute(inputs, null, false, true)
		if expr.has_execute_failed():
			_script_log("ERROR while evaluating constant '" + constant + "'(>" + expr_text + "<): " + expr.get_error_text())
			results[constant] = constant
			return expr_text
	
	symbol.set_meta("is_inlined", true)
	
	match symbol.type:
		"bool": symbol.name = str(bool(result))
		"Color": symbol.name = "Color" + str(Color(result))
		"float": symbol.name = str(result) if str(result).contains(".") else str(result) + ".0"
		"int": symbol.name = str(int(result))
		"NodePath": symbol.name = 'NodePath("' + str(NodePath(result)) + '")'
		"Vector2": symbol.name = "Vector2" + str(Vector2(result))
		"Vector2i": symbol.name = "Vector2i" + str(Vector2i(result))
		"Vector3": symbol.name = "Vector3" + str(Vector3(result))
		"Vector3i": symbol.name = "Vector3i" + str(Vector3i(result))
		"Vector4": symbol.name = "Vector4" + str(Vector4(result))
		"Vector4i": symbol.name = "Vector4i" + str(Vector4i(result))
		_: 
			if enum_or_class: symbol.name = str(result)
			else: symbol.set_meta("is_inlined", false)
	
	if symbol.get_meta("is_inlined", false) and symbol.has_meta("member"):
		symbol.get_meta("member").name = symbol.name
	
	_script_log("evaluated constant '" + constant + "' = " + expr_text + " = " + symbol.name)
	
	results[constant] = result
	
	return result


func _get_class_symbols(class_ : String) -> PackedStringArray:
	var symbols : PackedStringArray
	
	symbols.append(class_)
	
	for signal_ in ClassDB.class_get_signal_list(class_, true):
		symbols.append(signal_.name)
	
	for const_ in ClassDB.class_get_integer_constant_list(class_, true):
		symbols.append(const_)
	
	#for enum_ in ClassDB.class_get_enum_list(class_, true):
		#symbols.append(enum_)
	
	for var_ in ClassDB.class_get_property_list(class_, true):
		const EXCLUDE_USAGES : PackedInt32Array = [64]
		if !EXCLUDE_USAGES.has(var_.usage):
			symbols.append(var_.name)
	
	for func_ in ClassDB.class_get_method_list(class_, true):
		symbols.append(func_.name)
	
	return symbols


func _parse_script(path : String) -> void:
	var script : Script = load(path)
	var source_code : String = script.source_code
	
	_global_symbols.obfuscation_enabled = true
	
	var is_autoload : bool = _autoloads.has(path)
	var scope_path : String = _autoloads.get(path, path.get_file().replace(".", "_"))
	var scope_path_root : String = scope_path + "."
	var scope_path_identations : Array[int]
	var prev_identation : int = 0
	var identation_locked : bool = false
	var scope_tree : Array
	var cur_scope_tree_branch : Array = scope_tree
	var cur_scope_tree_path : Array[int]
	var scope_id : String = ""
	var local_scope_stack : Array[int]
	var in_class : bool = false
	var lambda_idx : int = 0
	var lines : PackedStringArray = source_code.split("\n")
	
	var script_data : ScriptData = ScriptData.new(hash(path + str(_id_seed)), _built_in_symbols)
	script_data.exporter = self
	script_data.local_symbols.exporter = self
	script_data.path = path
	script_data.base_script = script.get_base_script().resource_path if script.get_base_script() else ""
	script_data.name = scope_path
	script_data.autoload = is_autoload
	script_data.source_code = source_code
	_scripts_data[path] = script_data
	
	_current_script = script_data
	_script_log("Export log for '" + path + "'\n")
	_script_log("---------- " + " Parsing script " + path + " ----------")
	
	const ExpressionType : Dictionary = {
		NONE = 0,
		VAR = 1,
		FUNC = 2,
		CLASS = 3,
		CLASS_NAME = 4,
		SIGNAL = 5,
		ENUM = 6,
		ENUM_VALUES = 7,
		ENUM_VALUE_ASSIGNMENT = 8,
		CONST = 9,
		PARAMS = 10,
		PARAMS_HINT = 11,
		STRING = 12,
		FOR = 13,
	}
	const ExpressionIdentifiers : Dictionary = {
		"var": ExpressionType.VAR,
		"func": ExpressionType.FUNC,
		"class": ExpressionType.CLASS,
		"class_name": ExpressionType.CLASS_NAME,
		"signal": ExpressionType.SIGNAL,
		"enum": ExpressionType.ENUM,
		"const": ExpressionType.CONST,
		"'": ExpressionType.STRING,
		"\"": ExpressionType.STRING,
		"for": ExpressionType.FOR,
	}
	var expr_type : int = ExpressionType.NONE
	var string_end : String
	var parentheses : int = 0
	var enum_path : String
	var enum_path_local : String
	var cur_emum_idx : int
	var param_idx : int = -1
	var string_param_names : PackedStringArray
	var string_params : Dictionary
	var skipped_lines : Dictionary
	
	for line in lines:
		var parser := Parser.new(line)
		var identation : int = parser.get_identation()
		var tokens : PackedStringArray = parser.get_tokens()
		var tokens_tween : PackedStringArray = parser.tweens
		var line_idx : int = script_data.get_line_count()
		var declarations : Dictionary
		
		if line.begins_with("##OBFUSCATE ") and tokens.size() >= 4:
			if tokens[3] == "true":
				_global_symbols.obfuscation_enabled = true
			elif tokens[3] == "false":
				_global_symbols.obfuscation_enabled = false
			script_data.local_symbols.obfuscation_enabled = _global_symbols.obfuscation_enabled
			_script_log(str(line_idx+1) + " ##OBFUSCATE " + str(_global_symbols.obfuscation_enabled))
		elif line.begins_with("##OBFUSCATE_STRING_PARAMETERS"):
			string_param_names = line.trim_prefix("##OBFUSCATE_STRING_PARAMETERS").replace(" ", "").split(",", false)
			_script_log(str(line_idx+1) + " ##OBFUSCATE_STRING_PARAMETERS " + str(string_param_names))
		
		if !identation_locked and tokens and tokens[0] != "#":
			var lower_scope : bool = false
			for i in range(scope_path_identations.size() -1, -1, -1):
				if identation <= scope_path_identations[i]:
					scope_path_identations.remove_at(i)
					lower_scope = true
			if lower_scope:
				scope_path = _set_scope_path_level(scope_path, scope_path_identations.size() + 1)
				for i in range(local_scope_stack.size() - 1, -1, -1):
					if identation <= local_scope_stack[i]:
						local_scope_stack.remove_at(i)
			
			if prev_identation != identation:
				in_class = in_class and identation > 0
				if identation > prev_identation:
					for x in identation - prev_identation:
						cur_scope_tree_path.append(cur_scope_tree_branch.size())
						cur_scope_tree_branch.append([])
						cur_scope_tree_branch = cur_scope_tree_branch.back()
				else:
					for x in prev_identation - identation:
						cur_scope_tree_path.pop_back()
				
				scope_id = ""
				cur_scope_tree_branch = scope_tree
				for i in cur_scope_tree_path.size():
					cur_scope_tree_branch = cur_scope_tree_branch[cur_scope_tree_path[i]]
					scope_id += str(cur_scope_tree_path[i]) + ("-" if i+1 < cur_scope_tree_path.size() else "")
				
				_script_log(str(line_idx+1) + " SCOPE ID " + scope_id)
				
				prev_identation = identation
		
		var i : int = 0
		while i < tokens.size():
			var token : String = tokens[i]
			if token.begins_with("#") and !string_end:
				break
			
			match expr_type:
				ExpressionType.NONE:
					if ExpressionIdentifiers.has(token):
						expr_type = ExpressionIdentifiers[token]
						if expr_type == ExpressionType.STRING:
							string_end = token
				
				ExpressionType.STRING:
					if token == "\\":
						i += 1
					elif token == string_end:
						expr_type = ExpressionType.NONE
						string_end = ""
				
				ExpressionType.VAR:
					if line.begins_with("@export") and !_obfuscate_export_vars:
						_global_symbols.exclude_symbol(token)
						_script_log(str(line_idx+1) + " skipping export var " + token) 
					else:
						var type : String = _get_var_type(i, tokens)
						if local_scope_stack:
							script_data.add_local_symbol(token, scope_path, scope_id, type)
						else:
							var new_symbol : SymbolTable.Symbol = _global_symbols.add_symbol(token, "", "")
							declarations[i] = new_symbol
							if new_symbol.name and (is_autoload or tokens[0] == "static"):
								declarations[i] = _global_symbols.add_symbol(scope_path + "." + token, scope_path + "." + new_symbol.name, type)
							if !in_class:
								declarations[i] = script_data.add_member_symbol(token, SymbolTable.Symbol.new(token, new_symbol.name if new_symbol.name else token, type))
						if !local_scope_stack:
							_script_log(str(line_idx+1) + " var " + scope_path + "." + token + " : " + type)
						else:
							_script_log(str(line_idx+1) + " local var " + scope_path + "." + token + "." + scope_id + " : " + type)
					expr_type = ExpressionType.NONE
				
				ExpressionType.FUNC:
					if !token.begins_with("("):
						var new_symbol : SymbolTable.Symbol = _global_symbols.add_symbol(token)
						declarations[i] = new_symbol
						if new_symbol.name and (is_autoload or tokens[0] == "static"):
							declarations[i] = _global_symbols.add_symbol(scope_path + "." + token, scope_path + "." + new_symbol.name)
							declarations[i].string_params = new_symbol.string_params
						if !local_scope_stack and !in_class:
							declarations[i] = script_data.add_member_symbol(token, new_symbol if new_symbol else SymbolTable.Symbol.new(token, token))
						string_params = new_symbol.string_params
						scope_path += "." + token
					else:
						scope_path += "." + "@lambda" + str(lambda_idx)
						lambda_idx += 1
					
					scope_path_identations.append(identation)
					local_scope_stack.append(identation)
					
					_script_log(str(line_idx+1) + " func " + scope_path)
					
					expr_type = ExpressionType.PARAMS
					identation_locked = true
				
				ExpressionType.CLASS:
					var new_symbol : SymbolTable.Symbol = _global_symbols.add_symbol(token)
					declarations[i] = _global_symbols.add_symbol(scope_path + "." + token, scope_path + "." + new_symbol.name)
					if !local_scope_stack and !in_class:
						script_data.add_member_symbol(token, new_symbol if new_symbol else SymbolTable.Symbol.new(token, token))
					
					scope_path += "." + token
					scope_path_identations.append(identation)
					in_class = true
					_classes[scope_path] = declarations[i]
					
					_script_log(str(line_idx+1) + " class " + scope_path)
					
					expr_type = ExpressionType.NONE
				
				ExpressionType.CLASS_NAME:
					#var new_symbol : SymbolTable.Symbol = _global_symbols.add_symbol(token, "", "")
					#_global_classes[token] = new_symbol.name
					
					script_data.name = token
					scope_path = token
					scope_path_root = token + "."
					expr_type = ExpressionType.NONE
					
					_script_log(str(line_idx+1) + " global class " + token)
				
				ExpressionType.SIGNAL:
					if !_obfuscate_signals:
						expr_type = ExpressionType.NONE
					else:
						var new_symbol : SymbolTable.Symbol = _global_symbols.add_symbol(token)
						declarations[i] = new_symbol
						if new_symbol.name and is_autoload:
							declarations[i] = _global_symbols.add_symbol(scope_path + "." + token, scope_path + "." + new_symbol.name)
						if !local_scope_stack and !in_class:
							script_data.add_member_symbol(token, new_symbol if new_symbol else SymbolTable.Symbol.new(token, token))
						
						string_params = new_symbol.string_params
						
						scope_path += "." + token
						scope_path_identations.append(identation)
						local_scope_stack.append(identation)
						
						_script_log(str(line_idx+1) + " signal " + scope_path)
						
						expr_type = ExpressionType.NONE
						for j in range(i + 1, tokens.size()):
							if tokens[j] == "#":
								break
							elif tokens[j] == "(":
								expr_type = ExpressionType.PARAMS
								identation_locked = true
								break
				
				ExpressionType.ENUM:
					var new_symbol : SymbolTable.Symbol = _global_symbols.add_symbol(token)
					var global_enum : SymbolTable.Symbol = _global_symbols.add_symbol(scope_path + "." + token, scope_path + "." + new_symbol.name if !_inline_enums or !_global_symbols.obfuscation_enabled else "int")
					declarations[i] = global_enum
					if !local_scope_stack and !in_class:
						script_data.add_member_symbol(token, new_symbol if !_inline_enums or !_global_symbols.obfuscation_enabled else SymbolTable.Symbol.new(token, "int"))
					
					cur_emum_idx = -1
					enum_path = scope_path + "." + (new_symbol.name if new_symbol else token)
					var j : int = scope_path.find(".")
					if j != -1:
						enum_path_local = scope_path.substr(j + 1) + "." + new_symbol.name
					else:
						enum_path_local = new_symbol.name
					
					scope_path += "." + token
					scope_path_identations.append(identation)
					local_scope_stack.append(identation)
					
					if _global_symbols.obfuscation_enabled and _inline_enums:
						_enums[scope_path] = { "symbol": global_enum }
					
					_script_log(str(line_idx+1) + " enum " + scope_path + " -> " + enum_path)
					
					expr_type = ExpressionType.ENUM_VALUES
				
				ExpressionType.ENUM_VALUES:
					if token == "}":
						expr_type = ExpressionType.NONE
					elif token == "=":
						expr_type = ExpressionType.ENUM_VALUE_ASSIGNMENT
					elif token != "{" and token != ",":
						cur_emum_idx += 1
						if i + 2 < tokens.size() and tokens[i + 1] == "=":
							cur_emum_idx = int(tokens[i + 2])
						var name : String = _global_symbols.add_symbol(token).name
						script_data.add_local_symbol(token, scope_path, scope_id, "", str(cur_emum_idx) if _inline_enums and _global_symbols.obfuscation_enabled else name)
						_enums.get(scope_path, {})[token] = cur_emum_idx
						_global_symbols.add_symbol(token)
						_global_symbols.add_symbol(scope_path + "." + token, str(cur_emum_idx) if _inline_enums and _global_symbols.obfuscation_enabled and _global_symbols.obfuscation_enabled else enum_path + "." + name)
						_global_symbols.add_symbol(_get_scope_path_tail(scope_path) + "." + token, str(cur_emum_idx) if _inline_enums and _global_symbols.obfuscation_enabled else _get_scope_path_tail(enum_path) + "." + name)
						if !in_class:
							script_data.add_member_symbol(
								scope_path.trim_prefix(scope_path_root) + "." + token,
								SymbolTable.Symbol.new(enum_path.trim_prefix(scope_path_root) + "." + name, str(cur_emum_idx) if _inline_enums else enum_path.trim_prefix(scope_path_root) + "." + name))
						_script_log(str(line_idx+1) + " enum value " + scope_path + "." + token + " -> " + enum_path + "." + name)
				
				ExpressionType.ENUM_VALUE_ASSIGNMENT:
					if token == "}":
						expr_type = ExpressionType.NONE
					elif token == ",":
						expr_type = ExpressionType.ENUM_VALUES
				
				ExpressionType.CONST:
					var type : String = _get_var_type(i, tokens)
					
					var assignment : Dictionary = Parser.read_assignment(lines, line_idx, i + 1) if _inline_constants else {}
					var value : String = assignment.get("value", "")
					if local_scope_stack:
						var local_constant : SymbolTable.Symbol = script_data.add_local_symbol(token, scope_path, scope_id, type)
						local_constant.set_meta("value", value)
						local_constant.set_meta("declaration_to", assignment.get("to", []))
						local_constant.set_meta("const_script", script_data)
						declarations[i] = local_constant
						_constants[local_constant.get_meta("local_path")] = local_constant
						_script_log(str(line_idx+1) + " local const " + scope_path + "." + token + "." + scope_id + " : " + type + " = " + value)
					else:
						var new_symbol : SymbolTable.Symbol = _global_symbols.add_symbol(token, "", "")
						var constant : SymbolTable.Symbol = _global_symbols.add_symbol(scope_path + "." + token, scope_path + "." + new_symbol.name, type)
						constant.set_meta("value", value)
						constant.set_meta("declaration_to", assignment.get("to", []))
						constant.set_meta("const_script", script_data)
						declarations[i] = constant
						_constants[scope_path + "." + token] = constant
						if !in_class:
							constant.set_meta("member",
								script_data.add_member_symbol(token, SymbolTable.Symbol.new(token, new_symbol.name, type)))
								#script_data.add_member_symbol(token, SymbolTable.Symbol.new(token, value if value else (new_symbol.name if new_symbol else token), type)))
						_script_log(str(line_idx+1) + " const " + scope_path + "." + token + " : " + type + " = " + value)
					
					expr_type = ExpressionType.NONE
				
				ExpressionType.PARAMS:
					if token == ")":
						param_idx = -1
						string_param_names = PackedStringArray()
						expr_type = ExpressionType.NONE
						identation_locked = false
					elif token == ":" or token == "=":
						expr_type = ExpressionType.PARAMS_HINT
						parentheses = 0
					elif token != "(" and token != "," and token != "\\":
						var type : String = _get_var_type(i, tokens)
						var name : String = script_data.add_local_symbol(token, scope_path, "", type).name
						param_idx += 1
						if string_param_names.has(token):
							string_params[param_idx] = token
							_script_log(str(line_idx+1) + " binding string param " + token + " " + str(param_idx))
						_script_log(str(line_idx+1) + " param " + scope_path + "." + token + " : " + type)
				
				ExpressionType.PARAMS_HINT:
					if token == ")":
						parentheses -= 1
						if parentheses < 0:
							param_idx = -1
							string_param_names = PackedStringArray()
							expr_type = ExpressionType.NONE
							identation_locked = false
					elif token == "(":
						parentheses += 1
					elif token == "," and parentheses <= 0:
						expr_type = ExpressionType.PARAMS
				
				ExpressionType.FOR:
					scope_id += "-" + str(cur_scope_tree_branch.size())
					var type : String = _get_var_type(i, tokens)
					script_data.add_local_symbol(token, scope_path, scope_id, type)
					_script_log(str(line_idx+1) + " local var " + scope_path + "." + token + "." + scope_id + " : " + type)
					expr_type = ExpressionType.NONE
				
				_:
					expr_type = ExpressionType.NONE
			
			i += 1
		
		var line_data : ScriptData.Line = script_data.new_line()
		line_data.idx = line_idx
		line_data.skip = skipped_lines.get(line_idx, false)
		line_data.source_text = line
		line_data.text = line
		line_data.tokens = tokens
		line_data.tokens_tween = tokens_tween
		line_data.declarations = declarations
		line_data.scope_path = scope_path
		line_data.scope_id = scope_id
		line_data.identation = identation
		line_data.local_scope = !local_scope_stack.is_empty()
		line_data.in_class = in_class


func _is_statement_end(stmt: String) -> bool:
	var round_brackets: Array = []
	var square_brackets: Array = []
	var curly_brackets: Array = []

	for char in stmt:
		match char:
			"(":
				round_brackets.append(char)
			")":
				if round_brackets.size() == 0 or round_brackets.pop_back() != "(":
					return false
			"[":
				square_brackets.append(char)
			"]":
				if square_brackets.size() == 0 or square_brackets.pop_back() != "[":
					return false
			"{":
				curly_brackets.append(char)
			"}":
				if curly_brackets.size() == 0 or curly_brackets.pop_back() != "{":
					return false

	return round_brackets.size() == 0 and square_brackets.size() == 0 and curly_brackets.size() == 0


func _shuffle_top_level(code: String, path: String) -> String:
	# shuffles all top-level statements
	var script_data : String = code
	var statements: Array[String] = []
	var current: String = ""
	var idx: int = 0
	var length: int = script_data.length()
	var result: String = ""
	for i in script_data:
		if i != "\n":
			current += i
		elif current.begins_with("class_name ") or current.begins_with("@tool") or current.begins_with("extends ") or current.begins_with("@icon"):
			result += current + "\n"
			current = ""
		elif (current.begins_with("static var ") or current.begins_with("signal ") or current.begins_with("var ") or current.begins_with("const ")) and _is_statement_end(current):
			statements.push_back(current)
			current = ""
		elif (current.begins_with("@export") or current.begins_with("@onready")) and _is_statement_end(current):
			statements.push_back(current)
			current = ""
		elif (current.begins_with("static func ") or current.begins_with("func ") or current.begins_with("class ")) and idx < length-1 and script_data[idx+1] != "	":
			statements.push_back(current)
			current = ""
		else:
			current += "\n"
		idx += 1
	statements.shuffle()
	statements.push_back(current)
	for stmt in statements:
		result += stmt + "\n"
	#result += "\n# " + path + " :: " + str(statements)
	return result



func _obfuscate_script(path : String) -> String:
	#if _is_debug_script(path):
		#print("\n---------- ", "OBFUSCATE SCRIPT ", path, " ----------\n")
	#elif _obfuscate_debug_only:
		#return _scripts_data[path].source_code
	
	# Insert obfuscated symbols
	var obfuscate_declarations : bool = true
	var script_data : ScriptData = _scripts_data[path]
	_current_script = script_data
	_script_log("---------- " + " Obfuscating script " + path + " ----------")
	var line_mapper : ScriptData.LineMapper = script_data.line_mapper
	script_data.reload(_scripts_data)
	var line_data : ScriptData.Line = script_data.get_next_line()
	while line_data:
		var line_code : String
		
		if !_obfuscation_enabled:
			line_mapper.add_linked_line(line_data, line_data.text)
			line_data = script_data.get_next_line()
			continue
		elif line_data.text.begins_with("##OBFUSCATE") and line_data.tokens.size() >= 4:
			if line_data.tokens[3] == "true":
				obfuscate_declarations = true
			elif line_data.tokens[3] == "false":
				obfuscate_declarations = false
			line_mapper.add_linked_line(line_data, line_data.text)
			line_data = script_data.get_next_line()
			_script_log(str(script_data._idx-1) + " ##OBFUSCATE " + str(obfuscate_declarations))
			continue
		
		var i : int = 0
		while i < line_data.tokens.size():
			var token : String = line_data.tokens[i]
			var original_token : String = token
			var new_symbol : SymbolTable.Symbol
			
			# Skip marked lines
			if line_data.skip:
				break
			
			# Skip constant declaration, if inlined
			if _inline_constants and token == "const" and line_data.declarations.has(i + 1):
				var declaration : SymbolTable.Symbol = line_data.declarations[i + 1]
				if declaration.get_meta("is_inlined", false):
					var declaration_to : Array = declaration.get_meta("declaration_to", [0, 0])
					_script_log(str(script_data._idx+1) + " removed inlined constant's declaration " + str([line_data.idx, i]) + "-" + str(declaration_to))
					for l in range(line_data.idx, declaration_to[0]):
						line_mapper.add_linked_line(line_data, line_code)
						line_data = script_data.get_next_line()
						line_code = ""
						i = 0
					while i < declaration_to[1]:
						i += 1
					continue
			
			# Skip entire enum declaration if enum inlining is enabled
			if _inline_enums and token == "enum" and obfuscate_declarations:
				while line_data and !line_data.text.contains("}"):
					line_mapper.add_linked_line(line_data, "")
					line_data = script_data.get_next_line()
				line_mapper.add_linked_line(line_data, "")
				line_data = script_data.get_next_line() # A bit sloppy, but it's okay
				i = 0
				continue
			
			line_code += line_data.tokens_tween[i]
			
			# Skip comments
			if token == "#":
				line_code += token
				i += 1
				while i < line_data.tokens.size():
					line_code += line_data.tokens_tween[i]
					line_code += line_data.tokens[i]
					i += 1
				break
			
			# Skip strings
			if original_token == "'" or original_token == '"':
				var str : String
				var str_end : String = original_token
				line_code += token
				i += 1
				while i < line_data.tokens.size():
					str += line_data.tokens_tween[i] + line_data.tokens[i]
					if line_data.tokens[i] == "\\":
						i += 1
						if i < line_data.tokens.size():
							str += line_data.tokens_tween[i] + line_data.tokens[i]
					elif line_data.tokens[i] == str_end:
						break
					i += 1
				i += 1
				
				if line_data.text.ends_with("##OBFUSCATE_STRINGS") and str.length() >= 2:
					new_symbol = _global_symbols.get_symbol(str.trim_suffix(str_end))
					if new_symbol:
						line_code += new_symbol.name + str_end
						_script_log(str(script_data._idx+1) + " found string symbol >" + str.trim_suffix(str_end) + "< = " + new_symbol.name)
						continue
					else:
						var created_symbol : SymbolTable.Symbol = _global_symbols.add_symbol(str.trim_suffix(str_end))
						if created_symbol:
							line_code += created_symbol.name + str_end
							_script_log(str(script_data._idx+1) + " created string symbol >" + str.trim_suffix(str_end) + "< = " + created_symbol.name)
							continue
				
				line_code += str
				
				continue
			
			# Skip node paths
			if token.begins_with("$") or (token.begins_with("%") and i + 1 < line_data.tokens.size() and !"01234567890()".contains(line_data.tokens[i+1][0]) and !line_data.text.contains("% ")): #Hack: !line_data.text.contains("% ")
				var node_path : String = token
				i += 1
				while i < line_data.tokens.size():
					var section : String = line_data.tokens[i]
					node_path += line_data.tokens_tween[i] + section
					if "()[].,<=>+".contains(section):
						break
					i += 1
				i += 1
				line_code += node_path
				_script_log(str(script_data._idx+1) + " skipped node path >" + node_path + "<")
				continue
			
			# Skip declarations if obfuscation is disabled
			if !obfuscate_declarations and ["class", "class_name", "signal", "enum", "const", "var", "func"].has(token):
				line_code += token
				if i + 1 < line_data.tokens.size():
					line_code += line_data.tokens_tween[i + 1] + line_data.tokens[i + 1]
				i += 2
				#if token == "func":
					#pass #NOTE not yet sure if manually skipping all parameters is required
				_script_log(str(script_data._idx+1) + " skip " + token.to_upper() + " obfuscation")
				continue
			
			# Fetch maximum symbol path
			var search_members : bool = i == 0 or line_data.tokens[i-1] != "."
			var segments : PackedStringArray = [token]
			while i + 2 < line_data.tokens.size() and line_data.tokens[i + 1] == ".":
				segments.append("." + line_data.tokens[i + 2])
				token += segments[-1]
				i += 2
			
			# Search for token in members and globals
			while segments.size() > 0:
				if search_members:
					# Search local variables
					new_symbol = script_data.get_local_symbol(token, line_data.scope_path, line_data.scope_id)
					if new_symbol:
						_script_log(str(script_data._idx+1) + " found local symbol >" + new_symbol.get_meta("local_path") + "< = " + new_symbol.name)
						break
					
					# Search members
					new_symbol = script_data.get_member_symbol(token)
					if new_symbol:
						_script_log(str(script_data._idx+1) + " found member symbol >" + token + "< = " + new_symbol.name + " : " + new_symbol.type)
						break
				
				# Search globals
				if !new_symbol:
					new_symbol = _global_symbols.get_symbol(token)
					if new_symbol:
						_script_log(str(script_data._idx+1) + " found symbol >" + token + "< = " + new_symbol.name)
						break
				
				segments.resize(segments.size() - 1)
				token = ""
				for segment in segments:
					token += segment
				if segments.size() > 0:
					i -= 2
			
			line_code += new_symbol.name if new_symbol else original_token
			
			# Obfuscate string params
			if new_symbol and new_symbol.string_params and line_data.tokens[0] != "func":
				var params : Array[ScriptData.Param] = script_data.read_params(script_data._idx, i+1)
				for param_idx in params.size():
					var param : ScriptData.Param = params[param_idx]
					if !new_symbol.string_params.has(param_idx) or !param.is_string:
						continue
					var pos : Array[int] = script_data.increment_token_position(param.from_line, param.from_token, 2)
					var param_token : String = script_data.get_token_at(pos[0], pos[1])
					var param_symbol : SymbolTable.Symbol = _global_symbols.add_symbol(param_token)
					script_data.set_token_at(pos[0], pos[1], param_symbol.name)
					_script_log(str(script_data._idx+1) + " found string param " + param.raw + " " + str(param.from_line+1) + ":" + str(param.from_token) + "-" + str(param.to_line+1) + ":" + str(param.to_token) + " = " + "")
			
			# Skip dictionary accesses
			if new_symbol and new_symbol.type and new_symbol.type == "Dictionary":
				while i + 2 < line_data.tokens.size() and line_data.tokens[i + 1] == ".":
					line_code += line_data.tokens[i + 1] + line_data.tokens[i + 2]
					i += 2
			
			i += 1
		
		if line_data.tokens_tween.size() > line_data.tokens.size():
			line_code += line_data.tokens_tween[-1]
		
		line_mapper.add_linked_line(line_data, line_code)
		line_data = script_data.get_next_line()
	
	# Compile features
	if _feature_filters:# and _scripts_last_modification.get(path, 0) != FileAccess.get_modified_time(path):
		#_scripts_last_modification[path] = FileAccess.get_modified_time(path)
		_compile_script_features(script_data)
	
	# Strip comments
	if _strip_comments:
		for i in range(line_mapper.get_line_count() - 1, -1, -1):
			var line : ScriptData.Line = line_mapper.get_line(i)
			if line.text.contains("#"):
				var cur_string_literal : String
				var idx : int = 0
				while idx < line.text.length():
					var c : String = line.text[idx]
					if c == "'" or c == '"':
						if !cur_string_literal:
							cur_string_literal = c
						elif cur_string_literal == c:
							cur_string_literal = ""
					elif c == "#" and !cur_string_literal:
						line.text = line.text.substr(0, idx) if idx > 0 else ""
					idx += 1
	
	# Strip empty lines
	if _strip_empty_lines:
		for i in range(line_mapper.get_line_count() - 1, -1, -1):
			var line : ScriptData.Line = line_mapper.get_line(i)
			if !line.has_content():
				line_mapper.remove_line(i)
				continue
	
	if _regex_filter_enabled:
		var regex = RegEx.new()
		regex.compile(_regex_filter)
		for i in range(line_mapper.get_line_count() - 1, -1, -1):
			var line : ScriptData.Line = line_mapper.get_line(i)
			if regex.search(line.source_text):
				line_mapper.remove_line(i)
				continue
	
	# Inject startup code into the first autoload
	if path == _inject_autoload:
		var injection_code : String = 'print("GDMaim - Source map \'' + _source_map_filename + '\'\\n");'
		var did_inject : bool = false
		for i in line_mapper.get_line_count():
			var line : ScriptData.Line = line_mapper.get_line(i)
			if line.text.begins_with("func ") and line.text.contains("_enter_tree") and i + 1 < line_mapper.get_line_count():
				line_mapper.edit_line(i + 1, '\t' + injection_code + line_mapper.get_line(i + 1).text.trim_prefix("\t").trim_prefix("    "))
				did_inject = true
		if !did_inject:
			line_mapper.add_new_line('func _enter_tree() -> void:\n\t' + injection_code)
	
	script_data.export_code = line_mapper.get_code()
	
	return script_data.export_code


func _obfuscate_resource(path : String, source_data : String) -> String:
	if _is_debug_resource(path):
		print("\n---------- OBFUSCATE RESOURCE ", path, " ----------\n")
	#elif _obfuscate_debug_only:
		#return source_data
	
	var data : String = ""
	
	var lines : PackedStringArray = source_data.split("\n")
	var i : int = 0
	while i < lines.size():
		var line : String = lines[i]
		if line.begins_with("\""):
			data += line + "\n"
			i += 1
			continue
		
		if line.begins_with('[connection signal="') or line.begins_with('[node name="'):
			var node_paths : bool = false
			var tokens : PackedStringArray = line.split(" ", false)
			for token in tokens:
				if token.begins_with('signal="') or token.begins_with('method="') or token.begins_with('node_paths=PackedStringArray("') or node_paths:
					node_paths = (token.begins_with("node_paths") or node_paths) and token[-1] == ","
					
					var start : int = token.find('"')
					var end : int = token.find('"', start + 1)
					if end == -1:
						continue
					
					var name : String = token.substr(start + 1, end - (start + 1))
					var new_symbol : SymbolTable.Symbol = _global_symbols.get_symbol(name)
					if new_symbol:
						line = _replace_first(line, name, new_symbol.name)
						if _is_debug_resource(path):
							print(i+1, " FOUND SYMBOL >", name, "< = ", new_symbol.name)
		
		data += line + "\n"
		i += 1
		
		if _obfuscate_export_vars and (line.begins_with("[node") or line.begins_with("[sub_resource") or line.begins_with("[resource")):
			var tmp_lines : String
			var has_script : bool = line.contains("instance=") or line.contains('type="Animation"')
			var j : int = i
			while j < lines.size(): 
				if lines[j].begins_with("["):
					break
				
				tmp_lines += lines[j] + "\n"
				
				var tokens : PackedStringArray = lines[j].split(" = ", false, 1)
				if tokens.size() == 2 and tokens[0] == "script":
					has_script = true
					if _is_debug_resource(path):
						prints(i+1, "FOUND SCRIPT", line, tokens[1])
				
				j += 1
			
			if !has_script:
				data += tmp_lines
				i = j
			else:
				j = mini(j, lines.size())
				while i < j:
					line = lines[i]
					var tokens : PackedStringArray = line.split(" = ", false, 1)
					if tokens.size() == 2:
						if tokens[1].begins_with("NodePath(") and tokens[1].contains(":"):
							var parser := Parser.new(tokens[1])
							var node_path : String = parser.read_string()
							var properties : PackedStringArray = node_path.split(":", false)
							var new_path : String = properties[0]
							for property in properties.slice(1):
								var new_symbol : SymbolTable.Symbol = _global_symbols.get_symbol(property)
								new_path += ":" + (new_symbol.name if new_symbol else property)
							tokens[1] = 'NodePath("' + new_path + '")'
							line = tokens[0] + " = " + tokens[1]
							if _is_debug_resource(path) and node_path != new_path:
								print(i+1, " FOUND NODE PATH >", node_path, "< = ", new_path)
						
						var new_symbol : SymbolTable.Symbol = _global_symbols.get_symbol(tokens[0])
						if new_symbol:
							line = new_symbol.name + " = " + tokens[1]
							if _is_debug_resource(path):
								print(i+1, " FOUND EXPORT VAR >", tokens[0], "< = ", new_symbol.name)
					elif line.begins_with('"method":'):
						var parser := Parser.new(line.trim_prefix('"method":'))
						var method : String = parser.read_string()
						var new_symbol : SymbolTable.Symbol = _global_symbols.get_symbol(method)
						if new_symbol:
							line = '"method": &"' + new_symbol.name + '"'
							if _is_debug_resource(path):
								print(i+1, " FOUND METHOD >", method, "< = ", new_symbol.name)
					
					data += line + "\n"
					i += 1
	
	data = data.strip_edges(false, true) + "\n"
	
	if _is_debug_resource(path):
		print("")
		_print_source_code(data)
	
	return data


func _compile_script_features(script : ScriptData) -> void:
	if script.source_code.find("##FEATURE") == -1:
		return
	
	var line_mapper : ScriptData.LineMapper = script.line_mapper
	
	var idx : int = 0
	while idx < line_mapper.get_line_count():
		var line : ScriptData.Line = line_mapper.get_line(idx)
		idx += 1
		
		if line.text.begins_with("##FEATURE_FUNC "):
			var feature : String = line.text.trim_prefix("##FEATURE_FUNC ")
			if _features.has(feature):
				continue
			
			var func_name : String = script.name
			var ret_type : String
			line = line_mapper.get_line(idx)
			if !line.text.begins_with("func"):
				continue
			else:
				var splits1 : PackedStringArray = line.text.split("(")
				if splits1:
					func_name += "." + splits1[0].lstrip("func").replace(" ", "")
				var splits2 : PackedStringArray = line.text.split(")")
				if splits2:
					ret_type = splits2[-1].replace(" ", "").replace("->", "").replace(":", "")
			
			idx += 1
			line = line_mapper.get_line(idx)
			if line:
				line.text = '\tprinterr("ERROR: illegal call to ' + "'" + func_name + "'!" + '");'
				if ret_type == "bool":
					line.text += "return false"
				elif ret_type == "int":
					line.text += "return 0"
				elif ret_type == "float":
					line.text += "return 0.0"
				elif ret_type == "String":
					line.text += 'return ""'
				elif ret_type == "Array":
					line.text += "return []"
				elif ret_type == "Array[int]":
					line.text += "return []"
				elif ret_type == "Array[float]":
					line.text += "return []"
				elif ret_type == "Dictionary":
					line.text += "return {}"
				elif ret_type == "void":
					line.text += "pass"
				else:
					line.text += "return null"
			
			idx += 1
			line = line_mapper.get_line(idx)
			while line:
				if (line.text and line.text[0] != "\t" and line.text[0] != " " and line.text[0] != "#") or line.text.begins_with("##"):
					break
				else:
					line.text = ""
				idx += 1
				line = line_mapper.get_line(idx)


func _set_scope_path_level(scope_path : String, level : int) -> String:
	var out : String
	
	var cur_level : int = 0
	var idx : int = 0
	while idx < scope_path.length():
		if scope_path[idx] == ".":
			cur_level += 1
			if cur_level >= level:
				break
		out += scope_path[idx]
		idx += 1
	
	return out


func _get_scope_path_tail(scope_path : String) -> String:
	var idx : int = scope_path.rfind(".")
	if idx != -1 and idx + 1 < scope_path.length():
		return scope_path.substr(idx + 1)
	return scope_path


func _get_scope_class(path : String) ->  String:
	while path.count(".") > 0:
		if _classes.has(path):
			break
		path = path.substr(0, path.rfind("."))
	return path


func _get_var_type(idx : int, tokens : PackedStringArray) -> String:
	if idx + 2 < tokens.size() and tokens[idx + 1] == ":" and tokens[idx + 2] != "=":
		return tokens[idx + 2] #NOTE does not use global symbol table
	return ""


func _lines_to_code(lines : PackedStringArray) -> String:
	var code : String
	for line in lines:
		code += line + "\n"
	return code


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


func _replace_first(str : String, replace : String, with : String) -> String:
	var idx : int = str.find(replace)
	if idx == -1:
		return str
	elif idx == 0:
		return with + str.substr(idx + replace.length())
	else:
		return str.substr(0, idx) + with + str.substr(idx + replace.length())


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


func _script_log(text : String) -> void:
	if _current_script:
		_current_script.log_line(text)


func _print_source_code(source_code : String) -> void:
	var t : PackedStringArray = source_code.split("\n")
	for i in t.size():
		print(pad_digits(i+1, 7, "-"), "|", t[i])


static func pad_digits(num : int, digits : int, char : String = " ") -> String:
	var str : String
	var n : int = num / 10
	var d : int = 1
	while n > 0:
		n /= 10
		d += 1
	for i in digits - d:
		str += char
	return str + str(num)


func _is_debug_script(path : String) -> bool:
	for debug_script in _debug_scripts:
		if path.contains(debug_script):
			return true
	return false


func _is_debug_resource(path : String) -> bool:
	for res in _debug_resources:
		if path.contains(res):
			return true
	return false
