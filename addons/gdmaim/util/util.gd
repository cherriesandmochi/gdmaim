@tool
extends Object
## util.gd
## GDMaim Util General Helper Purposes 

# I'll drop some useful junk in here.
# - Twitser

const PATH : String = "res://.gdmaim"
const CUSTOM_USER_BUILT : String = "custom.txt"

static func f12_please() -> String:
	return """#GDMaim User Custom Setting

custom.txt\t|\tAllow you define you custom token for prevent obfuscate.

===============================================
custom.txt@Example: write you token like a list
-----------------------------------------------
my_property
my_another_property
my_function
my_another_special_function	
===============================================
"""
	# T0D0

static func get_custom_user_tokens() -> PackedStringArray:
	var out : PackedStringArray = []
	if FileAccess.file_exists(PATH.path_join(CUSTOM_USER_BUILT)):
		var packed : PackedStringArray = FileAccess.get_file_as_string(PATH.path_join(CUSTOM_USER_BUILT)).split("\n")
		
		for token : String in packed:
			token = token.strip_edges()
			if token.is_empty() or token.begins_with("#"):
				continue
			out.append(token)
			
	return out

static func initialize() -> void:
	if !DirAccess.dir_exists_absolute(PATH):
		DirAccess.make_dir_recursive_absolute(PATH)
		
	if !FileAccess.file_exists(PATH.path_join(".gdignore")):
		var file : FileAccess = FileAccess.open(PATH.path_join(".gdignore"), FileAccess.WRITE)
		file.store_string("#GDMaim Util Helper Resource Folder")
		file.close()
		
	if !FileAccess.file_exists(PATH.path_join("README.txt")):
		var file : FileAccess = FileAccess.open(PATH.path_join("README.txt"), FileAccess.WRITE)
		file.store_string(f12_please())
		file.close()

	if !FileAccess.file_exists(PATH.path_join(CUSTOM_USER_BUILT)):
		var file : FileAccess = FileAccess.open(PATH.path_join(CUSTOM_USER_BUILT), FileAccess.WRITE)
		file.store_string("# Store here you custom tokens!\n")
		file.close()
