#pragma once

#include "editor/export/editor_export_plugin.h"

#include "core/object/ref_counted.h"
#include "core/variant/callable.h"

class GritPiper : public RefCounted{
	GDCLASS(GritPiper, RefCounted);

private:
	Callable exporter;
	HashMap<String, String> stripped_sources;
	uint64_t customization_hash = 0;

protected:
	static void _bind_methods();

public:
	GritPiper() = default;
	~GritPiper() = default;

	Callable get_exporter() const;
	void set_exporter(const Callable &p_exporter);
	void compile(const String & output_directory, const Array &data, int export_mode, bool strip, bool p_debug);
	String get_source(const String &path);
	static const String get_path(const String path);
};
