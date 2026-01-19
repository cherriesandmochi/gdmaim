class_name GDMaimLog extends RefCounted

const DEFAULT_MAX_TIME_OUT : int = 30000
const DEFAULT_LOGS_PATH : String = "res://addons/gdmaim/logs"

static var LOG_ONLY_ERROR_OR_EXCEEDED_SUCESS : bool = true

static func add_log(log_data : String, base_dir : String = DEFAULT_LOGS_PATH) -> void:
	var dir : String = base_dir
	var file : String = dir.path_join("log_file.log")
	var fl : FileAccess = FileAccess.open(file, FileAccess.READ_WRITE)
	if !fl:
		push_error("Can not open log file!")
		return
	fl.seek(fl.get_length())
	fl.store_string("\n\n"+log_data)
	fl.close()

static func clear(base_dir : String = DEFAULT_LOGS_PATH) -> void:
	var dir : String = base_dir
	var file : String = dir.path_join("log_file.log")
	
	if !DirAccess.dir_exists_absolute(dir):
		DirAccess.make_dir_recursive_absolute(dir)
	
	var fl : FileAccess = FileAccess.open(file, FileAccess.WRITE)
	if !fl:
		push_error("Can not create/clear log file!")
		return
	fl.store_string("[LOG FILE]")
	fl.close()

class LogTrack extends RefCounted:
	enum ERR_TRACK{
		PASSED,
		PASSED_EXCEEDED,
		TIME_OUT,
		NOT_PASSED
	}
	
	var _track_name : String = ""
	var _time_in : int = 0
	var _time_out : int = 0
	var _time_expected : int = 0
	var _result : ERR_TRACK = ERR_TRACK.NOT_PASSED
	
	func _init(track_name : String, expected_time : int) -> void:
		_track_name = track_name
		_time_expected = expected_time
		_time_in = Time.get_ticks_msec()
		_time_out = _time_in
		
	func is_track_passed() -> bool:
		return _result == ERR_TRACK.PASSED

	func _to_string() -> String:
		return '[{0}] {1} - {2} ms'.format([ERR_TRACK.keys()[_result], _track_name, _time_out - _time_in])

	func name() -> String:
		return _track_name

	func done() -> void:
		if _time_in + _time_expected < Time.get_ticks_msec():
			_result = ERR_TRACK.PASSED_EXCEEDED
		else:
			_result = ERR_TRACK.PASSED
		_time_out = Time.get_ticks_msec()
		
	func is_time_out() -> bool:
		if _time_in + _time_expected < Time.get_ticks_msec():
			stack_overflow()
			return true
		return false
		
	func stack_overflow() -> void:
		_result = ERR_TRACK.TIME_OUT
		_time_out = Time.get_ticks_msec()
		
	func error() -> void:
		_result = ERR_TRACK.NOT_PASSED
		_time_out = Time.get_ticks_msec()

class LogFile extends RefCounted:
	var _log_tracks : Array[LogTrack] = []
	
	func _init(file_path : String, expected_time : int = DEFAULT_MAX_TIME_OUT) -> void:
		_log_tracks.append(LogTrack.new(file_path, expected_time))
				
	func start_track(track_name : String, expected_time : int = DEFAULT_MAX_TIME_OUT) -> void:
		var log_track : LogTrack = LogTrack.new(track_name, expected_time)
		_log_tracks.append(log_track)
	
	func get_last_track() -> LogTrack:
		return _log_tracks[_log_tracks.size() - 1]
	
	func is_track_time_out() -> bool:
		return get_last_track().is_time_out()
		
	func is_file_time_out() -> bool:
		return _log_tracks[0].is_time_out()
		
	func get_track(track_name : String) -> LogTrack:
		for index : int in range(_log_tracks.size() - 1, -1, -1):
			var log_track : LogTrack = _log_tracks[index]
			if log_track.name() == track_name:
				return log_track
		return null
		
	func error_track() -> void:
		get_last_track().error()
		
	func done_track() -> void:
		get_last_track().done()
		
	func done_file() -> void:
		var err : bool = false
		for x : int in range(1, _log_tracks.size(), 1):
			if !_log_tracks[x].is_track_passed():
				err = true
				break
		if err:
			_log_tracks[0].error()
			GDMaimLog.add_log(str(self))
		else:
			_log_tracks[0].done()
			if !GDMaimLog.LOG_ONLY_ERROR_OR_EXCEEDED_SUCESS:
				GDMaimLog.add_log(str(self))
			
	func _to_string() -> String:
		var st : String = ""
		for l : int in range(1, _log_tracks.size(), 1):
			st += str("\n\t", _log_tracks[l])
		return '[FILE] {0}{1}'.format([_log_tracks[0], st])
