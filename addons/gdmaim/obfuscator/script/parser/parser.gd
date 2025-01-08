extends RefCounted


const _Logger := preload("../../../logger.gd")
const PreprocessorHints := preload("../preprocessor_hints.gd")
const Stream := preload("../tokenizer/stream.gd")
const Token := preload("../tokenizer/token.gd")
const Tokenizer := preload("../tokenizer/tokenizer.gd")
const SymbolTable := preload("../../symbol_table.gd")
const AST := preload("ast.gd")

var _tokenizer : Tokenizer
var _symbol_table : SymbolTable
var _class_symbol : SymbolTable.Symbol
var _is_autoload : bool
var _current_identation : int


func read(tokenizer : Tokenizer, symbol_table : SymbolTable, autoload_symbol : SymbolTable.Symbol = null) -> AST.ASTNode:
	_tokenizer = tokenizer
	_symbol_table = symbol_table
	_class_symbol = autoload_symbol
	_is_autoload = autoload_symbol != null
	
	var ast := AST.Class.new(null)
	ast.body = _parse_block(ast, -1)
	
	_symbol_table = null
	
	return ast


func get_class_symbol() -> SymbolTable.Symbol:
	return _class_symbol 


func _parse_block(parent : AST.ASTNode, identation : int) -> AST.Sequence:
	var ast := AST.Sequence.new(parent)
	ast.token_from = _tokenizer.peek(1)
	
	var prev_line : Tokenizer.Line = null
	while !_tokenizer.is_eof():
		var token : Token = _tokenizer.peek(1)
		var line : Tokenizer.Line = _tokenizer.get_output_line(token.line)
		
		if prev_line and prev_line != line and line.has_statement() and line.get_identation() <= identation:
			break
		else:
			prev_line = line
		
		if parent is AST.Match and token.is_punctuator(":") and line.get_identation() == identation + 1:
			ast.statements.append(_parse_block(ast, identation + 1))
		else:
			var statement : AST.ASTNode = _parse_statement(ast)
			if statement:
				ast.statements.append(statement)
	
	ast.token_to = _tokenizer.peek(0)
	
	return ast


func _parse_statement(parent : AST.ASTNode) -> AST.ASTNode:
	var token : Token = _tokenizer.peek()
	
	#TODO skip and consume identation here
	
	match token.type:
		Token.Type.LINE_BREAK: #TODO remove
			_parse_line_break()
			return null
		Token.Type.IDENTATION: #TODO remove
			_parse_identation()
			return null
		Token.Type.SYMBOL:
			return _parse_symbol(parent)
			#return _parse_call(parent) if _is_call() else _parse_assignment(parent)
		Token.Type.KEYWORD:
			return _parse_keyword(parent)
	
	_tokenizer.get_next()
	
	#TODO skip line break here, unless we've got a semicolon coming up next
	
	return null


func _parse_line_break() -> void:
	_tokenizer.get_next()
	_current_identation = 0


func _parse_identation() -> void:
	var token : Token = _tokenizer.get_next()
	_current_identation = token.get_value().length()


func _parse_symbol(parent : AST.ASTNode) -> AST.Symbol:
	var ast := AST.Symbol.new(parent)
	
	ast.path = _parse_symbol_path(ast)
	
	return ast


func _parse_symbol_path(ast_node : AST.ASTNode) -> SymbolTable.SymbolPath:
	var path : SymbolTable.SymbolPath = _symbol_table.create_symbol_path(ast_node)
	path.maybe_local = !_tokenizer.peek(0) or !_tokenizer.peek(0).is_punctuator(".")
	path.set_log(_Logger.current_log)
	path.line = _tokenizer.peek().line
	
	while !_tokenizer.is_eof():
		var token : Token = _tokenizer.peek()
		if token.is_punctuator("."):
			_tokenizer.get_next()
			continue
		elif token.is_symbol():
			_tokenizer.get_next()
			var symbol : SymbolTable.Symbol = path.add(token.get_value())
			token.link_symbol(symbol)
			continue
		break
	
	path.is_call = _tokenizer.peek().is_punctuator("(")
	
	return path


func _parse_call(parent : AST.ASTNode) -> AST.Call:
	var ast := AST.Call.new(parent)
	
	ast.path = _parse_symbol_path(ast)
	ast.args = _parse_args(ast)
	
	return ast


func _parse_args(parent : AST.ASTNode) -> Array[AST.Expr]:
	var args : Array[AST.Expr]
	
	while !_tokenizer.is_eof():
		var token : Token = _tokenizer.peek()
		if token.is_punctuator("(") or token.is_punctuator(","):
			_tokenizer.get_next()
			_parse_expression(parent)
		elif token.is_punctuator(")"):
			_tokenizer.get_next()
			break
		else:
			_Logger.write("ERROR: Parser._parse_args() - Unexpected token '" + str(token) + "'!")
			return args
	
	return args


func _parse_assignment(parent : AST.ASTNode) -> AST.Assignment:
	var ast := AST.Assignment.new(parent)
	
	ast.target = _parse_symbol_path(ast)
	ast.op = _tokenizer.get_next().get_value()
	ast.expr = _parse_expression(ast)
	
	return ast


func _parse_expression(parent : AST.ASTNode, end : String = "") -> AST.Expr:
	var ast := AST.Expr.new(parent)
	
	#TODO
	
	return ast


func _parse_keyword(parent : AST.ASTNode) -> AST.ASTNode:
	var token : Token = _tokenizer.get_next()
	
	if token.get_value().begins_with("@export"):
		if _tokenizer.peek().is_punctuator("("):
			_skip_brackets("(", ")")
		while _tokenizer.get_next().get_value() != "var":
			pass
		return _parse_export_var(parent)
	
	match token.get_value():
		"class_name":
			_parse_class_name(parent)
		"extends":
			_parse_extends(parent)
		"class":
			return _parse_class(parent)
		"signal":
			return _parse_signal(parent)
		"enum":
			return _parse_enum(parent)
		"const":
			return _parse_const(parent)
		"var":
			return _parse_var(parent)
		"func":
			return _parse_func(parent)
		"if":
			return _parse_if(parent)
		"else":
			return _parse_else(parent)
		"elif":
			return _parse_elif(parent)
		"match":
			return _parse_match(parent)
		"for":
			return _parse_for(parent)
		"while":
			return _parse_while(parent)
	
	return null


func _parse_if(parent : AST.ASTNode) -> AST.If:
	if _tokenizer.peek(-1).type != Token.Type.IDENTATION:
		return null # Don't parse inline conditionals
	
	var ast := AST.If.new(parent)
	
	var identation : int = _current_identation
	
	ast.condition = _parse_expression(ast, ":")
	ast.body = _parse_block(ast, identation)
	
	return ast


func _parse_else(parent : AST.ASTNode) -> AST.Else:
	if _tokenizer.peek(-1).type != Token.Type.IDENTATION:
		return null # Don't parse inline conditionals
	
	var ast := AST.Else.new(parent)
	
	var identation : int = _current_identation
	
	ast.body = _parse_block(ast, identation)
	
	return ast


func _parse_elif(parent : AST.ASTNode) -> AST.Elif:
	if _tokenizer.peek(-1).type != Token.Type.IDENTATION:
		return null # do not parse inline conditionals
	
	var ast := AST.Elif.new(parent)
	
	var identation : int = _current_identation
	
	ast.condition = _parse_expression(ast, ":")
	ast.body = _parse_block(ast, identation)
	
	return ast


func _parse_match(parent : AST.ASTNode) -> AST.Match:
	var ast := AST.Match.new(parent)
	
	ast.body = _parse_block(ast, _current_identation)
	#var identation : int = _current_identation
	#
	#while !_tokenizer.is_eof():
		#var token : Token = _tokenizer.peek()
		#if token.is_symbol():
			#ast.symbols.append(_parse_symbol(ast))
		#elif token.is_punctuator(":"):
			#_tokenizer.get_next()
			#_tokenizer.get_next()
			#break
		#else:
			#_tokenizer.get_next()
	#
	#while !_tokenizer.is_eof():
		#var token : Token = _tokenizer.peek()
		#var line : Tokenizer.Line = _tokenizer.get_output_line(token.line)
		#if !line.has_statement():
			#_tokenizer.skip_to("\n")
		#elif line.get_identation() <= identation:
			#break
		#else:
			#_tokenizer.get_next()
			#if _tokenizer.peek().is_symbol():
				#ast.symbols.append(_parse_symbol(ast))
			#_tokenizer.skip_to("\n")
			#ast.patterns.append(_parse_block(ast, identation + 1))
	
	return ast


func _parse_for(parent : AST.ASTNode) -> AST.For:
	var ast := AST.For.new(parent)
	
	var identation : int = _current_identation
	var symbol_token : Token = _tokenizer.get_next()
	
	var iterator := AST.Iterator.new(ast)
	iterator.symbol = _symbol_table.create_symbol(ast, symbol_token.get_value(), _parse_var_type(ast))
	
	ast.iterator = iterator
	ast.expr = _parse_expression(ast, ":")
	ast.body = _parse_block(ast, identation)
	
	symbol_token.link_symbol(iterator.symbol)
	if _line_has_hint(PreprocessorHints.LOCK_SYMBOLS):
		_symbol_table.lock_symbol(iterator.symbol)
	
	return ast


func _parse_while(parent : AST.ASTNode) -> AST.While:
	var ast := AST.While.new(parent)
	
	var identation : int = _current_identation
	
	ast.condition = _parse_expression(ast, ":")
	ast.body = _parse_block(ast, identation)
	
	return ast


func _parse_class_name(parent : AST.ASTNode) -> void:
	var token : Token = _tokenizer.get_next()
	if !token.is_symbol():
		_Logger.write("ERROR: Parser._parse_class_name() - Symbol expected!")
		return
	
	var ast : AST.ASTNode = parent
	while ast.get_parent() and not ast is AST.Class:
		ast = ast.get_parent()
	
	_class_symbol = _symbol_table.create_global_symbol(token.get_value())
	ast.symbol = _class_symbol
	
	token.link_symbol(ast.symbol)


func _parse_extends(parent : AST.ASTNode) -> void:
	var token : Token = _tokenizer.peek()
	if !token.is_symbol():
		_Logger.write("ERROR: Parser._parse_extends() - Symbol expected!")
		return
	
	var ast : AST.ASTNode = parent
	while ast.get_parent() and not ast is AST.Class:
		ast = ast.get_parent()
	
	ast.ext = _parse_symbol_path(ast)


func _parse_class(parent : AST.ASTNode) -> AST.Class:
	var token : Token = _tokenizer.get_next()
	if !token.is_symbol():
		_Logger.write("ERROR: Parser._parse_class() - Symbol expected!")
		return null
	
	var ast := AST.Class.new(parent)
	
	var name : String = token.get_value()
	var identation : int = _current_identation
	
	if _tokenizer.peek().is_keyword("extends"):
		_tokenizer.get_next()
		ast.ext = _parse_symbol_path(ast)
	
	ast.symbol = _symbol_table.create_global_symbol(name)
	ast.body = _parse_block(ast, identation)
	
	token.link_symbol(ast.symbol)
	if _line_has_hint(PreprocessorHints.LOCK_SYMBOLS):
		_symbol_table.lock_symbol(ast.symbol)
	
	if _class_symbol:
		_class_symbol.add_child(ast.symbol)
	
	return ast


func _parse_signal(parent : AST.ASTNode) -> AST.SignalDef:
	var token : Token = _tokenizer.get_next()
	if !token.is_symbol():
		_Logger.write("ERROR: Parser._parse_signal() - Symbol expected!")
		return null
	
	var ast := AST.SignalDef.new(parent)
	ast.symbol = _symbol_table.create_global_symbol(token.get_value())
	ast.params = _parse_params(parent)
	
	token.link_symbol(ast.symbol)
	if _line_has_hint(PreprocessorHints.LOCK_SYMBOLS):
		_symbol_table.lock_symbol(ast.symbol)
	
	if _class_symbol and _is_autoload:
		_class_symbol.add_child(ast.symbol)
	
	return ast


func _parse_enum(parent : AST.ASTNode) -> AST.EnumDef:
	var token : Token = _tokenizer.get_next()
	if !token.is_symbol():
		_Logger.write("ERROR: Parser._parse_enum() - Symbol expected!")
		return null
	
	var ast := AST.EnumDef.new(parent)
	ast.symbol = _symbol_table.create_global_symbol(token.get_value())
	
	token.link_symbol(ast.symbol)
	if _line_has_hint(PreprocessorHints.LOCK_SYMBOLS):
		_symbol_table.lock_symbol(ast.symbol)
	
	var expect_key : bool = true
	while !_tokenizer.is_eof():
		token = _tokenizer.peek()
		if token.is_symbol():
			if expect_key:
				ast.keys.append(_parse_enum_key(ast))
				expect_key = false
				continue
			else:
				parent.statements.append(_parse_symbol(parent))
		elif token.is_punctuator(","):
			expect_key = true
		elif token.is_punctuator("}"):
			break
		_tokenizer.get_next()
	
	if _class_symbol:
		_class_symbol.add_child(ast.symbol)
	
	return ast


func _parse_enum_key(parent : AST.EnumDef) -> AST.EnumDef.KeyDef:
	var token : Token = _tokenizer.get_next()
	if !token.is_symbol():
		_Logger.write("ERROR: Parser._parse_enum_key() - Symbol expected!")
		return null
	
	var key := AST.EnumDef.KeyDef.new(parent)
	#key.symbol = _symbol_table.create_local_symbol(token.get_value())
	key.symbol = _symbol_table.create_global_symbol(token.get_value())
	
	token.link_symbol(key.symbol)
	if _line_has_hint(PreprocessorHints.LOCK_SYMBOLS):
		_symbol_table.lock_symbol(key.symbol)
	
	parent.symbol.add_child(key.symbol)
	
	token = _tokenizer.peek()
	if token.is_operator("="):
		_tokenizer.get_next()
		key.expr = _parse_expression(key)
	else:
		key.expr = AST.Expr.new(key)
	
	return key


func _parse_const(parent : AST.ASTNode) -> AST.Const:
	var token : Token = _tokenizer.get_next()
	if !token.is_symbol():
		_Logger.write("ERROR: Parser._parse_const() - Symbol expected!")
		return null
	
	var ast := AST.Const.new(parent)
	ast.symbol = _symbol_table.create_symbol(ast, token.get_value(), _parse_var_type(ast))
	
	token.link_symbol(ast.symbol)
	if _line_has_hint(PreprocessorHints.LOCK_SYMBOLS):
		_symbol_table.lock_symbol(ast.symbol)
	
	if _class_symbol:
		_class_symbol.add_child(ast.symbol)
	
	return ast


func _parse_var(parent : AST.ASTNode) -> AST.Var:
	var token : Token = _tokenizer.get_next()
	if !token.is_symbol():
		_Logger.write("ERROR: Parser._parse_var() - Symbol expected!")
		return null
	
	var is_static : bool = _tokenizer.peek(-2).is_keyword("static")
	
	var ast := AST.Var.new(parent)
	ast.symbol = _symbol_table.create_symbol(ast, token.get_value(), _parse_var_type(ast))
	
	token.link_symbol(ast.symbol)
	if _line_has_hint(PreprocessorHints.LOCK_SYMBOLS):
		_symbol_table.lock_symbol(ast.symbol)
	
	if _class_symbol and (_is_autoload or is_static):
		_class_symbol.add_child(ast.symbol)
	
	return ast


func _parse_export_var(parent : AST.ASTNode) -> AST.ExportVar:
	var token : Token = _tokenizer.get_next()
	if !token.is_symbol():
		_Logger.write("ERROR: Parser._parse_export_var() - Symbol expected!")
		return null
	
	var ast := AST.ExportVar.new(parent)
	ast.symbol = _symbol_table.create_export_symbol(token.get_value(), _parse_var_type(ast))
	
	token.link_symbol(ast.symbol)
	if _line_has_hint(PreprocessorHints.LOCK_SYMBOLS):
		_symbol_table.lock_symbol(ast.symbol)
	
	if _class_symbol and _is_autoload:
		_class_symbol.add_child(ast.symbol)
	
	return ast


func _parse_func(parent : AST.ASTNode) -> AST.Func:
	var token : Token = _tokenizer.peek()
	var name : String = "@lambda"
	var is_static : bool = false
	var lock_symbols : bool = _line_has_hint(PreprocessorHints.LOCK_SYMBOLS)
	var obfuscate_string_params : bool = _line_has_hint(PreprocessorHints.OBFUSCATE_STRING_PARAMETERS, -1)
	var string_params : PackedStringArray = _line_get_hint_args(PreprocessorHints.OBFUSCATE_STRING_PARAMETERS, -1).split(" ", false)
	if token.is_symbol():
		_tokenizer.get_next()
		name = token.get_value()
		is_static = _tokenizer.peek(-2).is_keyword("static")
	
	var identation : int = _current_identation
	
	var ast := AST.Func.new(parent)
	
	var params : Array[AST.Parameter] = _parse_params(ast)
	var type : String = _parse_func_type(ast)
	
	ast.symbol = _symbol_table.create_symbol(ast, name, type)
	ast.params = params
	ast.body = _parse_block(ast, identation)
	
	if token.is_symbol():
		token.link_symbol(ast.symbol)
		if lock_symbols:
			_symbol_table.lock_symbol(ast.symbol)
	
	if obfuscate_string_params:
		for i in params.size():
			if string_params.has(params[i].symbol.get_name()):
				ast.symbol.add_string_param(i)
	
	if _class_symbol and (_is_autoload or is_static):
		_class_symbol.add_child(ast.symbol)
	
	return ast


func _parse_params(parent : AST.ASTNode) -> Array[AST.Parameter]:
	var params : Array[AST.Parameter]
	if _tokenizer.is_eof() or !_tokenizer.peek().is_punctuator("("):
		return params
	
	var pth : int = 0
	var param := AST.Parameter.new(parent)
	var expect_symbol : bool = true
	while !_tokenizer.is_eof():
		var token : Token = _tokenizer.peek()
		if token.is_punctuator():
			_tokenizer.get_next()
			if token.has_value("("):
				pth += 1
			elif token.has_value(")"):
				pth -= 1
				if pth <= 0:
					if param.symbol:
						params.append(param)
					param = AST.Parameter.new(parent)
					return params
			elif token.has_value(",") and pth == 1:
				expect_symbol = true
				params.append(param)
				param = AST.Parameter.new(parent)
		elif token.is_symbol():
			if expect_symbol:
				_tokenizer.get_next()
				expect_symbol = false
				param.symbol = _symbol_table.create_local_symbol(token.get_value(), _parse_var_type(param))
				token.link_symbol(param.symbol)
				if _line_has_hint(PreprocessorHints.LOCK_SYMBOLS):
					_symbol_table.lock_symbol(param.symbol)
	
			else:
				_parse_symbol_path(parent)
		else:
			_tokenizer.get_next()
	
	return params


func _parse_func_type(ast : AST.ASTNode) -> String:
	if _tokenizer.peek(1).is_operator("->"):
		_tokenizer.get_next()
		var path : SymbolTable.SymbolPath = _parse_symbol_path(ast)
		return str(path)#.trim_suffix("void")
	
	return ""


func _parse_var_type(ast : AST.ASTNode) -> String:
	if _tokenizer.peek(1).is_punctuator(":") and !_tokenizer.peek(2).is_operator("="):
		_tokenizer.get_next()
		var path : SymbolTable.SymbolPath = _parse_symbol_path(ast)
		return str(path)
	
	return ""


func _skip_brackets(brackets_in : String, brackets_out : String) -> void:
	var pth : int = 0
	while !_tokenizer.is_eof():
		var token : Token = _tokenizer.get_next()
		if token.is_punctuator():
			if token.get_value() == brackets_in:
				pth += 1
			elif token.get_value() == brackets_out:
				pth -= 1
				if pth <= 0:
					return


func _get_line_hints(offset : int = 0) -> Dictionary:
	var line : Tokenizer.Line = _tokenizer.get_output_line(_tokenizer.peek(0).line + offset)
	return line.hints if line else {}


func _line_has_hint(hint : String, offset : int = 0) -> bool:
	var line : Tokenizer.Line = _tokenizer.get_output_line(_tokenizer.peek(0).line + offset)
	return line.hints.has(hint) if line else false


func _line_get_hint_args(hint : String, offset : int = 0) -> String:
	var line : Tokenizer.Line = _tokenizer.get_output_line(_tokenizer.peek(0).line + offset)
	return line.hints.get(hint, "") if line else ""


func _is_call() -> bool:
	var i : int = 1
	var token : Token = _tokenizer.peek(i)
	while token and (token.is_symbol() or token.is_punctuator(".")):
		i += 1
		token = _tokenizer.peek(i)
	
	return token.is_punctuator("(") if token else false


func _is_statement(token : Token) -> bool:
	return token and token.type != Token.Type.COMMENT and token.type != Token.Type.WHITESPACE and token.type != Token.Type.IDENTATION and token.type != Token.Type.LINE_BREAK 
