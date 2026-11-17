#pragma once

#include "editor/export/editor_export_plugin.h"
#include "grit_piper.h"

class GritExportPlugin : public EditorExportPlugin {
	GDCLASS(GritExportPlugin, EditorExportPlugin);

	HashMap<String, String> stripped_sources;
	uint64_t customization_hash = 0;
	EditorExportPreset::ScriptExportMode script_mode = EditorExportPreset::MODE_SCRIPT_BINARY_TOKENS_COMPRESSED;

protected:
	virtual void _export_begin(const HashSet<String> &p_features, bool p_debug, const String &p_path, int p_flags) override;
	virtual void _export_file(const String &p_path, const String &p_type, const HashSet<String> &p_features) override;
	virtual void _export_end() override;
	virtual bool _begin_customize_resources(const Ref<EditorExportPlatform> &p_platform, const Vector<String> &p_features) override;
	virtual Ref<Resource> _customize_resource(const Ref<Resource> &p_resource, const String &p_path) override;
	virtual uint64_t _get_customization_configuration_hash() const override;
	virtual void _get_export_options(const Ref<EditorExportPlatform> &p_export_platform, List<EditorExportPlatform::ExportOption> *r_options) const override;

public:
	static constexpr const char *SETTING_ENABLED = "grit/export/enabled";
	static constexpr const char *SETTING_GENERATED_DIRECTORY = "grit/export/generated_directory";
	static constexpr const char *OPTION_STRIP_SCRIPTS = "grit/strip_scripts";

	virtual String get_name() const override { return "Grit"; }
	
	friend class GritPiper;
};
