#pragma once

#include "grit_ir.h"

#include "core/templates/hash_map.h"

class GritSourceWriter {
	HashMap<String, LocalVector<GritFunction>> functions_by_script;
	Vector<String> script_order;

	static Error write_file_if_changed(const String &p_path, const String &p_content);
	static void remove_stale_files(const String &p_directory, const Vector<String> &p_current_files);

public:
	void add_function(const GritFunction &p_function);
	bool has_function(const String &p_script_path, const StringName &p_name, int p_initial_line) const;
	int get_function_count() const;

	Error write(const String &p_output_directory, const String &p_fingerprint, bool p_obfuscate) const;
	static Error write_fingerprint(const String &p_fingerprint);
};
