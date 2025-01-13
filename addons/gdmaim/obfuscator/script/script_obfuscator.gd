extends RefCounted


const _Logger := preload("../../logger.gd")
const _Settings := preload("../../settings.gd")
const PreprocessorHints := preload("preprocessor_hints.gd")
const SymbolTable := preload("../symbol_table.gd")
const Parser := preload("parser/parser.gd")
const Token := preload("tokenizer/token.gd")
const Tokenizer := preload("tokenizer/tokenizer.gd")
const AST := preload("parser/ast.gd")

var path : String
var source_code : String
var generated_code : String
var parser : Parser
var tokenizer : Tokenizer

var _symbol_table : SymbolTable
var _ast : AST.ASTNode


func _init(path : String) -> void:
	self.path = path


func parse(source_code : String, symbol_table : SymbolTable, autoload_symbol : SymbolTable.Symbol = null) -> void:
	self.source_code = source_code
	_symbol_table = symbol_table
	tokenizer = Tokenizer.new()
	tokenizer.read(source_code)
	parser = Parser.new()
	_ast = parser.read(tokenizer, symbol_table, autoload_symbol)


func run(features : PackedStringArray) -> bool:
	if !parser:
		_Logger.write("ERROR: ScriptObfuscator.run() - No parsed data!")
		return false
	
	tokenizer.reset()
	while !tokenizer.is_eof():
		var token : Token = tokenizer.get_next()
		var next_token : Token = tokenizer.peek()
		var line : Tokenizer.Line = tokenizer.get_output_line(token.line)
		var prev_line : Tokenizer.Line = tokenizer.get_output_line(token.line-1)
		
		if _Settings.current.obfuscation_enabled:
			if line.has_hint(PreprocessorHints.OBFUSCATE_STRINGS):
				_string_obfuscation(token)
			_string_param_obfuscation(token, next_token)
		
		if !_Settings.current.feature_filters:
			continue
		
		if prev_line and prev_line.has_hint(PreprocessorHints.FEATURE_FUNC):
			_func_feature_filter(token, line, prev_line.get_hint_args(PreprocessorHints.FEATURE_FUNC), features)
	
	if _Settings.current.obfuscation_enabled and _Settings.current.shuffle_top_level:
		_shuffle_toplevel()
	
	_strip_code()
	
	return true


func generate_source_code() -> String:
	generated_code = tokenizer.generate_source_code()
	return generated_code


func generate_line_mappings() -> Array[Dictionary]:
	var mappings_in : Dictionary
	var mappings_out : Dictionary
	var output_lines : Array[Tokenizer.Line] = tokenizer.get_output_lines()
	for i in output_lines.size():
		var line : Tokenizer.Line = output_lines[i]
		for token in line.tokens:
			if token.line != -1:
				mappings_in[token.line] = i
				mappings_out[i] = token.line
				break
	#HACK it works?
	mappings_in[tokenizer.line_count-1] = output_lines.size()
	mappings_out[output_lines.size()] = tokenizer.line_count-1
	
	var last_valid : int = 0
	for i in tokenizer.line_count:
		var from : int = mappings_in.get(i, last_valid)
		last_valid = from
		mappings_in[i] = from
	
	return [mappings_in, mappings_out]


func get_class_symbol() -> SymbolTable.Symbol:
	return parser.get_class_symbol() if parser else null


func _string_obfuscation(token : Token) -> void:
	if !token.is_string_literal():
		return
	
	var str : String = token.get_value()
	token.set_value(str[0] + _symbol_table.obfuscate_string_global(str.substr(1, str.length() - 2)) + str[-1])


func _string_param_obfuscation(token : Token, next_token : Token) -> void:
	if !token.symbol or !next_token or !next_token.is_punctuator("("):
		return
	
	var symbol : SymbolTable.Symbol = token.symbol
	if !symbol.has_string_params():
		return
	
	var param : int = 0
	var pth : int = 0
	var maybe_str_param : bool = true
	while !tokenizer.is_eof():
		token = tokenizer.get_next()
		if token.is_punctuator():
			if token.has_value("("):
				pth += 1
			elif token.has_value(")"):
				pth -= 1
				if pth <= 0:
					break
			elif token.has_value(",") and pth == 1:
				param += 1
				maybe_str_param = true
			continue
		elif maybe_str_param and token.is_string_literal() and symbol.is_string_param(param):
			var str : String = token.get_value()
			token.set_value(str[0] + _symbol_table.obfuscate_string_global(str.substr(1, str.length() - 2)) + str[-1])
		maybe_str_param = false


func _func_feature_filter(token : Token, line : Tokenizer.Line, feature : String, features : PackedStringArray) -> void:
	if !token.is_keyword("func"):
		return
	
	if !features.has(feature):
		var identation : int = line.get_identation()
		var func_path : String = path.get_basename() + "." + tokenizer.get_next().get_value()#tokenizer.get_next().symbol.get_source_name()
		var func_body : String = 'printerr("ERROR: illegal call to ' + "'" + func_path + "'!" + '");'
		var pth : int = 0
		while !tokenizer.is_eof():
			token = tokenizer.get_next()
			if token.is_punctuator("("):
				pth += 1
			elif token.is_punctuator(")"):
				pth -= 1
				if pth <= 0:
					break
		token = tokenizer.get_next()
		if token.is_operator("->"):
			token = tokenizer.get_next()
			var ret_type : String = token.get_value()
			const ret_code : Dictionary = {
				"bool": "return false",
				"int": "return 0",
				"float": "return 0.0",
				"String": 'return ""',
				"Array": "return []",
				"Array[int]": "return []",
				"Array[float]": "return []",
				"Dictionary": "return {}",
				"void": "",
			}
			func_body += ret_code.get(ret_type, "return null")
		var line_idx_from : int = tokenizer.find_output_line(line)
		var line_idx : int = line_idx_from + 2
		var last_valid : int = -1
		tokenizer.get_output_line(line_idx_from + 1).clear_tokens()
		tokenizer.get_output_line(line_idx_from + 1).insert_token(1, Token.new(Token.Type.KEYWORD, func_body, 0, line_idx_from + 1))
		while line_idx < tokenizer.get_output_line_count():
			var tline : Tokenizer.Line = tokenizer.get_output_line(line_idx)
			if tline.has_statement():
				if tline.get_identation() <= identation:
					break
				last_valid = line_idx
			if tline.tokens:
				tokenizer.seek_token(tline.tokens[0])
			line_idx += 1
		if last_valid != -1:
			for l in range(line_idx_from + 2, last_valid + 1):
				tokenizer.get_output_line(l).clear_tokens()


func _shuffle_toplevel() -> void:
	var lines : Array[Tokenizer.Line] = tokenizer.get_output_lines()
	var top_block : Array
	var on_ready : Array[Array] = []
	var blocks : Array[Array] = []
	var current_block : Array
	var current_is_onready : bool
	
	var add_block = func(block : Array, is_onready : bool):
		if block.is_empty(): return
		if is_onready:
			on_ready.append(block)
		else:
			blocks.append(block)
	
	for i in lines.size():
		var line : Tokenizer.Line = lines[i]
		var prev_line : Tokenizer.Line = lines[i - 1] if i >= 1 else null
		var starter_token : Token = line.tokens[0] if line.tokens else null
		var prev_starter_token : Token = prev_line.tokens[0] if prev_line and prev_line.tokens else null
		if starter_token and ["@icon", "@tool", "class_name", "extends"].has(starter_token.get_value()):
			top_block.append(line)
			continue
		if starter_token and starter_token.get_value() == "@onready":
			add_block.call(current_block, current_is_onready)
			current_block = []
			current_is_onready = true
		elif line.get_identation() == 0 and starter_token and starter_token.is_keyword() and (!prev_starter_token or (!prev_starter_token.has_value("@rpc") and !(prev_starter_token.get_value().begins_with("@export") and !prev_line.has_token_value("var")))):
			add_block.call(current_block, current_is_onready)
			current_block = []
			current_is_onready = false
		
		current_block.append(line)
	
	add_block.call(current_block, current_is_onready)
	
	var w_blocks : Dictionary
	var random := RandomNumberGenerator.new()
	var line_seeds : Dictionary
	for block in blocks:
		var line_seed : int = 0
		if block:
			line_seed = hash(block[0].to_string())
			line_seeds[line_seed] = line_seeds.get(line_seed, -1) + 1
			line_seed += line_seeds[line_seed]
		random.seed = hash(path) + path.length() + _symbol_table._seed + line_seed
		w_blocks[block] = random.randi()
	blocks.sort_custom((func(a, b): return w_blocks[a] > w_blocks[b]))
	
	if on_ready:
		var idx : int = 0
		var max_spacing : int = mini(blocks.size() / on_ready.size() * 2, blocks.size() + 1)
		random.seed = hash(path) + path.length() + _symbol_table._seed
		for block in on_ready:
			if max_spacing:
				idx += maxi(1, random.randi() % max_spacing)
			blocks.insert(mini(idx, blocks.size()), block)
	
	lines.clear()
	for block in [top_block] + blocks:
		lines.append_array(block)


func _strip_code() -> void:
	var regex := RegEx.new()
	if _Settings.current.regex_filter_enabled and _Settings.current.regex_filter:
		regex.compile(_Settings.current.regex_filter)
	
	var lines : Array[Tokenizer.Line] = tokenizer.get_output_lines()
	for l in range(lines.size() - 1, -1, -1):
		var line : Tokenizer.Line = lines[l]
		
		if _Settings.current.strip_comments or _Settings.current.strip_extraneous_spacing:
			for i in range(line.tokens.size() - 1, -1, -1):
				var token : Token = line.tokens[i]
				
				# Strip comments
				if _Settings.current.strip_comments and token.type == Token.Type.COMMENT:
					line.remove_token(i)
					continue
				
				# Strip extraneous spacing
				if _Settings.current.strip_extraneous_spacing:
					if token.type == Token.Type.IDENTATION and (i == line.tokens.size()-1 or line.tokens[i+1].type == Token.Type.LINE_BREAK):
						line.remove_token(i)
						continue
					elif token.type == Token.Type.WHITESPACE:
						var prev_type : int = line.tokens[i-1].type
						var next_type : int = line.tokens[i+1].type if i+1 < line.tokens.size() else Token.Type.NONE
						if i == 0 or prev_type == Token.Type.OPERATOR or prev_type == Token.Type.PUNCTUATOR or next_type == Token.Type.OPERATOR or next_type == Token.Type.PUNCTUATOR:
							line.remove_token(i)
							continue
		
		# Strip empty lines
		if _Settings.current.strip_empty_lines and str(line).replace(" ", "").replace("\n", "").replace("\t", "").replace(";", "").is_empty():
			tokenizer.remove_output_line(l)
			continue
		
		# Strip lines matching RegEx
		if regex.is_valid() and regex.search(str(line)):
			tokenizer.remove_output_line(l)
			continue
