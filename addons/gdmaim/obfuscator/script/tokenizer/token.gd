extends RefCounted


enum Type {
	NONE = 0,
	SYMBOL = 2^0,
	KEYWORD = 2^1,
	LITERAL = 2^2,
	NUMBER_LITERAL = 2^3,
	STRING_LITERAL = 2^4,
	NODE_PATH = 2^5,
	PUNCTUATOR = 2^6,
	OPERATOR = 2^7,
	COMMENT = 2^8,
	WHITESPACE = 2^9,
	IDENTATION = 2^10,
	LINE_BREAK = 2^11,
}

const StringRef := preload("../../string_ref.gd")
const SymbolTable := preload("../../symbol_table.gd")

var type : Type
var idx : int
var line : int
var symbol : SymbolTable.Symbol

var _value := StringRef.new()


func _init(type : Type, value : String, idx : int, line : int) -> void:
	self.type = type
	self.idx = idx
	self.line = line
	_value.set_value(value)


func _to_string() -> String:
	return str(_value)


func set_value(new_val : String) -> void:
	_value.set_value(new_val)


func get_value() -> String:
	return str(_value)


func link_value(root : StringRef) -> void:
	_value.link(root)


func link_symbol(symbol : SymbolTable.Symbol) -> void:
	self.symbol = symbol
	_value.link(symbol.name)


func has_value(value : String) -> bool:
	return get_value() == value


func is_symbol(value : String = "") -> bool:
	return type == Type.SYMBOL and (!value or get_value() == value)


func is_keyword(value : String = "") -> bool:
	return type == Type.KEYWORD and (!value or get_value() == value)


func is_literal(value : String = "") -> bool:
	return type == Type.LITERAL and (!value or get_value() == value)


func is_number_literal(value : String = "") -> bool:
	return type == Type.NUMBER_LITERAL and (!value or get_value() == value)


func is_string_literal(value : String = "") -> bool:
	return type == Type.STRING_LITERAL and (!value or get_value() == value)


func is_node_path(value : String = "") -> bool:
	return type == Type.NODE_PATH and (!value or get_value() == value)


func is_punctuator(value : String = "") -> bool:
	return type == Type.PUNCTUATOR and (!value or get_value() == value)


func is_operator(value : String = "") -> bool:
	return type == Type.OPERATOR and (!value or get_value() == value)


func is_comment(value : String = "") -> bool:
	return type == Type.COMMENT and (!value or get_value() == value)


func is_whitespace(value : String = "") -> bool:
	return type == Type.WHITESPACE and (!value or get_value() == value)


func is_identation(value : String = "") -> bool:
	return type == Type.IDENTATION and (!value or get_value() == value)


func is_line_break(value : String = "") -> bool:
	return type == Type.LINE_BREAK and (!value or get_value() == value)
