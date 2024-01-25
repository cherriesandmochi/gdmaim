extends RefCounted


var exporter : Object
var symbols : Dictionary
var obfuscation_enabled : bool = true

var _seed : int
var _exclude_symbols : Dictionary
var _id_list : PackedStringArray


func _init(seed : int, obfuscate_names : bool = true, exclude_symbols : Dictionary = {}) -> void:
	_seed = seed
	obfuscation_enabled = obfuscate_names
	_exclude_symbols = exclude_symbols


func add_symbol(name : String, custom_name : String = "", type : String = "") -> Symbol:
	#HACK illegal symbols should never get picked up in the first place; edit: not sure if they still do
	const illegal_symbols : PackedStringArray = ["gd", "in", "for", "while", "if", "else", "pass", "break", "return", "res", "var", "func", "static", "const", "enum", "class", "signal", "await"]
	
	if symbols.has(name):
		if !obfuscation_enabled:
			exclude_symbol(name)
		return symbols[name]
	elif name.length() < 3 or illegal_symbols.has(name) or _exclude_symbols.has(name):
		exclude_symbol(name)
		custom_name = name
	elif !obfuscation_enabled and !custom_name:
		if !name.contains("."):
			exclude_symbol(name)
		custom_name = name
	
	var symbol := Symbol.new(_generate_symbol_name(name) if !custom_name else custom_name, type)
	symbols[name] = symbol
	
	return symbol


func exclude_symbol(name : String) -> void:
	_exclude_symbols[name] = name


func has_symbol(name : String) -> bool:
	return symbols.has(name)


func get_symbol(name : String) -> Symbol:
	return symbols.get(name)


func _generate_symbol_name(name : String) -> String:
	var random := RandomNumberGenerator.new()
	random.seed = hash(name) + name.length() + _seed
	var target_length : int = maxi(1, exporter._id_target_length)
	var id : String
	while !id or _id_list.has(id):
		id = exporter._id_prefix
		for j in target_length:
			id += exporter._id_characters[random.randi() % exporter._id_characters.length()]
		target_length += 1
	
	_id_list.append(id)
	
	return id


class Symbol:
	var name : String
	var type : String
	var string_params : Dictionary
	
	func _init(name : String = "", type : String = "") -> void:
		self.name = name
		self.type = type
