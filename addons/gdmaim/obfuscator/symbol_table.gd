extends RefCounted


const _Logger := preload("../logger.gd")
const _Settings := preload("../settings.gd")
const StringRef := preload("string_ref.gd")
const AST := preload("script/parser/ast.gd")

var settings : _Settings

var _seed : int
var _locked_symbols : Dictionary
var _global_symbols : Dictionary
var _local_symbols : Array[Symbol]
var _export_symbols : Dictionary
var _symbol_paths : Array[SymbolPath]
var _id_list : PackedStringArray
var _id_table : Dictionary
var _unique_ids : Dictionary


func _init(settings : _Settings) -> void:
	self.settings = settings
	_seed = hash(str(settings.symbol_seed if !settings.symbol_dynamic_seed else int(Time.get_unix_time_from_system())))


func lock_symbol_name(name : String) -> void:
	_locked_symbols[name] = true


func lock_symbol(symbol : Symbol) -> void:
	var lidx : int = _local_symbols.find(symbol)
	if lidx != -1:
		_local_symbols.remove_at(lidx)
	else:
		lock_symbol_name(symbol.get_name())


func create_symbol(ast_node : AST.ASTNode, name : String, type : String = "") -> Symbol:
	if ast_node.get_parent() is AST.Sequence and ast_node.get_parent().get_parent() is AST.Class:
		return create_global_symbol(name, type)
	else:
		return create_local_symbol(name, type)


func create_export_symbol(name : String, type : String = "") -> Symbol:
	var symbol : Symbol = create_global_symbol(name, type)
	_export_symbols[name] = symbol
	
	if !settings.obfuscate_export_vars:
		lock_symbol_name(name)
	
	return symbol


func create_global_symbol(name : String, type : String = "") -> Symbol:
	var symbol : Symbol = _global_symbols.get(name)
	if !symbol:
		symbol = Symbol.new(name)
		_global_symbols[name] = symbol
	
	return Symbol.new_linked(type, symbol)


func create_local_symbol(name : String, type : String = "") -> Symbol:
	var symbol := Symbol.new(name, type)
	_local_symbols.append(symbol)
	
	return symbol


func create_symbol_path(ast_node : AST.ASTNode) -> SymbolPath:
	var path := SymbolPath.new()
	path.set_ast(ast_node)
	_symbol_paths.append(path)
	
	return path


func find_global_symbol(symbol : String) -> Symbol:
	return _global_symbols.get(symbol)


func find_export_symbol(symbol : String) -> Symbol:
	return _export_symbols.get(symbol)


func rename_symbol(symbol : Symbol, target : String) -> void:
	#if _locked_symbols.has(symbol.get_name()): #NOTE obfuscation does its own check
		#return
	
	symbol.name.set_value(target)


func resolve_symbol_paths() -> void:
	for symbol_path in _symbol_paths:
		var log : String = "Line " + str(symbol_path.line + 1) + " | " + str(symbol_path) + " |"
		
		var cur : Symbol
		var maybe_local : bool = symbol_path.maybe_local
		for symbol in symbol_path.symbols:
			log += " [" + symbol.get_name() + "="
			if cur:
				cur = cur.children.get(symbol.get_name())
				log += "CHILD" if cur else ""
			if !cur and maybe_local:
				cur = _search_symbol(symbol_path.get_ast(), symbol.get_name(), symbol_path.is_call and symbol_path.symbols.back() == symbol)
				log += "LOCAL" if cur else ""
			if !cur:
				cur = find_global_symbol(symbol.get_name())
				log += "GLOBAL" if cur else ""
			if cur:
				symbol.link(cur)
				if cur.type == "Dictionary":
					break
			else:
				log += "?"
			maybe_local = false
			log += "]"
		
		_Logger.swap(symbol_path.get_log())
		_Logger.write(log)


func obfuscate_symbols() -> void:
	for symbol in _local_symbols:
		rename_symbol(symbol, _generate_symbol_name(symbol.get_name(), true))
	
	for symbol in _global_symbols.values():
		if !_locked_symbols.has(symbol.get_name()):
			rename_symbol(symbol, _generate_symbol_name(symbol.get_name()))


func obfuscate_string_global(str : String) -> String:
	return _generate_symbol_name(str) if !_locked_symbols.has(str) else str


func _search_symbol(ast_node : AST.ASTNode, name : String, is_func : bool) -> Symbol:
	var origin : AST.ASTNode = ast_node
	
	ast_node = ast_node.get_parent()
	while ast_node:
		for child in ast_node.get_children():
			if child == origin:
				break
			elif child is AST.SymbolDeclaration and child.symbol.get_name() == name and (child is AST.Func == is_func):
				return child.symbol
		
		ast_node = ast_node.get_parent()
	
	return null


func _generate_symbol_name(source_name : String, unique : bool = false) -> String:
	if !unique and _id_table.has(source_name):
		return _id_table[source_name]
	
	var random := RandomNumberGenerator.new()
	random.seed = hash(source_name) + source_name.length() + _seed
	
	var target_length : int = maxi(1, settings.symbol_target_length)
	var id : String
	while !id or _id_list.has(id):
		var _id : String = settings.symbol_prefix
		for j in target_length:
			_id += settings.symbol_characters[random.randi() % settings.symbol_characters.length()]
		if !id and unique and _id_list.has(_id):
			_unique_ids[source_name] = _unique_ids.get(source_name, 0) + 1
			random.seed += _unique_ids[source_name]
		else:
			target_length += 1
		id = _id
	
	_id_list.append(id)
	if !unique:
		_id_table[source_name] = id
	
	return id


class Symbol extends RefCounted:
	var name : StringRef
	var source_name : String
	var type : String
	var children : Dictionary
	var string_params : Array
	var parent : Symbol
	
	static func new_linked(type : String, ref : Symbol) -> Symbol:
		var symbol := Symbol.new(ref.source_name, type)
		symbol.link(ref)
		return symbol
	
	func _init(name : String, type : String = "") -> void:
		self.name = StringRef.new(name)
		self.source_name = name
		self.type = type
	
	func _to_string() -> String:
		return str(name) if !type else str(name) + ":" + type
	
	func get_name() -> String:
		return name.get_value()
	
	func get_source_name() -> String:
		return source_name
	
	func add_child(other : Symbol) -> void:
		children[other.name.get_value()] = other
	
	func add_string_param(idx : int) -> void:
		if !string_params.has(idx):
			string_params.append(idx)
	
	func has_string_params() -> bool:
		return !string_params.is_empty()
	
	func is_string_param(idx : int) -> bool:
		return string_params.has(idx)
	
	func link(other : Symbol) -> void:
		name.link(other.name)
		parent = other
		children.merge(other.children)
		other.children = children
		string_params = string_params + other.string_params
		other.string_params = string_params
	
	func get_root() -> Symbol:
		return parent.get_root() if parent else self


class SymbolPath:
	var ast : WeakRef
	var symbols : Array[Symbol]
	var is_call : bool = false
	var maybe_local : bool = true
	var log : WeakRef
	var line : int
	
	func _to_string() -> String:
		var str : String
		for symbol in symbols:
			str += str(symbol) + "."
		return str.trim_suffix(".")
	
	func add(name : String) -> Symbol:
		var symbol := Symbol.new(name)
		symbols.append(symbol)
		return symbol
	
	func set_ast(ast : AST.ASTNode) -> void:
		self.ast = weakref(ast)
	
	func get_ast() -> AST.ASTNode:
		return ast.get_ref() as AST.ASTNode if ast else null
	
	func set_log(log : Variant) -> void:
		self.log = weakref(log)
	
	func get_log() -> Variant:
		return log.get_ref() if log else null
