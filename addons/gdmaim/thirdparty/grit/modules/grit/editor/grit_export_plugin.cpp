#include "grit_export_plugin.h"

#include "grit_project_compiler.h"

#include "grit/runtime/grit_registry.h"

#include "modules/gdscript/gdscript.h"
#include "modules/gdscript/gdscript_tokenizer_buffer.h"

#include "core/config/project_settings.h"
#include "core/io/resource.h"

void GritExportPlugin::_get_export_options(const Ref<EditorExportPlatform> &p_export_platform, List<EditorExportPlatform::ExportOption> *r_options) const {
	r_options->push_back(EditorExportPlatform::ExportOption(PropertyInfo(Variant::BOOL, OPTION_STRIP_SCRIPTS), false));
}

void GritExportPlugin::_export_begin(const HashSet<String> &p_features, bool p_debug, const String &p_path, int p_flags) {
	stripped_sources.clear();
	if (!bool(GLOBAL_GET(SETTING_ENABLED))) {
		return;
	}

	const Ref<EditorExportPreset> preset = get_export_preset();
	script_mode = preset.is_valid() ? preset->get_script_export_mode() : EditorExportPreset::MODE_SCRIPT_BINARY_TOKENS_COMPRESSED;
	const bool strip_requested = bool(get_option(OPTION_STRIP_SCRIPTS));
	const bool strip = strip_requested && !p_debug;
	if (strip_requested && p_debug) {
		print_line("Grit: scripts are only stripped in release exports, this debug export keeps them.");
	}

	const String output_directory = ProjectSettings::get_singleton()->globalize_path(GLOBAL_GET(SETTING_GENERATED_DIRECTORY));
	GritProjectCompiler compiler;
	GritProjectCompiler::Report report;
	const Error error = compiler.compile(output_directory, strip, report);
	if (error != OK) {
		ERR_PRINT("Grit: native code generation failed, the exported project will use the GDScript VM.");
		return;
	}

	stripped_sources = report.stripped_sources;
	customization_hash = GritRegistry::make_pack_fingerprint(report.fingerprint, strip).hash64();
	add_file(GritRegistry::get_fingerprint_path(), GritRegistry::make_pack_fingerprint(report.fingerprint, strip).to_utf8_buffer(), false);
	print_line(vformat("Grit: generated C++ written to \"%s\". Rebuild the export template with grit_generated pointing there.", output_directory));
}

void GritExportPlugin::_export_file(const String &p_path, const String &p_type, const HashSet<String> &p_features) {
	const String *source = stripped_sources.getptr(p_path);
	if (!source) {
		return;
	}
	if (script_mode == EditorExportPreset::MODE_SCRIPT_TEXT) {
		add_file(p_path, source->to_utf8_buffer(), false);
		skip();
		return;
	}
	const GDScriptTokenizerBuffer::CompressMode compress_mode = script_mode == EditorExportPreset::MODE_SCRIPT_BINARY_TOKENS_COMPRESSED ? GDScriptTokenizerBuffer::COMPRESS_ZSTD : GDScriptTokenizerBuffer::COMPRESS_NONE;
	const Vector<uint8_t> tokens = GDScriptTokenizerBuffer::parse_code_string(*source, compress_mode);
	ERR_FAIL_COND_MSG(tokens.is_empty(), vformat("Grit: could not tokenize the stripped \"%s\".", p_path));
	add_file(p_path.get_basename() + ".gdc", tokens, true);
}

bool GritExportPlugin::_begin_customize_resources(const Ref<EditorExportPlatform> &p_platform, const Vector<String> &p_features) {
	for (const KeyValue<String, String> &entry : stripped_sources) {
		if (entry.key.contains("::")) {
			return true;
		}
	}
	return false;
}

Ref<Resource> GritExportPlugin::_customize_resource(const Ref<Resource> &p_resource, const String &p_path) {
	const Ref<GDScript> script = p_resource;
	if (script.is_null() || !script->is_built_in()) {
		return Ref<Resource>();
	}
	const String *source = stripped_sources.getptr(script->get_path());
	if (!source || ResourceCache::get_ref(script->get_path()).ptr() == script.ptr()) {
		return Ref<Resource>();
	}
	script->set_source_code(*source);
	return script;
}

uint64_t GritExportPlugin::_get_customization_configuration_hash() const {
	return customization_hash;
}

void GritExportPlugin::_export_end() {
	stripped_sources.clear();
}
