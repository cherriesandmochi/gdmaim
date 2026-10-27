@tool
extends RefCounted


static var current_log : Variant
static var logs : Dictionary = {}


static func clear_all() -> void:
	logs.clear()


static func swap(new_log : Variant) -> void:
	current_log = new_log
	logs[new_log] = logs.get(new_log, "")


static func clear() -> void:
	logs[current_log] = ""


static func write(new_text : String) -> void:
	logs[current_log] += new_text + "\n"


static func forget(target : Variant) -> void:
	logs.erase(target)
	if is_same(current_log, target):
		current_log = null


static func get_log(target : Variant) -> String:
	return logs[target].trim_suffix("\n")


static func get_current() -> String:
	return logs[current_log].trim_suffix("\n")
