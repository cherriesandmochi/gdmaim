extends RefCounted


var _data : String
var _idx : int = -1
var _line : int = 1
var _col : int = 0


func _init(characters : String) -> void:
	_data = characters


func get_next() -> String:
	_idx += 1
	if _data[_idx] == "\n":
		_line += 1
		_col = 0
	else:
		_col += 1
	return _data[_idx] if _idx < _data.length() else ""


func peek(offset : int = 1) -> String:
	return _data[_idx + offset] if _idx + offset < _data.length() else ""


func is_eof() -> bool:
	return _idx + 1 >= _data.length()


func get_line_pos() -> int:
	return _line


func get_column_pos() -> int:
	return _col


func get_pos_str() -> String:
	return "line: " + str(_line) + ", column: " + str(_col) + ")"
