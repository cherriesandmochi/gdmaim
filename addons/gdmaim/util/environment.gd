extends RefCounted
const PYTHON_MAYOR_VERSION : int = 3
const PYTHON_MINOR_VERSION : int = 9

var debug : bool = false
var _valid_py : String = ""

func run_py(command : String, open_console : bool = false) -> Array:
	var output : Array = []
	var version : String = ""
	var os_str = OS.get_name()
	var exit : int = -1
	var cmd : String = "python"
	
	if _valid_py.is_empty():
		check_python()
	
	if !_valid_py.is_empty():
		cmd = _valid_py
	
	if os_str == "Windows":
		output.clear()
		
		if cmd == "py":
			cmd += " -3"
			
		if open_console:
			if debug:
				exit = OS.execute("cmd.exe", ["/c", "start", "/wait", "cmd.exe", "/k", "echo \"Executing command\" & echo [{1}] & echo - & echo \"Please wait...\" & {0} {1} & pause & exit 0".format([cmd, command])], output, true) == 0
			else:
				exit = OS.execute("cmd.exe", ["/c", "start", "/wait", "cmd.exe", "/k", "echo \"Executing command\" & echo [{1}] & echo - & echo \"Please wait...\" & {0} {1} & exit 0".format([cmd, command])], output, true) == 0
		else:
			exit = OS.execute("cmd.exe", ["/c", "{0} {1}".format([cmd, command])], output, true)
			
		if output.size() < 1:
			if exit == 0:
				exit = -1
		exit = -1
	else:
		output.clear()
		
		if debug:
			command += " & read -n 1 -s -r -p \"Press any button to continue\" & exit 0"
		else:
			command += " & exit 0"
			
		exit = OS.execute(cmd, [command], output, true, open_console) == 0 and output.size() > 0
			
		if output.size() < 1:
			if exit == 0:
				exit = -1
				
	return [exit, output]

func check_python() -> Array:
	var cmds : PackedStringArray = ["python3", "python", "py"]
	var output : Array = []
	var version : String = ""
	var os_str = OS.get_name()
	
	if os_str == "Windows":
		for cmd : String in cmds:
			output.clear()
			
			if cmd == "py":
				cmd += " -3"
			
			if OS.execute("cmd.exe", ["/c", cmd + " --version"], output, true, false) == 0 and output.size() > 0:
				version = output[0]
				_valid_py = cmd
				break
			
	else:
		for cmd : String in cmds:
			output.clear()
			
			if OS.execute(cmd, ["--version"], output, true, false) == 0 and output.size() > 0:
				version = output[0]
				_valid_py = cmd
				break
				
	var py_on : bool = false
	var py_ver : String = "unknow"
	
	if !version.is_empty():
		var clean_line : String = version.strip_edges()
		var parts : PackedStringArray = clean_line.split(" ")
		
		if parts.size() >= 2:
			var version_numbers : PackedStringArray = parts[1].split(".")
			if version_numbers.size() >= 2:
				var major : int = version_numbers[0].to_int()
				var minor : int = version_numbers[1].to_int()
				
				if major == PYTHON_MAYOR_VERSION and minor >= PYTHON_MINOR_VERSION:
					py_on = true
				
				py_ver = str(version_numbers[0], ".", version_numbers[1])
				
				if version_numbers.size() > 2:
					for x : int in range(2, version_numbers.size(), 1):
						var out : String = ""
						var _str : String = version_numbers[x]
						
						for z : int in _str.length():
							var chr : String = _str[z]
							if chr.is_valid_int():
								out += chr
								
						if out.is_empty():
							break
							
						py_ver = str(py_ver, ".", out)
					
			elif version_numbers.size() == 1:
				py_ver = version_numbers[0]
				
	return [py_on, py_ver]
