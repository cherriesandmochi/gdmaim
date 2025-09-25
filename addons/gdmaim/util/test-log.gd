@tool
extends Node

const LogSystem = preload("res://addons/gdmaim/util/log_system.gd")

func _ready() -> void:
	LogSystem.clear()
	
	success_test()
	undone_error_test()
	manual_error_test()
	stack_overflow_error_test()

func success_test() -> void:
	var file_path : String = "res://my_file_path.gd"
	
	var log_file : LogSystem.LogFile = LogSystem.LogFile.new(file_path)

	#region TEST_1
	# Start Action
	log_file.start_track("Succes Action 1")
	
	# Done Action
	log_file.done_track()
	#endregion
	
	#region TEST_2
	# Start Action
	log_file.start_track("Succes Action 2")
	
	# Done Action
	log_file.done_track()
	
	log_file.done_file()
	#endregion
	
	print("\n===== RESULT =====\n",log_file)
	
func undone_error_test() -> void:
	var file_path : String = "res://my_file_path.gd"
	
	var log_file : LogSystem.LogFile = LogSystem.LogFile.new(file_path)

	#region TEST_1
	# Start Action
	log_file.start_track("Succes Action")
	
	# Done Action
	log_file.done_track()
	#endregion
	
	#region TEST_2
	# Start Action
	log_file.start_track("Never Done Action")
	
	## undone track!
	
	log_file.done_file()
	#endregion
	
	print("\n===== RESULT =====\n",log_file)

func manual_error_test() -> void:
	var file_path : String = "res://my_file_path.gd"
	
	var log_file : LogSystem.LogFile = LogSystem.LogFile.new(file_path)

	#region TEST_1
	# Start Action
	log_file.start_track("Succes Action")
	
	# Done Action
	log_file.done_track()
	#endregion
	
	#region TEST_2
	# Start Action
	log_file.start_track("Manual Error Action")
	
	# Manual Error
	log_file.error_track()
	
	log_file.done_file()
	#endregion
	
	print("\n===== RESULT =====\n",log_file)
	
func stack_overflow_error_test() -> void:
	var file_path : String = "res://my_file_path.gd"
	
	var log_file : LogSystem.LogFile = LogSystem.LogFile.new(file_path)

	#region TEST_1
	# Start Action
	log_file.start_track("Succes Action")
	
	# Done Action
	log_file.done_track()
	#endregion
	
	#region TEST_2
	# Start Action
	log_file.start_track("Manual Error Action")
	
	# Manual Error
	log_file.error_track()
	#endregion
	
	#region TEST_3
	# Start
	log_file.start_track("Stack Overflow Action", 3000) # wait 3 seconds
	
	while true:
		if log_file.is_track_time_out():
			break
	#endregion
	
	log_file.done_file()
	print("\n===== RESULT =====\n",log_file)
