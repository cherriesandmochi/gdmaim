#include "grit_piper.h"
#include "grit_project_compiler.h"
#include "grit_export_plugin.h"

#include "core/object/class_db.h"

#include "core/variant/callable.h"
#include "core/object/ref_counted.h"
#include "modules/grit/runtime/grit_registry.h"

void GritPiper::_bind_methods() {
	ClassDB::bind_method(D_METHOD("compile", "output_directory", "data", "export_mode", "strip", "p_debug"), &GritPiper::compile);
	ClassDB::bind_method(D_METHOD("get_source", "path"), &GritPiper::get_source);
	ClassDB::bind_method(D_METHOD("set_exporter", "exporter"), &GritPiper::set_exporter);
	ADD_PROPERTY(PropertyInfo(Variant::CALLABLE, "exporter"), "set_exporter", "get_exporter");
}

void GritPiper::set_exporter(const Callable &p_exporter) {
	exporter = p_exporter;
}

Callable GritPiper::get_exporter() const {
	return exporter;
}

void GritPiper::compile(const String &output_directory, const Array &data, int export_mode, bool strip, bool p_debug) {
	ERR_FAIL_COND_EDMSG(exporter.is_null(), "Not valid exporter!");

	Ref<GritExportPlugin> gritter = memnew(GritExportPlugin);
	
	stripped_sources.clear();
	gritter->script_mode = EditorExportPreset::MODE_SCRIPT_TEXT;

	if (export_mode >= 0 && export_mode <= EditorExportPreset::ScriptExportMode::MODE_SCRIPT_BINARY_TOKENS_COMPRESSED) {
		gritter->script_mode = (EditorExportPreset::ScriptExportMode)export_mode;
	}

	strip = strip && !p_debug;
	if (strip && p_debug) {
		print_line("Grit: scripts are only stripped in release exports, this debug export keeps them.");
	}

	GritProjectCompiler compiler;
	GritProjectCompiler::Report report;

	const Error error = compiler.gdm_compile(output_directory, data, strip, report);

	if (error != OK) {
		ERR_PRINT("Grit: native code generation failed, the exported project will use the GDScript VM.");
		return;
	}

	stripped_sources = report.stripped_sources;
	customization_hash = GritRegistry::make_pack_fingerprint(report.fingerprint, strip).hash64();

	exporter.call(GritRegistry::get_fingerprint_path(), GritRegistry::make_pack_fingerprint(report.fingerprint, strip).to_utf8_buffer(), false);
	print_line(vformat("Grit: generated C++ written to \"%s\". Rebuild the export template with grit_generated pointing there.", output_directory));
}

String GritPiper::get_source(const String &path) {
	const String *sc_source = stripped_sources.getptr(path);

	if (sc_source != nullptr) {
		return *sc_source;
	}

	return "";
}


const String GritPiper::get_path(const String path){
	return path.replace("res://addons/gdmaim/tmp/", "res://");
}
