extends RefCounted


const _Logger := preload("../../logger.gd")
const _Settings := preload("../../settings.gd")
const SymbolTable := preload("../symbol_table.gd")

var path : String

var _source_data : String
var _data : String


func _init(path : String) -> void:
	self.path = path


func run(source_data : String, symbol_table : SymbolTable) -> bool:
	_source_data = source_data
	_data = ""
	
	var lines : PackedStringArray = source_data.split("\n")
	var i : int = 0
	while i < lines.size():
		var line : String = lines[i]
		if line.begins_with("\""):
			_data += line + "\n"
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
					var new_symbol : SymbolTable.Symbol = symbol_table.find_global_symbol(name)
					if new_symbol:
						line = _replace_first(line, name, str(new_symbol.name))
						_Logger.write(str(i+1) + " found symbol '" + name + "' = " + str(new_symbol.name))
		
		_data += line + "\n"
		i += 1
		
		if line.begins_with("[node") or line.begins_with("[sub_resource") or line.begins_with("[resource"):
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
					_Logger.write(str(i+1) + " found script " + line + " " + tokens[1])
				
				j += 1
			
			if !has_script:
				_data += tmp_lines
				i = j
			else:
				j = mini(j, lines.size())
				while i < j:
					line = lines[i]
					var tokens : PackedStringArray = line.split(" = ", false, 1)
					if tokens.size() == 2:
						if tokens[1].begins_with("NodePath(") and tokens[1].contains(":"):
							var node_path : String = _read_string(tokens[1])
							var properties : PackedStringArray = node_path.split(":", false)
							var new_path : String = properties[0]
							for property in properties.slice(1):
								var new_symbol : SymbolTable.Symbol = symbol_table.find_global_symbol(property)
								new_path += ":" + (str(new_symbol.name) if new_symbol else property)
							tokens[1] = 'NodePath("' + new_path + '")'
							line = tokens[0] + " = " + tokens[1]
							if node_path != new_path:
								_Logger.write(str(i+1) + " found node path '" + node_path + "' = " + new_path)
						
						var new_symbol : SymbolTable.Symbol = symbol_table.find_global_symbol(tokens[0])
						if new_symbol:
							line = str(new_symbol.name) + " = " + tokens[1]
							_Logger.write(str(i+1) + " found export var '" + tokens[0] + "' = " + str(new_symbol.name))
					elif line.begins_with('"method":'):
						var method : String = _read_string(line.trim_prefix('"method":'))
						var new_symbol : SymbolTable.Symbol = symbol_table.find_global_symbol(method)
						if new_symbol:
							line = '"method": &"' + str(new_symbol.name) + '"'
							_Logger.write(str(i+1) + " found method '" + method + "' = " + str(new_symbol.name))
					
					_data += line + "\n"
					i += 1
	
	_data = _data.strip_edges(false, true) + "\n"
	
	return true


func get_source_data() -> String:
	return _source_data


func get_data() -> String:
	return _data


func set_data(custom : String) -> void:
	_data = custom


func _replace_first(str : String, replace : String, with : String) -> String:
	var idx : int = str.find(replace)
	if idx == -1:
		return str
	elif idx == 0:
		return with + str.substr(idx + replace.length())
	else:
		return str.substr(0, idx) + with + str.substr(idx + replace.length())


func _read_string(input : String) -> String:
	var out : String
	var str_end : String
	for char in input:
		if char == "'" or char == '"':
			if str_end:
				return out
			str_end = char
		elif str_end:
			out += char
	return out
