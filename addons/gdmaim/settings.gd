extends RefCounted


const _Settings := preload("settings.gd")

enum GDScriptExportMode {
	TEXT = 0,
	BINARY,
	COMPRESSED,
}

var obfuscation_enabled : bool = true
var shuffle_top_level : bool = true
var inline_constants : bool = true
var inline_enums : bool = true
var obfuscate_export_vars : bool = true
var obfuscate_signals : bool = true
var symbol_target_length : int = 4
var symbol_prefix : String = "__"
var symbol_characters : String = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_"
var symbol_seed : int = 0
var symbol_dynamic_seed : bool = false
var strip_comments : bool = true
var strip_empty_lines : bool = true
var strip_extraneous_spacing : bool = true
var feature_filters : bool = true
var regex_filter_enabled : bool = true
var regex_filter : String = ""
var preprocessor_prefix : String = "##"
var source_map_path : String = get_script().resource_path.get_base_dir() + "/source_maps"
var source_map_max_files : int = 10
var source_map_compress : bool = true
var source_map_inject_name : bool = true
var debug_scripts : PackedStringArray
var debug_resources : PackedStringArray
var obfuscate_debug_only : bool = false
var export_mode : int = GDScriptExportMode.TEXT

var _cfg := ConfigFile.new()
var _entries : Dictionary
var _categories : Array[Category]


static var current : _Settings


func _init() -> void:
	current = self
	
	set_category("obfuscator", "Obfuscation")
	add_entry("obfuscation_enabled", "enabled", "Enable Obfuscation", "If false, skip obfuscation entirely, but still allow post-processing to take place.")
	add_entry("obfuscate_export_vars", "export_vars", "Obfuscate Export Vars", "If true, obfuscate export variables.\nNote: Requires scenes and resources which modify custom export vars to be saved as '*.tscn' and '*.tres', respectively.")
	#add_entry("obfuscate_signals", "signals", "Obfuscate Signals", "If true, obfuscate signals.")
	add_entry("shuffle_top_level", "shuffle_top_level", "Shuffle Top-Level Declarations", "If true, shuffles all top-level declarations of variables, functions, signals, etc.")
	add_entry("inline_constants", "inline_consts", "Inline Constants", "If true, replace constants with hardcoded values.\nNote: Only bool, int, float, Color, Vector(2/3/4)(i) and NodePath are supported.").disabled = true
	add_entry("inline_enums", "inline_enums", "Inline Enums", "If true, replace enums with hardcoded values.").disabled = true
	add_entry("preprocessor_prefix", "preprocessor_prefix", "Preprocessor Prefix", "Sets the prefix to use for preprocessor hints.")
	
	set_category("post_process", "Post Processing")
	add_entry("strip_comments", "strip_comments", "Strip Comments", "If true, remove all comments.")
	add_entry("strip_empty_lines", "strip_empty_lines", "Strip Empty Lines", "If true, remove all empty lines.")
	add_entry("strip_extraneous_spacing", "strip_extraneous_spacing", "Strip Extraneous Spacing", "If true, remove all irrelevant spaces and tabs.")
	add_entry("regex_filter_enabled", "regex_filter_enabled", "Strip Lines Matching RegEx", "If true, any lines matching the regular expression will be removed from the obfuscated code.")
	add_entry("regex_filter", "regex_filter", "", "Enter Regular Expression")
	add_entry("feature_filters", "feature_filters", "Process Feature Filters", "If true, export template feature tags may be used to filter code.")
	add_entry("export_mode", "export_mode", "Export Mode", "Sets the exported script format.\nText: easier debugging\nBinary: faster loading\nCompressed: smaller files").set_custom_type(Entry.CustomType.OPTIONS, ["Text", "Binary", "Compressed"])
	
	set_category("id", "Name Generator")
	add_entry("symbol_prefix", "prefix", "Prefix", "Sets the prefix to use for all generated names.")
	add_entry("symbol_characters", "character_list", "Character List", "A list of characters which the obfuscator will pick from, when generating names.")
	add_entry("symbol_target_length", "target_length", "Target Name Length", "Sets the name length, excluding prefix, which the obfuscator tries to target when generating names.")
	add_entry("symbol_seed", "seed", "Seed", "Sets the seed to use to generate names. A seed will always generate the same name for a given symbol.\nNote: 'Use Dynamic Seed' overrides this setting.")
	add_entry("symbol_dynamic_seed", "dynamic_seed", "Use Dynamic Seed", "If true, generate an unique seed on every export.\nNote: Overrides 'Seed'.\nNot recommended as it might negatively affect delta updates.")
	
	set_category("source_mapping", "Source Mapping")
	add_entry("source_map_path", "filepath", "Output Path", "Source maps will get saved to this path upon export.")
	add_entry("source_map_max_files", "max_files", "Max Files", "Sets the maximum amount of source map files allowed.")
	add_entry("source_map_compress", "compress", "Compress", "If true, source maps will be compressed upon export.")
	add_entry("source_map_inject_name", "inject_name", "Inject Name", "If true, upon export, injects a print statement of the associated source map's filename into the first enabled autoload. This does not affect your source code. Makes selecting the right source map very easy, when a player/user reports an error and shares their logfile.")
	
	#set_category("debug", "Debug")
	#add_entry("debug_scripts", debug_scripts", "", "")
	#add_entry("debug_resources", debug_resources", "", "")
	#add_entry("obfuscate_debug_only", "obfuscate_debug_only", "", "")
	
	deserialize()


func set_category(cfg_region : String, visible_name : String) -> void:
	_categories.append(Category.new(cfg_region, visible_name))


func add_entry(var_name : String, cfg_key : String, visible_name : String, tooltip : String) -> Entry:
	_entries[var_name] = Entry.new(var_name, _categories.back().cfg_region, cfg_key, visible_name, tooltip)
	_categories.back().entries.append(_entries[var_name])
	_cfg.set_value(_categories.back().cfg_region, cfg_key, get(var_name))
	return _entries[var_name]


func serialize() -> void:
	_write_entries()
	if !DirAccess.dir_exists_absolute(_get_cfg_dir()):
		DirAccess.make_dir_recursive_absolute(_get_cfg_dir())
	_cfg.save(_get_cfg_dir() + "/export.cfg")


func deserialize() -> void:
	_cfg.load(_get_cfg_dir() + "/export.cfg")
	_read_entries()


func get_categories() -> Array[Category]:
	return _categories


func _write_entries() -> void:
	for entry in _entries:
		_cfg.set_value(_entries[entry].cfg_region, _entries[entry].cfg_key, get(entry))


func _read_entries() -> void:
	for entry in _entries:
		set(entry, _cfg.get_value(_entries[entry].cfg_region, _entries[entry].cfg_key, get(entry)))


func _get_cfg_dir() -> String:
	return get_script().resource_path.get_base_dir()


class Category:
	var cfg_region : String
	var visible_name : String
	var entries : Array[Entry]
	
	func _init(cfg_region : String, visible_name : String) ->void:
		self.cfg_region = cfg_region
		self.visible_name = visible_name


class Entry:
	enum CustomType {
		NONE = 0,
		OPTIONS,
	}
	
	var var_name : String
	var cfg_region : String
	var cfg_key : String
	var visible_name : String
	var tooltip : String
	var disabled : bool = false
	var custom_type : CustomType = CustomType.NONE
	var custom_data
	
	func _init(var_name : String, cfg_region : String, cfg_key : String, visible_name : String, tooltip : String) ->void:
		self.var_name = var_name
		self.cfg_region = cfg_region
		self.cfg_key = cfg_key
		self.visible_name = visible_name
		self.tooltip = tooltip
	
	func set_custom_type(custom_type : CustomType, custom_data) -> void:
		self.custom_type = custom_type
		self.custom_data = custom_data
