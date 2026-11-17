@tool
extends RefCounted
# # # # # # # # # # # # # # # # # # # # 
# [EXPERIMENTAL]
# HUMAN HERE: https://github.com/CodeNameTwister
# # # # # # # # # # # # # # # # # # # # # # 

const _Environment = preload("environment.gd")
const _GEN : String = "res://.godot/grit/generated"
const _TOOL : String = "res://addons/gdmaim/thirdparty/grit/tools/grit.py"
const _ENGINE : String = "res://addons/gdmaim/thirdparty/godot-4.7"
const _CHUNK_SIZE : int = 4096

var debug : bool = false
var _binary : String = ""

func sort(data: Dictionary) -> PackedStringArray:
	var children : Dictionary = {}
	var roots : Array = []
	var result : PackedStringArray = []
	
	for current in data:
		var parent_class : String = data[current]
		
		if parent_class == "" or parent_class == null or not data.has(parent_class):
			if current not in roots:
				roots.append(current)
		else:
			if not children.has(parent_class):
				children[parent_class] = []
			children[parent_class].append(current)
	
	for root in roots:
		_traverse_dfs(root, children, result)
		
	return result

func clear_folder(path: String) -> void:
	var dir : DirAccess = DirAccess.open(path)
	
	if dir:
		dir.list_dir_begin()
		
		var file : String = dir.get_next()
		while !file.is_empty():
			if file != "." and file != "..":
				var full : String = path.path_join(file)
				if dir.current_is_dir():
					var sub_dir = DirAccess.open(full)
					if sub_dir:
						clear_folder(full)
						dir.remove(file)
				else:
					dir.remove(file)
			file = dir.get_next()
		dir.list_dir_end()
	else:
		print("[AOT] Error on trying clear path: ", path)

func _traverse_dfs(node: String, children: Dictionary, result: PackedStringArray) -> void:
	result.append(node)
	if children.has(node):
		for child in children[node]:
			_traverse_dfs(child, children, result)
	
func _get_hash(path: String, type = HashingContext.HASH_SHA256) -> String:
	var file : FileAccess = FileAccess.open(path, FileAccess.READ)
	if file == null:
		printerr("Can not read {0}".format([path]))
		return ""
		
	var ctx : HashingContext = HashingContext.new()
	ctx.start(type)
	
	while not file.eof_reached():
		var chunk : PackedByteArray = file.get_buffer(_CHUNK_SIZE)
		if chunk.size() > 0:
			ctx.update(chunk)
	
	return ctx.finish().hex_encode()

func _define_arguments(custom_args : Dictionary) -> String:
	var engine_dir : String = _ENGINE
	_binary = ""
	
	if custom_args.has("engine"):
		engine_dir = custom_args["engine"]
	else:
		custom_args["engine"] = engine_dir
		
	if !custom_args.has("engine_build"):
		custom_args["engine_build"] = engine_dir.path_join("template")
		
	return "{0}".format([
		" --engine {0}".format([ProjectSettings.globalize_path(engine_dir)])
		])

func get_build() -> String:
	return _binary
	
func is_equal(origin : String, target : String, type = HashingContext.HASH_SHA256) -> bool:
	if FileAccess.file_exists(origin) and FileAccess.file_exists(target):
		return _get_hash(origin) == _get_hash(target)
	return false
	
func get_editor(root : String = "", custom_args : Dictionary = {}, env : _Environment= null) -> String:
	if root.is_empty():
		root = ProjectSettings.globalize_path(_TOOL)
		
	if env == null:
		env = _Environment.new()
	
	_define_arguments(custom_args)
	var output : String  = _get_template(env, root, "editor",  custom_args["engine"])
		
	if !FileAccess.file_exists(output):
		printerr("[ERROR] Can not get Editor, check errors using settings [AOT Debug Enabled] before make a issue!")
		output = ""
		
	return output
	
func wizard(custom_args : Dictionary = {}) -> void:
	var env : _Environment = _Environment.new()
	var info : Array = env.check_python()
	_binary = ""
	
	if info[0] != true:
		printerr("[ERROR] Not has valid python version!\ncurrent python version {0}".format([info[1]]))
		return
		
	print("[OK] Python version found {0}".format([info[1]]))

	if !FileAccess.file_exists(_TOOL):
		printerr("[ERROR] Not grit tool found!")
		return
		
	print("[OK] Grit tool found!")
	
	
	var args : String = _define_arguments(custom_args)
	var root : String = ProjectSettings.globalize_path(_TOOL)
	
	env.debug = custom_args.has("debug") and custom_args["debug"] == true
	
	info = env.run_py("{0} {1} {2}".format([root, args, "doctor"]))
	
	var arr : Array = info
	var desc : String = ""
	
	if arr.size() > 1:
		if arr[1] is Array:
			for x in arr[1]:
				var _str : String = str(x).replace("\\r\\n","\n").strip_edges()
				if _str.is_empty():
					continue
				desc += str(x)
		else:
			desc = str(arr[1]).replace("\\r\\n","\n").trim_prefix("[\"").trim_suffix("\"]")
	
	if info[0] != OK:
		print("## STATUS ##\n{0}".format([desc]))
		
		#Pray to rescue this dev \o/ |o| /o\ |o| \o/
		var rgx : RegEx = RegEx.create_from_string("(?m)\\(run:\\s*grit\\s*(.*)\\)", false)
		
		if rgx:
			var cmds : PackedStringArray = []
			for mt : RegExMatch in rgx.search_all(desc):
				var cmd : String = mt.get_string(1)
				if !cmd.is_empty():
					cmds.append(cmd)
					
			if cmds.size() > 0:
				print("## Setup environment ##")
				for cmd : String in cmds:
					info = env.run_py("{0} {1} {2}".format([root, args, cmd]), true)
					if info[0] == 0:
						continue
					elif cmd.begins_with("build"):
						if FileAccess.file_exists(get_editor(root, custom_args, env)):
							continue
					printerr("[ERROR] An error on try execute command \"{0}\", Please check errors using [AOT Debug Enabled] before contact GDMaim support".format([cmd]))
					return
					
				var dbuild : String = get_editor(root, custom_args, env)
				if dbuild.is_empty():
					print("[WARN] Check again the editor build!")
					return
				else:
					print("## Okey editor found, lets compile this ##")
				
	if custom_args.has("build_template") and custom_args["build_template"] == true:
		var project : String = ProjectSettings.globalize_path("res://")
		var target : String = project.path_join("bin")
		var template : String = "Windows Desktop"
		
		if custom_args.has("target"):
			target = custom_args["target"]
			
		if custom_args.has("template"):
			template = custom_args["template"]
			
		if custom_args.has("project"):
			project = custom_args["project"]
		
		args = ""
		
		var gen : String = "\"\""
		if !custom_args.has("dummy_template") or custom_args["dummy_template"] == false:
			if custom_args.has("generated"):
				gen = ProjectSettings.globalize_path(custom_args["generated"])
			else:
				gen = ProjectSettings.globalize_path(_GEN)
		
		args += " --generated {0}".format([gen])
			
		var template_build : String = "template_release"
		
		#if custom_args.has("debug") and custom_args["debug"] == true:
			#template_build = "template_debug"
		
		var output : String = _get_template(env, root, template_build, custom_args["engine"])
		if FileAccess.file_exists(output):
			DirAccess.remove_absolute(output)
			print("Cleaned '{0}' build".format([output.get_file()]))
		
		info = env.run_py("{0} {1} {2} {3}".format([root, "build", template_build, args]), true)
		output = _get_template(env, root, template_build, custom_args["engine"])
		
		if !FileAccess.file_exists(output):
			printerr("[ERROR] Can not compile template, check errors using settings [AOT Debug Enabled] before make a issue!")
			return
		
		print("## TEMPLATE DONE ##")
		_binary = output

func _get_template(env : _Environment, root : String, template_build : String,  engine : String) -> String:
	var info : Array = env.run_py("{0} {1} --target {2} --engine {3}".format([root, "template", template_build, ProjectSettings.globalize_path(engine)]), false)
	var result : Variant = info[1]
	var output : String = ""
	
	if result is Array or result is PackedStringArray:
		if result.size() > 0:
			output = str(result[0])
	else:
		output = str(result)
	output = output.replace("\r\n", "")
	
	print("TEMPLATE AT ", output)
	
	return output
