extends EditorExportPlugin


const ScriptData := preload("script_data.gd")
const SymbolTable := preload("symbol_table.gd")
const Parser := preload("parser.gd")

var cfg : ConfigFile

var _features : PackedStringArray
var _enabled : bool
var _convert_text_resources_to_binary : bool
var _export_path : String
var _scripts_last_modification : Dictionary
var _autoloads : Dictionary
var _built_in_symbols : Dictionary
var _global_symbols : SymbolTable
var _constants : Dictionary
var _scripts_data : Dictionary
var _global_classes : Dictionary
var _godot_data : Dictionary
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
var _debug_scripts : PackedStringArray
var _debug_resources : PackedStringArray
var _obfuscate_debug_only : bool


func _get_name() -> String:
	return "gdmaim"


func _export_begin(features : PackedStringArray, is_debug : bool, path : String, flags : int) -> void:
	_features = features
	_export_path = path
	_enabled = !features.has("no_gdmaim")
	if !_enabled:
		return
	
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
	_feature_filters = cfg.get_value("post_process", "feature_filters", false)
	_debug_scripts = cfg.get_value("debug", "debug_scripts", "").split(",", false)
	_debug_resources = cfg.get_value("debug", "debug_resources", "").split(",", false)
	_obfuscate_debug_only = cfg.get_value("debug", "obfuscate_debug_only", false)
	
	_convert_text_resources_to_binary = ProjectSettings.get_setting("editor/export/convert_text_resources_to_binary", false)
	
	if _convert_text_resources_to_binary:
		#push_warning("GDMaim: the project setting 'editor/export/convert_text_resources_to_binary' being enabled might significantly affect the time it takes to export")
		#_build_cache_path()
		push_warning("GDMaim: the project setting 'editor/export/convert_text_resources_to_binary' is enabled, but will be ignored during export")
	
	_prepare_obfuscation()


func _export_end() -> void:
	if !_enabled:
		return
	
	var symbol_table : String
	for symbol in _global_symbols.symbols:
		symbol_table += _global_symbols.symbols[symbol].name + "=" + symbol + "\n"
	if _write_file_str(_export_path.get_basename() + "_symbols.txt", symbol_table):
		print("GDMaim - a list of all identifiers and their generated names has been saved to '" + _export_path.get_basename() + "_symbols.txt'")
	else:
		push_warning("GDMaim - failed to write symbol table to '" + _export_path.get_basename() + "_symbols.txt'!")
	

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
		add_file(path, code.to_utf8_buffer(), true)


func _prepare_obfuscation() -> void:
	var scripts : PackedStringArray = _get_files("res://", ".gd")
	
	_godot_data.clear()
	_autoloads.clear()
	_built_in_symbols.clear()
	
	var cfg : ConfigFile = ConfigFile.new()
	cfg.load("res://project.godot")
	for autoload : String in cfg.get_section_keys("autoload"):
		_autoloads[cfg.get_value("autoload", autoload).replace("*", "")] = autoload
	
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
	
	var path : String = constant.substr(0, constant.rfind("."))
	var expr_text : String = constants[constant].name
	var tokens : PackedStringArray = _multi_split(expr_text, "+-*/(),")
	var input_names : PackedStringArray
	var inputs : Array
	
	if expr_text.contains(constant):
		results[constant] = expr_text
		return results[constant]
	
	for token in tokens:
		var token_raw : String = token
		if token and !token.contains(".") and !"01234567890".contains(token[0]):
			token = path + "." + token
		if constants.has(token):
			input_names.append(token_raw)
			inputs.append(_evaluate_constant(token, constants, results))
	
	var expr := Expression.new()
	expr.parse(expr_text, input_names)
	var result = expr.execute(inputs, null, false, true)
	if expr.has_execute_failed():
		#print("ERROR while evaluating constant '", constant, "': ", expr.get_error_text())
		results[constant] = expr_text
		return expr_text
	
	var symbol : SymbolTable.Symbol = constants[constant]
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
		_: pass
	symbol.get_meta("member").name = symbol.name
	
	#print("EVALUATING CONSTANT >", constant, "< = ", expr_text, " = ", symbol.name)
	
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
	if _is_debug_script(path):
		print("\n---------- ", "PARSE SCRIPT ", path, " ----------\n")
	
	var script : Script = load(path)
	var source_code : String = script.source_code
	var methods : Dictionary
	for method in script.get_script_method_list():
		methods[method.name] = method
	var signals : Dictionary
	for signal_ in script.get_script_signal_list():
		signals[signal_.name] = signal_
	
	_global_symbols.obfuscation_enabled = true
	
	var is_autoload : bool = _autoloads.has(path)
	var scope_path : String = _autoloads.get(path, path.get_file().replace(".", "_"))
	var scope_path_root : String = scope_path + "."
	var scope_path_identations : Array[int]
	var prev_identation : int = 0
	var scope_tree : Array
	var cur_scope_tree_branch : Array = scope_tree
	var cur_scope_tree_path : Array[int]
	var scope_id : String = ""
	var local_scope : bool = false
	var in_class : bool = false
	var lines : PackedStringArray = source_code.split("\n")
	
	var script_data : ScriptData = ScriptData.new(hash(path + str(_id_seed)), _built_in_symbols)
	script_data.exporter = self
	script_data.local_symbols.exporter = self
	script_data.path = path
	script_data.base_script = script.get_base_script().resource_path if script.get_base_script() else ""
	script_data.autoload = is_autoload
	script_data.code = source_code
	script_data.lines_str = lines
	_scripts_data[path] = script_data
	
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
		
		if line.begins_with("##OBFUSCATE ") and tokens.size() >= 4:
			if tokens[3] == "true":
				_global_symbols.obfuscation_enabled = true
			elif tokens[3] == "false":
				_global_symbols.obfuscation_enabled = false
			script_data.local_symbols.obfuscation_enabled = _global_symbols.obfuscation_enabled
			
			if _is_debug_script(path):
				prints(line_idx+1, "##OBFUSCATE", _global_symbols.obfuscation_enabled)
		elif line.begins_with("##OBFUSCATE_STRING_PARAMETERS"):
			string_param_names = line.trim_prefix("##OBFUSCATE_STRING_PARAMETERS").replace(" ", "").split(",", false)
			
			if _is_debug_script(path):
				prints(line_idx+1, "##OBFUSCATE_STRING_PARAMETERS", string_param_names)
		
		if tokens and tokens[0] != "#":
			var lower_scope : bool = false
			for i in range(scope_path_identations.size() -1, -1, -1):
				if identation <= scope_path_identations[i]:
					scope_path_identations.remove_at(i)
					lower_scope = true
			if lower_scope:
				scope_path = _set_scope_path_level(scope_path, scope_path_identations.size() + 1)
				local_scope = false
			
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
				
				#if _is_debug_script(path):
					#print(line_idx+1, " SCOPE ID ", scope_id)
				
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
						
						if _is_debug_script(path):
							print(line_idx+1, " SKIP EXPORT VAR ", token) 
					else:
						var type : String = _get_var_type(i, tokens)
						if local_scope:
							script_data.add_local_symbol(token, scope_path, scope_id, type)
						else:
							var new_symbol : SymbolTable.Symbol = _global_symbols.add_symbol(token, "", "")
							if new_symbol.name and (is_autoload or tokens[0] == "static"):
								_global_symbols.add_symbol(scope_path + "." + token, scope_path + "." + new_symbol.name, type)
							if !in_class:
								script_data.add_member_symbol(token, SymbolTable.Symbol.new(new_symbol.name if new_symbol.name else token, type))
						
						if _is_debug_script(path):
							print(line_idx+1, " VAR " if !local_scope else " LOCAL VAR ",  scope_path, ".", token, " : ", type)
					
					expr_type = ExpressionType.NONE
				
				ExpressionType.FUNC:
					if i == 1 or (i == 2 and tokens[0] == "static"):
						var new_symbol : SymbolTable.Symbol = _global_symbols.add_symbol(token)
						if new_symbol.name and (is_autoload or tokens[0] == "static"):
							_global_symbols.add_symbol(scope_path + "." + token, scope_path + "." + new_symbol.name).string_params = new_symbol.string_params
						if !local_scope and !in_class:
							script_data.add_member_symbol(token, new_symbol if new_symbol else SymbolTable.Symbol.new(token))
						
						string_params = new_symbol.string_params
						
						scope_path += "." + token
						scope_path_identations.append(identation)
						local_scope = true
						
						if _is_debug_script(path):
							print(line_idx+1, " FUNC ", scope_path)
						
						expr_type = ExpressionType.PARAMS
					else:
						#NOTE Lambda!
						expr_type = ExpressionType.NONE
				
				ExpressionType.CLASS:
					var new_symbol : SymbolTable.Symbol = _global_symbols.add_symbol(token)
					if new_symbol.name:
						_global_symbols.add_symbol(scope_path + "." + token, scope_path + "." + new_symbol.name)
					if !local_scope and !in_class:
						script_data.add_member_symbol(token, new_symbol if new_symbol else SymbolTable.Symbol.new(token))
					
					scope_path += "." + token
					scope_path_identations.append(identation)
					in_class = true
					
					if _is_debug_script(path):
						print(line_idx+1, " CLASS ", scope_path)
					
					expr_type = ExpressionType.NONE
				
				ExpressionType.CLASS_NAME:
					#var new_symbol : SymbolTable.Symbol = _global_symbols.add_symbol(token, "", "")
					#_global_classes[token] = new_symbol.name
					
					scope_path = token
					scope_path_root = token + "."
					expr_type = ExpressionType.NONE
					
					#if _is_debug_script(path):
						#print(line_idx+1, " GLOBAL CLASS ", token)
				
				ExpressionType.SIGNAL:
					if !_obfuscate_signals:
						expr_type = ExpressionType.NONE
					else:
						var new_symbol : SymbolTable.Symbol = _global_symbols.add_symbol(token)
						if new_symbol.name and is_autoload:
							_global_symbols.add_symbol(scope_path + "." + token, scope_path + "." + new_symbol.name)
						if !local_scope and !in_class:
							script_data.add_member_symbol(token, new_symbol if new_symbol else SymbolTable.Symbol.new(token))
						
						string_params = new_symbol.string_params
						
						scope_path += "." + token
						scope_path_identations.append(identation)
						local_scope = true
						
						if _is_debug_script(path):
							print(line_idx+1, " SIGNAL ", scope_path)
						
						expr_type = ExpressionType.NONE
						for j in range(i + 1, tokens.size()):
							if tokens[j] == "#":
								break
							elif tokens[j] == "(":
								expr_type = ExpressionType.PARAMS
								break
				
				ExpressionType.ENUM:
					var new_symbol : SymbolTable.Symbol = _global_symbols.add_symbol(token)
					_global_symbols.add_symbol(scope_path + "." + token, scope_path + "." + new_symbol.name if !_inline_enums or !_global_symbols.obfuscation_enabled else "int")
					if !local_scope and !in_class:
						#script_data.add_member_symbol(token, new_symbol if new_symbol else SymbolTable.Symbol.new(token if !_inline_enums else "int"))
						script_data.add_member_symbol(token, new_symbol if !_inline_enums or !_global_symbols.obfuscation_enabled else SymbolTable.Symbol.new("int"))
					
					cur_emum_idx = -1
					enum_path = scope_path + "." + (new_symbol.name if new_symbol else token)
					var j : int = scope_path.find(".")
					if j != -1:
						enum_path_local = scope_path.substr(j + 1) + "." + new_symbol.name
					else:
						enum_path_local = new_symbol.name
					
					scope_path += "." + token
					scope_path_identations.append(identation)
					local_scope = true
					
					if _is_debug_script(path):
						print(line_idx+1, " ENUM ", scope_path, " -> ", enum_path)
					
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
						
						var name : String = script_data.add_local_symbol(token, scope_path, scope_id, "", str(cur_emum_idx) if _inline_enums and _global_symbols.obfuscation_enabled else "")
						if name:
							_global_symbols.add_symbol(scope_path + "." + token, str(cur_emum_idx) if _inline_enums and _global_symbols.obfuscation_enabled and _global_symbols.obfuscation_enabled else enum_path + "." + name)
							_global_symbols.add_symbol(_get_scope_path_tail(scope_path) + "." + token, str(cur_emum_idx) if _inline_enums and _global_symbols.obfuscation_enabled else _get_scope_path_tail(enum_path) + "." + name)
							if !in_class:
								script_data.add_member_symbol(
									scope_path.trim_prefix(scope_path_root) + "." + token,
									SymbolTable.Symbol.new(str(cur_emum_idx) if _inline_enums else enum_path.trim_prefix(scope_path_root) + "." + name))
							
							if _is_debug_script(path):
								print(line_idx+1, " ENUM VALUE ", scope_path + "." + token, " -> ", enum_path + "." + name)
				
				ExpressionType.ENUM_VALUE_ASSIGNMENT:
					if token == "}":
						expr_type = ExpressionType.NONE
					elif token == ",":
						expr_type = ExpressionType.ENUM_VALUES
				
				ExpressionType.CONST:
					var type : String = _get_var_type(i, tokens)
					
					const inline_const_types : Array[String] = ["bool", "Color", "float", "int", "NodePath", "Vector2", "Vector2i", "Vector3", "Vector3i", "Vector4", "Vector4i"]
					var value : String
					if _inline_constants and i + 4 < tokens.size() and tokens[i + 3] == "=" and inline_const_types.has(type):
						skipped_lines[line_idx] = true
						for s in range(i + 4, tokens.size()):
							if tokens[s] == "#":
								break
							value += tokens[s]
						if i + 5 < tokens.size() and tokens[i + 5] == "(" and !tokens.has(")") and (tokens.find("#") == -1 or tokens.find(")") < tokens.find("#")):
							var depth : int = 0
							for l in range(line_idx + 1, lines.size()):
								skipped_lines[l] = true
								var line_parser := Parser.new(lines[l])
								var line_tokens : PackedStringArray = line_parser.get_tokens()
								for line_token in line_tokens:
									if line_token == "(":
										depth += 1
									elif line_token == ")":
										depth -= 1
									elif line_token == "#":
										break
									value += line_token
								if depth < 0:
									break
					
					if local_scope:
						script_data.add_local_symbol(token, scope_path, scope_id, type, value)
					else:
						var new_symbol : SymbolTable.Symbol = _global_symbols.add_symbol(token, value, "")
						_constants[scope_path + "." + token] = _global_symbols.add_symbol(scope_path + "." + token, value if value else scope_path + "." + new_symbol.name, type)
						if !local_scope and !in_class:
							_constants[scope_path + "." + token].set_meta("member",
								script_data.add_member_symbol(token, SymbolTable.Symbol.new(value if value else (new_symbol.name if new_symbol else token), type)))
					
					if _is_debug_script(path):
						print(line_idx+1, " CONST ", scope_path + "." + token, " : ", type)
					
					expr_type = ExpressionType.NONE
				
				ExpressionType.PARAMS:
					if token == ")":
						param_idx = -1
						string_param_names = PackedStringArray()
						expr_type = ExpressionType.NONE
					elif token == ":" or token == "=":
						expr_type = ExpressionType.PARAMS_HINT
						parentheses = 0
					elif token != "(" and token != "," and token != "\\":
						var type : String = _get_var_type(i, tokens)
						var name : String = script_data.add_local_symbol(token, scope_path, "", type)
						
						param_idx += 1
						if string_param_names.has(token):
							string_params[param_idx] = token
							
							if _is_debug_script(path):
								prints(line_idx+1, "SET STRING PARAM", token, param_idx)
						
						if _is_debug_script(path):
							print(line_idx+1, " PARAM ", scope_path + "." + token, " : ", type)
				
				ExpressionType.PARAMS_HINT:
					if token == ")":
						parentheses -= 1
						if parentheses < 0:
							param_idx = -1
							string_param_names = PackedStringArray()
							expr_type = ExpressionType.NONE
					elif token == "(":
						parentheses += 1
					elif token == "," and parentheses <= 0:
						expr_type = ExpressionType.PARAMS
				
				_:
					expr_type = ExpressionType.NONE
			
			i += 1
		
		var line_data : ScriptData.Line = script_data.new_line()
		line_data.skip = skipped_lines.get(line_idx, false)
		line_data.text = line
		line_data.tokens = tokens
		line_data.tokens_tween = tokens_tween
		line_data.scope_path = scope_path
		line_data.scope_id = scope_id
		line_data.identation = identation
		line_data.local_scope = local_scope
		line_data.in_class = in_class


func _obfuscate_script(path : String) -> String:
	if _is_debug_script(path):
		print("\n---------- ", "OBFUSCATE SCRIPT ", path, " ----------\n")
	elif _obfuscate_debug_only:
		return _scripts_data[path].code
	
	var lines : PackedStringArray
	
	# Insert obfuscated symbols
	var obfuscate_declarations : bool = true
	var script_data : ScriptData = _scripts_data[path]
	script_data.reload(_scripts_data)
	var line_data : ScriptData.Line = script_data.get_next_line()
	while line_data:
		var line_code : String
		
		if line_data.text.begins_with("##OBFUSCATE ") and line_data.tokens.size() >= 4:
			if line_data.tokens[3] == "true":
				obfuscate_declarations = true
			elif line_data.tokens[3] == "false":
				obfuscate_declarations = false
			lines.append(line_data.text)
			line_data = script_data.get_next_line()
			if _is_debug_script(path):
				prints(script_data._idx-1, "##OBFUSCATE", obfuscate_declarations)
			continue
		
		var i : int = 0
		while i < line_data.tokens.size():
			var token : String = line_data.tokens[i]
			var original_token : String = token
			var new_symbol : SymbolTable.Symbol
			
			# Skip marked lines
			if line_data.skip:
				break
			
			# Skip entire enum declaration if enum inlining is enabled
			if _inline_enums and token == "enum" and obfuscate_declarations:
				while line_data and !line_data.text.contains("}"):
					line_code += "\n"
					line_data = script_data.get_next_line()
				line_code += "\n"
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
						
						if _is_debug_script(path):
							print(script_data._idx+1, " FOUND STRING SYMBOL >", str.trim_suffix(str_end), "< = ", new_symbol.name)
						
						continue
					else:
						var created_symbol : SymbolTable.Symbol = _global_symbols.add_symbol(str.trim_suffix(str_end))
						if created_symbol:
							line_code += created_symbol.name + str_end
							
							if _is_debug_script(path):
								print(script_data._idx+1, " CREATED STRING SYMBOL >", str.trim_suffix(str_end), "< = ", created_symbol.name)
							
							continue
				
				line_code += str
				
				continue
			
			# Skip node paths
			if token.begins_with("$"):
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
				
				if _is_debug_script(path):
					prints(script_data._idx+1, "SKIPPING NODE PATH", ">" + node_path + "<")
				
				continue
			
			# Skip declarations if obfuscation is disabled
			if !obfuscate_declarations and ["class", "class_name", "signal", "enum", "const", "var", "func"].has(token):
				line_code += token
				if i + 1 < line_data.tokens.size():
					line_code += line_data.tokens_tween[i + 1] + line_data.tokens[i + 1]
				i += 2
				#if token == "func":
					#pass #NOTE not yet sure if manually skipping all parameters is required
				
				if _is_debug_script(path):
					print(script_data._idx+1, " SKIPPING " + token.to_upper() + " DECLARATION")
				
				continue
			
			# First, search for token in local vars, if currently in a local scope
			if line_data.local_scope and (i == 0 or line_data.tokens[i-1] != "."):
				new_symbol = script_data.get_local_symbol(token, line_data.scope_path, line_data.scope_id)
				if new_symbol and _is_debug_script(path):
					print(script_data._idx+1, " FOUND LOCAL SYMBOL >", line_data.scope_path + "." + token + "." + line_data.scope_id, "< = ", new_symbol.name)
			
			# Search for token in members and globals
			if !new_symbol:
				# Retrieve full token path
				var search_members : bool = i == 0 or line_data.tokens[i-1] != "."
				var segments : PackedStringArray = [token]
				while i + 2 < line_data.tokens.size() and line_data.tokens[i + 1] == ".":
					segments.append("." + line_data.tokens[i + 2])
					token += segments[-1]
					i += 2
				
				while segments.size() > 0:
					# Search members
					if search_members:
						new_symbol = script_data.get_member_symbol(token)
						if new_symbol:
							if _is_debug_script(path):
								print(script_data._idx+1, " FOUND MEMBER SYMBOL >", token, "< = ", new_symbol.name, " : ", new_symbol.type)
							break
					
					# Search globals
					if !new_symbol:
						new_symbol = _global_symbols.get_symbol(token)
						if new_symbol:
							if _is_debug_script(path):
								print(script_data._idx+1, " FOUND SYMBOL >", token, "< = ", new_symbol.name)
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
					
					if _is_debug_script(path):
						prints(script_data._idx+1, "FOUND STRING PARAM", param.raw, str(param.from_line+1) + ":" + str(param.from_token), "-", str(param.to_line+1) + ":" + str(param.to_token), " = ", "")
			
			# Skip dictionary accesses
			if new_symbol and new_symbol.type and new_symbol.type == "Dictionary":
				while i + 2 < line_data.tokens.size() and line_data.tokens[i + 1] == ".":
					line_code += line_data.tokens[i + 1] + line_data.tokens[i + 2]
					i += 2
			
			i += 1
		
		if line_data.tokens_tween.size() > line_data.tokens.size():
			line_code += line_data.tokens_tween[-1]
		
		lines.append(line_code)
		line_data = script_data.get_next_line()
	
	# Compile features
	if _feature_filters:# and _scripts_last_modification.get(path, 0) != FileAccess.get_modified_time(path):
		#_scripts_last_modification[path] = FileAccess.get_modified_time(path)
		lines = _compile_script_features(path, _lines_to_code(lines)).split("\n")
	
	# Strip comments
	if _strip_comments:
		for i in range(lines.size() - 1, -1, -1):
			var line : String = lines[i]
			if line.contains("#"):
				var cur_string_literal : String
				var idx : int = 0
				while idx < line.length():
					var c : String = line[idx]
					if c == "'" or c == '"':
						if !cur_string_literal:
							cur_string_literal = c
						elif cur_string_literal == c:
							cur_string_literal = ""
					elif c == "#" and !cur_string_literal:
						if idx > 0:
							lines[i] = line.substr(0, idx)
							break
						else:
							if _strip_empty_lines:
								lines.remove_at(i)
							else:
								lines[i] = "# ..."
							break
					idx += 1
	
	# Strip empty lines
	if _strip_empty_lines:
		for i in range(lines.size() - 1, -1, -1):
			var line : String = lines[i]
			if line.replace(" ", "").replace("\n", "").replace("\t", "").length() == 0:
				lines.remove_at(i)
				continue
	
	var code : String
	for line in lines:
		code += line + "\n"
	code = code.strip_edges(false, true) + "\n"
	
	if _is_debug_script(path):
		print("")
		_print_source_code(code)
	
	return code


func _obfuscate_resource(path : String, source_data : String) -> String:
	if _is_debug_resource(path):
		print("\n---------- OBFUSCATE RESOURCE ", path, " ----------\n")
	elif _obfuscate_debug_only:
		return source_data
	
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


#TODO: this should not be it's own function anymore...
func _compile_script_features(path : String, source_code : String) -> String:
	if source_code.find("##FEATURE") == -1:
		return source_code
	
	var script_class : String = ""
	if source_code.contains("class_name"):
		var idx : int = source_code.find("class_name")
		var cls : String = source_code.substr(idx, source_code.find("\n", idx) - idx)
		cls = cls.lstrip("class_name ")
		for c in cls:
			if c == " " or c == "\t":
				break
			script_class += c
	else:
		script_class = path.get_file().rstrip(".gd")
	
	var lines : PackedStringArray = source_code.split("\n")
	source_code = ""
	
	var idx : int = 0
	while idx < lines.size():
		var line : String = lines[idx]
		idx += 1
	
		source_code += line + "\n"
		
		if line.begins_with("##FEATURE_FUNC "):
			var feature : String = line.lstrip("##FEATURE_FUNC ")
			if _features.has(feature):
				continue
			
			var func_name : String = script_class
			var ret_type : String
			line = lines[idx]
			if !line.begins_with("func"):
				continue
			else:
				source_code += line + "\n"
				
				var splits1 : PackedStringArray = line.split("(")
				if splits1:
					func_name += "." + splits1[0].lstrip("func").replace(" ", "")
				
				var splits2 : PackedStringArray = line.split(")")
				if splits2:
					ret_type = splits2[-1].replace(" ", "").replace("->", "").replace(":", "")
			
			idx += 1
			var separation_buffer : String = ""
			while idx < lines.size():
				line = lines[idx]
				if (line and line[0] != "\t" and line[0] != " " and line[0] != "#") or line.begins_with("##FEATURE") or idx + 1 >= lines.size():
					source_code += '\tprinterr("ERROR: illegal call to ' + "'" + func_name + "'!" + '")\n'
					if ret_type == "bool":
						source_code += "\treturn false"
					elif ret_type == "int":
						source_code += "\treturn 0"
					elif ret_type == "float":
						source_code += "\treturn 0.0"
					elif ret_type == "String":
						source_code += '\treturn ""'
					elif ret_type == "Array":
						source_code += "\treturn []"
					elif ret_type == "Array[int]":
						source_code += "\treturn []"
					elif ret_type == "Array[float]":
						source_code += "\treturn []"
					elif ret_type == "Dictionary":
						source_code += "\treturn {}"
					elif ret_type == "void":
						source_code += "\tpass"
					else:
						source_code += "\treturn null"
					
					source_code += separation_buffer
					
					break
				else:
					var true_line : String = line.replace("\t", "").replace(" ", "").replace("\n", "")
					if true_line and true_line[0] != "#":
						separation_buffer = "\n"
					else:
						separation_buffer += lines[idx] + "\n"
				idx += 1
	
	return source_code


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


func _build_cache_path() -> void:
	var cache_dir : String = get_script().resource_path.get_base_dir() + "/cache"
	if !DirAccess.dir_exists_absolute(cache_dir):
		DirAccess.make_dir_recursive_absolute(cache_dir)
	_write_file_str(cache_dir + "/.gdignore", "")
	_write_file_str(get_script().resource_path.get_base_dir() + "/.gitignore", "cache/")


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


func _print_source_code(source_code : String) -> void:
	var t : PackedStringArray = source_code.split("\n")
	for i in t.size():
		print(_pad_digits(i+1, 7, "-"), "|", t[i])


func _pad_digits(num : int, digits : int, char : String = " ") -> String:
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
