extends RefCounted


const ScriptData := preload("script_data.gd")
const SymbolTable := preload("symbol_table.gd")

var exporter : Object
var path : String
var base_script : String
var autoload : bool = false
var code : String
var lines : Array[Line]
var lines_str : PackedStringArray
var local_symbols : SymbolTable
var member_symbols : Dictionary
var exclude_symbols : Dictionary

var _idx : int = -1
var _local_symbols_table : Dictionary


func _init(seed : int, exclude_symbols : Dictionary) -> void:
	local_symbols = SymbolTable.new(seed, true)
	self.exclude_symbols = exclude_symbols


func reload(script_cache : Dictionary) -> void:
	var cur_script : ScriptData = script_cache.get(base_script)
	while cur_script:
		#print("INHERIT MEMBERS FROM ", cur_script.path)
		for member in cur_script.member_symbols:
			member_symbols[member] = cur_script.member_symbols[member]
		cur_script = script_cache.get(cur_script.base_script)
	
	for name in member_symbols.keys(): #HACK we need to manually update dictionary with "keys()"; looks like a godot bug
		var splits1 : PackedStringArray = name.split(".")
		var splits2 : PackedStringArray = member_symbols[name].name.split(".")
		var new_name : String
		for i in splits1.size():
			if exclude_symbols.has(splits1[i]):
				new_name += ("." if new_name else "") + exclude_symbols[splits1[i]]
			elif splits1.size() == splits2.size():
				new_name += ("." if new_name else "") + splits2[i]
			else:
				new_name = member_symbols[name].name
				break
		member_symbols[name].name = new_name


func new_line() -> Line:
	lines.append(Line.new())
	return lines.back()


func get_line_count() -> int:
	return lines.size()


func get_next_line() -> Line:
	_idx += 1
	if _idx < lines.size():
		return lines[_idx]
	return null


func read_params(line_idx : int, token_idx : int) -> Array[Param]:
	var params : Array[Param]
	
	var cur_param : String
	var is_string : bool = false
	var from_line : int = line_idx
	var from_token : int = token_idx
	var parantheses : int = -1
	for i in range(line_idx, lines.size()):
		var line : Line = lines[i]
		while token_idx < line.tokens.size():
			var token : String = line.tokens[token_idx]
			if token == '"' or token == "'":
				is_string = true
			if token == "(":
				parantheses += 1
				if parantheses > 0:
					cur_param += token
			elif token == ")":
				parantheses -= 1
				if parantheses > 0:
					cur_param += token
				else:
					params.append(Param.new(cur_param, from_line, from_token, i, token_idx, is_string))
					return params
			elif token == ",":
				if parantheses == 0:
					params.append(Param.new(cur_param, from_line, from_token, i, token_idx, is_string))
					cur_param = ""
					is_string = false
					from_line = i
					from_token = token_idx
			else:
				cur_param += token
			token_idx += 1
		
		token_idx = 0
	
	return params


func add_member_symbol(symbol_name : String, symbol : SymbolTable.Symbol) -> SymbolTable.Symbol:
	member_symbols[symbol_name] = symbol
	return symbol


func get_member_symbol(symbol_name : String) -> SymbolTable.Symbol:
	return member_symbols.get(symbol_name)


func add_local_symbol(symbol_name : String, scope_path : String, scope_id : String, type : String = "", custom_name : String = "") -> String:
	var symbol : SymbolTable.Symbol = local_symbols.get_symbol(scope_path + "." + symbol_name + "." + scope_id)
	if symbol:
		#NOTE should this even ever happen?
		#prints("LOCAL SYMBOL ALREADY EXISTS:", scope_path + "." + symbol_name + "." + scope_id, symbol.name)
		return symbol.name
	
	if exclude_symbols.has(symbol_name) or (!custom_name and !local_symbols.obfuscation_enabled):
		custom_name = symbol_name
	
	local_symbols.add_symbol(scope_path + "." + symbol_name + "." + scope_id, custom_name, type)
	symbol = local_symbols.get_symbol(scope_path + "." + symbol_name + "." + scope_id)
	var symbols : Dictionary = _local_symbols_table.get(scope_path + "." + symbol_name, {})
	_local_symbols_table[scope_path + "." + symbol_name] = symbols
	symbols[scope_id] = symbol
	
	return symbol.name


func get_local_symbol(name : String, scope_path : String, scope_id : String) -> SymbolTable.Symbol:
	var symbols : Dictionary = _local_symbols_table.get(scope_path + "." + name, {})
	
	for target_scope_id : String in symbols:
		if !target_scope_id or scope_id.begins_with(target_scope_id):
			return symbols[target_scope_id]
	
	return null


func increment_token_position(line : int, token : int, increment : int) -> Array[int]:
	var prev_pos : Array[int] = [line, token]
	for i in increment:
		token += 1
		while token >= lines[line].tokens.size():
			if line >= lines.size():
				return prev_pos
			token = 0
			line += 1
	
	return [line, token]


func get_token_at(line : int, token : int) -> String:
	return lines[line].tokens[token]


func set_token_at(line : int, token : int, new : String) -> void:
	lines[line].tokens[token] = new


class Line:
	var skip : bool
	var text : String
	var tokens : PackedStringArray
	var tokens_tween : PackedStringArray
	var scope_path : String
	var scope_id : String
	var identation : int
	var local_scope : bool
	var in_class : bool


class Param:
	var raw : String
	var from_line : int
	var from_token : int
	var to_line : int
	var to_token : int
	var is_string : bool
	
	func _init(raw : String, from_line : int, from_token : int, to_line : int, to_token : int, is_string : bool) -> void:
		self.raw = raw
		self.from_line = from_line
		self.from_token = from_token
		self.to_line = to_line
		self.to_token = to_token
		self.is_string = is_string
