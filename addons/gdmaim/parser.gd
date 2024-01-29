extends RefCounted


var code : String
var idx : int = -1
var last_char : String
var tweens : PackedStringArray


func _init(code : String) -> void:
	self.code = code


func has_finished() -> bool:
	return idx >= code.length()


func reset() -> void:
	idx = -1


func read_char() -> String:
	idx += 1
	if idx < code.length():
		last_char = code[idx]
		return last_char
	else:
		last_char = ""
		return ""


func read_until(delimeters : String) -> String:
	var out : String
	
	while read_char():
		if delimeters.find(last_char) != -1:
			break
		out += last_char
	
	return out


func get_tokens() -> PackedStringArray:
	var tokens : PackedStringArray
	
	reset()
	tweens.clear()
	var cur_tween : String
	while !has_finished():
		var token : String = read_until(" ;,\n\t(:.+-*/%)[]{}'<=>|!#\"\\")
		if token:
			tokens.append(token)
			tweens.append(cur_tween)
			cur_tween = ""
		if last_char:
			if last_char != " " and last_char != "\n" and last_char != "\t":
				tokens.append(last_char)
				tweens.append(cur_tween)
				cur_tween = ""
				if last_char == "'" or last_char == '"':
					var str_end : String = last_char
					var str : String
					while read_char():
						if last_char == str_end:
							tokens.append(str)
							tweens.append("")
							tokens.append(last_char)
							tweens.append("")
							break
						else:
							str += last_char
							if last_char == "\\":
								str += read_char()
			else:
				cur_tween += last_char
	
	if cur_tween:
		tweens.append(cur_tween)
	
	return tokens


func get_identation() -> int:
	var identation : int
	
	reset()
	read_char()
	while !has_finished():
		if last_char == " " or last_char == "\t":
			identation += 1
		else:
			break
		read_char()
	
	return identation


func read_string() -> String:
	while read_char():
		if last_char == "'" or last_char == '"':
			var str : String
			var str_end : String = last_char
			while read_char():
				if last_char == str_end:
					return str
				str += last_char
				if last_char == "\\":
					str += read_char()
	return ""


static func read_assignment(lines : PackedStringArray, line_idx : int, token_idx : int, allow_containers : bool = false) -> Dictionary:
	var data : Dictionary = {
		"value": "",
		"from": [0, 0],
		"to": [0, 0],
	}
	
	var parser := preload("parser.gd").new(lines[line_idx])
	var tokens : PackedStringArray = parser.get_tokens()
	
	var t : int = token_idx
	while t < tokens.size():
		if tokens[t] == "=":
			t += 1
			break
		t += 1
	
	if !allow_containers and t < tokens.size() and "{[".contains(tokens[t]):
		return data
	
	data["from"] = [line_idx, t]
	
	var depth : int = 0
	var l : int = line_idx
	while l < lines.size():
		parser.code = lines[l]
		tokens = parser.get_tokens()
		while t < tokens.size():
			var token : String = tokens[t]
			if token == "#" or token == ";":
				break
			else:
				data["value"] += token
				if "([{".contains(token):
					depth += 1
				elif ")]}".contains(token):
					depth -= 1
			t += 1
		if depth <= 0:
			break
		t = 0
		l += 1
	
	data["to"] = [l, t]
	
	return data
