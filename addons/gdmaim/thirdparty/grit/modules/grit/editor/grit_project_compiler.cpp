#include "grit_project_compiler.h"

#include "grit_script_stripper.h"

#include "grit/compiler/grit_recording_code_generator.h"
#include "grit/compiler/grit_source_writer.h"

#include "modules/gdscript/gdscript.h"
#include "modules/gdscript/gdscript_cache.h"

#include "core/crypto/crypto_core.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/resource_loader.h"

#include "grit/editor/grit_piper.h"

static constexpr const char *GRIT_FORMAT_VERSION = "grit-format-5";

TypedDictionary<String, Ref<GDScript>> GritProjectCompiler::buffer;

void GritProjectCompiler::collect_files(const String &p_directory, Vector<String> &r_scripts, Vector<String> &r_resources) {
	Ref<DirAccess> directory = DirAccess::open(p_directory);
	if (directory.is_null() || directory->file_exists(".gdignore")) {
		return;
	}

	for (const String &name : directory->get_files()) {
		const String extension = name.get_extension();
		if (extension == "gd") {
			r_scripts.push_back(p_directory.path_join(name));
		} else if (extension == "tscn" || extension == "scn" || extension == "tres" || extension == "res") {
			r_resources.push_back(p_directory.path_join(name));
		}
	}
	for (const String &name : directory->get_directories()) {
		collect_files(p_directory.path_join(name), r_scripts, r_resources);
	}
}

bool GritProjectCompiler::may_contain_builtin_scripts(const String &p_path) {
	const Vector<uint8_t> content = FileAccess::get_file_as_bytes(p_path);
	return _may_contain_builtin_scripts(p_path, content);
}
	
bool GritProjectCompiler::_may_contain_builtin_scripts(const String &p_path, const Vector<uint8_t>& content) {
	const String extension = p_path.get_extension();
	const CharString marker = String(extension == "tscn" || extension == "tres" ? "type=\"GDScript\"" : "GDScript").utf8();
	const int marker_length = marker.length();
	const uint8_t *bytes = content.ptr();
	for (int i = 0; i + marker_length <= content.size(); i++) {
		if (memcmp(bytes + i, marker.get_data(), marker_length) == 0) {
			return true;
		}
	}
	return false;
}

void GritProjectCompiler::collect_builtin_scripts(const Variant &p_value, const String &p_owner_path, HashSet<const Object *> &r_visited, Vector<ScriptSource> &r_sources) {
	switch (p_value.get_type()) {
		case Variant::OBJECT: {
			const Ref<Resource> resource = p_value;
			if (resource.is_null() || r_visited.has(resource.ptr())) {
				return;
			}
			r_visited.insert(resource.ptr());
			const String path = resource->get_path();
			const bool is_owned = path == p_owner_path || path.begins_with(p_owner_path + "::");
			const Ref<GDScript> script = resource;
			if (script.is_valid()) {
				if (is_owned && path != p_owner_path) {
					ScriptSource source;
					source.path = path;
					source.code = script->get_source_code();
					source.script = script;
					r_sources.push_back(source);
				}
				return;
			}
			if (!path.is_empty() && !is_owned) {
				return;
			}
			List<PropertyInfo> properties;
			resource->get_property_list(&properties);
			for (const PropertyInfo &property : properties) {
				if (property.usage & PROPERTY_USAGE_STORAGE) {
					collect_builtin_scripts(resource->get(property.name), p_owner_path, r_visited, r_sources);
				}
			}
		} break;
		case Variant::ARRAY: {
			const Array array = p_value;
			for (int i = 0; i < array.size(); i++) {
				collect_builtin_scripts(array[i], p_owner_path, r_visited, r_sources);
			}
		} break;
		case Variant::DICTIONARY: {
			const Dictionary dictionary = p_value;
			for (const KeyValue<Variant, Variant> &entry : dictionary) {
				collect_builtin_scripts(entry.key, p_owner_path, r_visited, r_sources);
				collect_builtin_scripts(entry.value, p_owner_path, r_visited, r_sources);
			}
		} break;
		default:
			break;
	}
}

String GritProjectCompiler::compute_fingerprint(const Vector<ScriptSource> &p_sources, bool p_strip) {
	CryptoCore::SHA256Context context;
	context.start();

	const CharString version = (String(GRIT_FORMAT_VERSION) + (p_strip ? "\nstripped" : "")).utf8();
	context.update(reinterpret_cast<const uint8_t *>(version.get_data()), version.length());
	for (const ScriptSource &source : p_sources) {
		const CharString header = vformat("\n%s\n%d\n", source.path, source.code.length()).utf8();
		const CharString code = source.code.utf8();
		context.update(reinterpret_cast<const uint8_t *>(header.get_data()), header.length());
		context.update(reinterpret_cast<const uint8_t *>(code.get_data()), code.length());
	}

	unsigned char hash[32];
	context.finish(hash);
	return String::hex_encode_buffer(hash, 32);
}

HashSet<String> GritProjectCompiler::strippable_functions(const LocalVector<GritFunction> &p_functions) {
	HashSet<String> lambda_roots;
	for (const GritFunction &function : p_functions) {
		const int separator = function.registry_key.find(">");
		if (separator >= 0) {
			lambda_roots.insert(function.registry_key.substr(0, separator));
		}
	}
	HashSet<String> strippable;
	for (const GritFunction &function : p_functions) {
		const bool is_root = !function.registry_key.contains(">");
		const String name = function.name;
		const bool is_accessor = name.begins_with("@") && (name.ends_with("_setter") || name.ends_with("_getter"));
		if (is_root && function.is_supported() && (!name.begins_with("@") || is_accessor) && !lambda_roots.has(function.registry_key)) {
			strippable.insert(GritScriptStripper::function_id(function.class_path, function.name));
		}
	}
	return strippable;
}

static String normalize_newlines(const String &p_source) {
	return p_source.replace("\r\n", "\n").replace("\r", "\n");
}


Error GritProjectCompiler::compile_script(const ScriptSource &p_source, LocalVector<GritFunction> &r_functions) {
	Error error = OK;
	Ref<GDScript> script = p_source.script;

	String path = p_source.path;

	if (script.is_null()) {
		script = GDScriptCache::get_full_script(path, error, String(), true);

		if (script.is_null()) {
			script = memnew(GDScript);
			script->set_source_code(normalize_newlines(p_source.code));
		} else {
			script->set_source_code(normalize_newlines(script->get_source_code()));
		}
		error = script->reload(false);
	}

	GritScriptRecording recording(script.ptr());
	error = script->reload(false);
	if (error != OK) {
		ERR_PRINT(vformat("Grit: could not compile \"%s\".", p_source.path));
		return error;
	}
	r_functions = recording.functions;

	return OK;
}

Error GritProjectCompiler::gdm_compile(const String &p_output_directory, const Array &arr, bool p_strip, Report &r_report) {
	Vector<ScriptSource> sources;

	for(int x=0; x<arr.size() ; x++){
		const Array & data = arr[x];

		Error error = OK;
		ScriptSource source;
		source.path = data[0];
		source.code = data[1];

		if (source.code.length() < 1) {
			error = ERR_FILE_CANT_READ;
		}

		ERR_FAIL_COND_V_MSG(error != OK, error, vformat("Grit: could not read \"%s\".", source.path));
		sources.push_back(source);
	}

	sources.sort();

	r_report = Report();
	r_report.fingerprint = compute_fingerprint(sources, p_strip);
	r_report.script_count = sources.size();

	GritSourceWriter writer;
	for (const ScriptSource &source : sources) {
		LocalVector<GritFunction> functions;
		const Error error = compile_script(source, functions);
		if (error != OK) {
			buffer.clear();
			return error;
		}
		
		for (GritFunction &function : functions) {
				function.script_path = GritPiper::get_path(function.script_path);
				function.class_path = GritPiper::get_path(function.class_path);
				function.signature = GritPiper::get_path(function.signature);
			}

		if (p_strip) {
			GritScriptStripper stripper;
			String stripped;
			const Error strip_error = stripper.strip(GritPiper::get_path(source.path), source.code, strippable_functions(functions), stripped);
			ERR_FAIL_COND_V_MSG(strip_error != OK, strip_error, vformat("Grit: could not strip \"%s\".", GritPiper::get_path(source.path)));
			for (GritFunction &function : functions) {
				function.is_stripped = stripper.get_stripped().has(GritScriptStripper::function_id(function.class_path, function.name)) && !function.registry_key.contains(">");
			}
			r_report.stripped_function_count += stripper.get_stripped().size();
			r_report.stripped_sources.insert(source.path, stripped);
		}

		for (const GritFunction &function : functions) {
			r_report.function_count++;
			if (function.is_supported()) {
				writer.add_function(function);
			} else {
				print_line(vformat("Grit: %s::%s stays in the GDScript VM: %s.", source.path, function.name, function.unsupported_reason));
			}
		}
	}
	buffer.clear();
	r_report.native_function_count = writer.get_function_count();

	Error error = writer.write(p_output_directory, r_report.fingerprint, p_strip);
	if (error != OK) {
		return error;
	}
	error = GritSourceWriter::write_fingerprint(r_report.fingerprint);
	if (error != OK) {
		return error;
	}

	print_line(vformat("Grit: %d of %d functions in %d scripts compiled to native code.", r_report.native_function_count, r_report.function_count, r_report.script_count));
	if (p_strip) {
		print_line(vformat("Grit: stripped the bodies of %d functions from the exported scripts.", r_report.stripped_function_count));
	}
	return OK;
}

Error GritProjectCompiler::compile(const String &p_output_directory, bool p_strip, Report &r_report) {
	Vector<String> script_paths;
	Vector<String> resource_paths;
	collect_files("res://", script_paths, resource_paths);
	resource_paths.sort();

	Vector<ScriptSource> sources;
	for (const String &path : script_paths) {
		Error error = OK;
		ScriptSource source;
		source.path = path;
		source.code = FileAccess::get_file_as_string(path, &error);
		ERR_FAIL_COND_V_MSG(error != OK, error, vformat("Grit: could not read \"%s\".", path));
		sources.push_back(source);
	}

	Vector<Ref<Resource>> resources;
	for (const String &path : resource_paths) {
		if (!may_contain_builtin_scripts(path)) {
			continue;
		}
		const Ref<Resource> resource = ResourceLoader::load(path, "", ResourceFormatLoader::CACHE_MODE_IGNORE);
		ERR_FAIL_COND_V_MSG(resource.is_null(), ERR_CANT_OPEN, vformat("Grit: could not load \"%s\" to find its built-in scripts.", path));
		resources.push_back(resource);
		HashSet<const Object *> visited;
		collect_builtin_scripts(resource, path, visited, sources);
	}
	sources.sort();

	r_report = Report();
	r_report.fingerprint = compute_fingerprint(sources, p_strip);
	r_report.script_count = sources.size();

	GritSourceWriter writer;
	for (const ScriptSource &source : sources) {
		LocalVector<GritFunction> functions;
		const Error error = compile_script(source, functions);
		if (error != OK) {
			return error;
		}
		if (p_strip) {
			GritScriptStripper stripper;
			String stripped;
			const Error strip_error = stripper.strip(source.path, source.code, strippable_functions(functions), stripped);
			ERR_FAIL_COND_V_MSG(strip_error != OK, strip_error, vformat("Grit: could not strip \"%s\".", source.path));
			for (GritFunction &function : functions) {
				function.is_stripped = stripper.get_stripped().has(GritScriptStripper::function_id(function.class_path, function.name)) && !function.registry_key.contains(">");
			}
			r_report.stripped_function_count += stripper.get_stripped().size();
			r_report.stripped_sources.insert(source.path, stripped);
		}

		for (const GritFunction &function : functions) {
			r_report.function_count++;
			if (function.is_supported()) {
				writer.add_function(function);
			} else {
				print_line(vformat("Grit: %s::%s stays in the GDScript VM: %s.", source.path, function.name, function.unsupported_reason));
			}
		}
	}
	r_report.native_function_count = writer.get_function_count();

	Error error = writer.write(p_output_directory, r_report.fingerprint, p_strip);
	if (error != OK) {
		return error;
	}
	error = GritSourceWriter::write_fingerprint(r_report.fingerprint);
	if (error != OK) {
		return error;
	}

	print_line(vformat("Grit: %d of %d functions in %d scripts compiled to native code.", r_report.native_function_count, r_report.function_count, r_report.script_count));
	if (p_strip) {
		print_line(vformat("Grit: stripped the bodies of %d functions from the exported scripts.", r_report.stripped_function_count));
	}
	return OK;
}
