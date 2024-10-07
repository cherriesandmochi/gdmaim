extends RefCounted


const _Logger := preload("../../../logger.gd")
const _Settings := preload("../../../settings.gd")
const Token := preload("token.gd")
const Stream := preload("stream.gd")

const KEYWORDS : Array[String] = ["extends", "class_name", "@tool", "@onready", "setget", "const", "signal", "enum", "static", "var", "func","@rpc", "class", "pass", "if", "else", "elif", "while", "for", "in", "match", "continue", "break", "return", "assert", "yield", "await", "preload", "load", "as", "and", "or", "not"]
const LITERALS : Array[String] = ["true", "false", "null", "self", "PI", "TAU", "NAN", "INF"]
const OPERATORS : String = "+-*^/%=<>!&|"
const PUNCTUATORS : String = "()[]{},;:."
const IDENTIFIER_CHARACTERS : String = "1234567890_@"

var line_count : int

var _stream : Stream
var _tokens : Array[Token]
var _idx : int
var _line : int
var _output : Array[Line]


func read(source_code : String) -> void:
	line_count = 1
	_tokens.clear()
	_idx = -1
	_line = 0
	_output = [Line.new()]
	
	_stream = Stream.new(source_code)
	while _read_next_token(): pass


func reset() -> void:
	_idx = -1


func is_eof(offset : int = 0) -> bool:
	return _idx + offset >= _tokens.size() - 1


func get_next() -> Token:
	_idx += 1
	return _tokens[_idx] if _idx < _tokens.size() else null


func get_next_filtered(type_filter : int = Token.Type.WHITESPACE + Token.Type.COMMENT + Token.Type.IDENTATION + Token.Type.LINE_BREAK) -> Token:
	var token : Token = peek_next_filtered(type_filter)
	if token:
		seek_token(token)
	else:
		_idx = _tokens.size() - 1
	
	return token


func peek_next_filtered(type_filter : int = Token.Type.WHITESPACE + Token.Type.COMMENT + Token.Type.IDENTATION + Token.Type.LINE_BREAK) -> Token:
	for i in range(_idx + 1, _tokens.size()):
		var token : Token = _tokens[i]
		if !(token.type & type_filter):
			return token
	
	return null


func peek(offset : int = 1) -> Token:
	return _tokens[_idx + offset] if _idx + offset < _tokens.size() and _idx + offset >= 0 else null


func seek_token(token : Token) -> Token:
	_idx = token.idx
	return token


func skip_to(token_str : String) -> Token:
	while !is_eof():
		var token : Token = get_next()
		if token.get_value() == token_str:
			return token
	
	return null


func get_tokens() -> Array[Token]:
	return _tokens


func insert_output_token(line_idx : int, token_idx : int, token : Token) -> void:
	_output[line_idx].insert_token(token_idx, token)


func remove_output_token(line_idx : int, token_idx : int) -> void:
	_output[line_idx].remove_token(token_idx)


func insert_output_line(line_idx : int, line : Line) -> void:
	_output.insert(line_idx, line)


func remove_output_line(line_idx : int) -> void:
	_output.remove_at(line_idx)


func get_output_line(idx : int) -> Line:
	return _output[idx] if idx >= 0 and idx < _output.size() else null


func find_output_line(line : Line) -> int:
	return _output.find(line)


func get_output_line_count() -> int:
	return _output.size()


func get_output_lines() -> Array[Line]:
	return _output


func generate_source_code() -> String:
	var code : String
	for line in _output:
		for token in line.tokens:
			code += token.get_value()
	
	return code


func _read_next_token() -> bool:
	if _stream.is_eof():
		return false
	
	if _stream.peek(1) == "\\" and _stream.peek(2) == "\n":
		# Multi-line statement
		_stream.get_next()
		_stream.get_next()
		return true
	elif _stream.peek() == "\n":
		# Line break
		_stream.get_next()
		_add_line_break()
		line_count += 1
		_output.append(Line.new())
		return true
	
	var char : String = _stream.peek()
	if _is_whitespace(char):
		if _stream.get_column_pos() == 0:
			# Identation
			_read_identation()
		else:
			# Whitespace
			_read_whitespace()
	elif char == "#":
		# Comment
		_read_comment()
	elif _is_punctuator(char):
		# Punctuator
		_add_punctuator(_stream.get_next())
	elif _is_operator(char):
		# Operator
		_read_operator()
	elif "\"'".contains(char):
		# String(literal)
		_read_string()
	elif char == "$":
		_read_node_path()
	elif _is_digit(char):
		# Number(literal)
		_read_number()
	elif _is_valid_identifier(char):
		# Identifier(symbol, keyword or literal)
		_read_identifier()
	else:
		_stream.get_next()
		_Logger.write("ERROR: Tokenizer._read_next_token() - Invalid character '" + char + "' at " + _stream.get_pos_str())
	
	return true


func _read_identation() -> void:
	_add_identation(_read_while(_is_whitespace).length())


func _read_whitespace() -> void:
	_add_whitespace(_read_while(_is_whitespace))


func _read_comment() -> void:
	var text : String = _read_until("\n")
	if text.begins_with(_Settings.current.preprocessor_prefix):
		var hints : PackedStringArray = text.split(_Settings.current.preprocessor_prefix)
		for hint in hints:
			var slices : PackedStringArray = hint.split(" ", false, 1)
			if slices:
				_output.back().add_hint(slices[0], slices[1] if slices.size() == 2 else "")
	_add_comment(text)


func _read_string() -> void:
	var str : String
	var end : String = _stream.get_next()
	
	while !_stream.is_eof():
		var char : String = _stream.get_next()
		if char == "\\":
			char += _stream.get_next()
		if char == end:
			break
		str += char
	
	_add_string_literal(end + str + end)


func _read_node_path() -> void:
	var str : String
	var str_char : String
	
	while !_stream.is_eof():
		var char : String = _stream.peek()
		if char == "\n" or (_is_punctuator(char) and (char != "." or !str_char)) or (_is_whitespace(char) and !str_char):
			break
		if char == "'" or char == '"':
			str_char = ""
			if str == "$":
				str_char = char
		str += char
		_stream.get_next()
	
	_add_node_path(str)


func _read_operator() -> void:
	_add_operator(_read_while(_is_operator))


func _read_number() -> void:
	_add_number_literal(_read_while(_is_digit))


func _read_identifier() -> void:
	var id : String = _read_while(_is_valid_identifier)
	if _is_keyword(id):
		_add_keyword(id)
	elif _is_literal(id):
		_add_literal(id)
	else:
		_add_symbol(id)


func _read_while(condition : Callable) -> String:
	var str : String
	
	while !_stream.is_eof():
		var char : String = _stream.peek()
		if !condition.call(char):
			return str
		str += char
		_stream.get_next()
	
	return str


func _read_until(delimeters : String) -> String:
	var str : String
	
	while !_stream.is_eof():
		var char : String = _stream.peek()
		if delimeters.contains(char):
			return str
		str += char
		_stream.get_next()
	
	return str


func _is_whitespace(char : String) -> bool:
	return char == " " or char == "\t"


func _is_operator(char : String) -> bool:
	return OPERATORS.contains(char)


func _is_punctuator(char : String) -> bool:
	return PUNCTUATORS.contains(char)


func _is_digit(char : String) -> bool:
	return ".0123456789".contains(char)


func _is_valid_identifier(char : String) -> bool:
	return TextServerManager.get_primary_interface().is_valid_letter(char.unicode_at(0)) or IDENTIFIER_CHARACTERS.contains(char)
	#return char.is_valid_unicode_identifier() or char == "@" #TODO: Godot 4.4


func _is_keyword(token : String) -> bool:
	return KEYWORDS.has(token) or token.begins_with("@export")


func _is_literal(token : String) -> bool:
	return LITERALS.has(token)


func _add_token(type : Token.Type, value : String, readable : bool = true) -> void:
	var token := Token.new(type, value, _tokens.size(), _output.size()-1)
	_output.back().add_token(token)
	if readable:
		_tokens.append(token)


func _add_symbol(name : String) -> void:
	_add_token(Token.Type.SYMBOL, name)


func _add_keyword(keyword : String) -> void:
	_add_token(Token.Type.KEYWORD, keyword)


func _add_literal(literal : String) -> void:
	_add_token(Token.Type.LITERAL, literal)


func _add_number_literal(number : String) -> void:
	_add_token(Token.Type.NUMBER_LITERAL, number)


func _add_string_literal(str : String) -> void:
	_add_token(Token.Type.STRING_LITERAL, str)


func _add_node_path(str : String) -> void:
	_add_token(Token.Type.NODE_PATH, str)


func _add_punctuator(punctuator : String) -> void:
	_add_token(Token.Type.PUNCTUATOR, punctuator)


func _add_operator(operator : String) -> void:
	_add_token(Token.Type.OPERATOR, operator)


func _add_comment(text : String) -> void:
	_add_token(Token.Type.COMMENT, text, false)


func _add_whitespace(str : String) -> void:
	_add_token(Token.Type.WHITESPACE, str, false)


func _add_identation(identation : int) -> void:
	_add_token(Token.Type.IDENTATION, "\t".repeat(identation))


func _add_line_break() -> void:
	_add_token(Token.Type.LINE_BREAK, "\n")


class Line:
	var tokens : Array[Token]
	var hints : Dictionary
	var identation : int
	
	func _init(tokens : Array[Token] = []) -> void:
		self.tokens = tokens
	
	func _to_string() -> String:
		var str : String
		for token in tokens:
			str += str(token)
		return str
	
	func add_token(token : Token) -> void:
		tokens.append(token)
	
	func insert_token(idx : int, token : Token) -> void:
		tokens.insert(idx, token)
	
	func remove_token(idx : int) -> void:
		tokens.remove_at(idx)
	
	func erase_token(token : Token) -> void:
		tokens.erase(token)
	
	func clear_tokens(keep_identation : bool = true) -> void:
		for i in range(tokens.size() - 1, -1, -1):
			var token : Token = tokens[i]
			if !token.is_line_break() and (!keep_identation or !token.is_identation()):
				tokens.remove_at(i)
	
	func add_hint(hint : String, args : String) -> void:
		hints[hint] = args
	
	func has_hint(hint : String) -> bool:
		return hints.has(hint)
	
	func get_hint_args(hint : String) -> String:
		return hints.get(hint, "")
	
	func get_identation() -> int:
		return 0 if !tokens or tokens[0].type != Token.Type.IDENTATION else tokens[0].get_value().length()
	
	func has_statement() -> bool:
		var t : int = 0 if get_identation() == 0 else 1
		return tokens.size() >= t + 1 and !tokens[t].is_comment() and !tokens[t].is_line_break()
	
	func has_token_value(value : String) -> bool:
		for token in tokens:
			if token.get_value() == value:
				return true
		return false
