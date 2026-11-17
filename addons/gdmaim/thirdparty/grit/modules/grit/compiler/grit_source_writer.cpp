#include "grit_source_writer.h"

#include "grit_cpp_emitter.h"

#include "grit/runtime/grit_registry.h"

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"

#include "grit/editor/grit_piper.h"

Error GritSourceWriter::write_file_if_changed(const String &p_path, const String &p_content) {
	Error error = OK;
	if (FileAccess::exists(p_path) && FileAccess::get_file_as_string(p_path, &error) == p_content && error == OK) {
		return OK;
	}
	Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::WRITE, &error);
	ERR_FAIL_COND_V_MSG(file.is_null(), error, vformat("Grit: could not write \"%s\".", p_path));
	file->store_string(p_content);
	return OK;
}

void GritSourceWriter::remove_stale_files(const String &p_directory, const Vector<String> &p_current_files) {
	Ref<DirAccess> directory = DirAccess::open(p_directory);
	if (directory.is_null()) {
		return;
	}
	for (const String &name : directory->get_files()) {
		if (name.ends_with(".gen.cpp") && !p_current_files.has(name)) {
			directory->remove(name);
		}
	}
}

void GritSourceWriter::add_function(const GritFunction &p_function) {
	if (!functions_by_script.has(p_function.script_path)) {
		functions_by_script.insert(p_function.script_path, LocalVector<GritFunction>());
		script_order.push_back(p_function.script_path);
	}
	functions_by_script[p_function.script_path].push_back(p_function);
}

bool GritSourceWriter::has_function(const String &p_script_path, const StringName &p_name, int p_initial_line) const {
	const LocalVector<GritFunction> *functions = functions_by_script.getptr(p_script_path);
	if (!functions) {
		return false;
	}
	for (const GritFunction &function : *functions) {
		if (function.name == p_name && function.initial_line == p_initial_line) {
			return true;
		}
	}
	return false;
}

int GritSourceWriter::get_function_count() const {
	int count = 0;
	for (const KeyValue<String, LocalVector<GritFunction>> &entry : functions_by_script) {
		count += entry.value.size();
	}
	return count;
}

Error GritSourceWriter::write(const String &p_output_directory, const String &p_fingerprint, bool p_obfuscate) const {
	Error error = DirAccess::make_dir_recursive_absolute(p_output_directory);
	ERR_FAIL_COND_V_MSG(error != OK, error, vformat("Grit: could not create \"%s\".", p_output_directory));

	Vector<String> sorted_scripts = script_order;
	sorted_scripts.sort();

	Vector<String> generated_files;
	for (const String &script_path : sorted_scripts) {
		const String path = GritPiper::get_path(script_path);
		const String file_name = GritCppEmitter::registration_symbol(path).trim_prefix("grit_register_") + ".gen.cpp";
		error = write_file_if_changed(p_output_directory.path_join(file_name), GritCppEmitter::emit_script_file(path, functions_by_script[script_path], p_obfuscate));
		if (error != OK) {
			return error;
		}
		generated_files.push_back(file_name);
	}

	const String registry_file_name = "grit_registry.gen.cpp";
	error = write_file_if_changed(p_output_directory.path_join(registry_file_name), GritCppEmitter::emit_registry_file(p_fingerprint, sorted_scripts, p_obfuscate));
	if (error != OK) {
		return error;
	}
	generated_files.push_back(registry_file_name);
	remove_stale_files(p_output_directory, generated_files);
	return OK;
}

Error GritSourceWriter::write_fingerprint(const String &p_fingerprint) {
	const String fingerprint_path = ProjectSettings::get_singleton()->globalize_path(GritRegistry::get_fingerprint_path());
	const Error error = DirAccess::make_dir_recursive_absolute(fingerprint_path.get_base_dir());
	ERR_FAIL_COND_V_MSG(error != OK, error, vformat("Grit: could not create \"%s\".", fingerprint_path.get_base_dir()));
	return write_file_if_changed(fingerprint_path, p_fingerprint);
}
