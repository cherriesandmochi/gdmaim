#include "register_types.h"

#include "runtime/grit_registry.h"
#include "runtime/grit_runtime.h"
#include "runtime/grit_runtime_code_generator.h"

#include "modules/gdscript/gdscript_compiler.h"

#ifdef TOOLS_ENABLED
#include "compiler/grit_recording_code_generator.h"
#include "editor/grit_export_plugin.h"

#include "core/config/project_settings.h"
#include "editor/editor_node.h"
#include "editor/export/editor_export.h"

#include "editor/grit_piper.h"
#include "core/object/class_db.h"
#endif

static GritRegistry *grit_registry = nullptr;

static GDScriptCodeGenerator *grit_create_code_generator() {
#ifdef TOOLS_ENABLED
	if (GritScriptRecording::get_current()) {
		return memnew(GritRecordingCodeGenerator);
	}
#endif
	if (grit_registry->is_active()) {
		return memnew(GritRuntimeCodeGenerator);
	}
	return memnew(GDScriptByteCodeGenerator);
}

#ifdef TOOLS_ENABLED
static void grit_editor_init() {
	Ref<GritExportPlugin> export_plugin;
	export_plugin.instantiate();
	EditorExport *editor_export = EditorExport::get_singleton();
	const Vector<Ref<EditorExportPlugin>> other_plugins = editor_export->get_export_plugins();
	for (const Ref<EditorExportPlugin> &plugin : other_plugins) {
		editor_export->remove_export_plugin(plugin);
	}
	editor_export->add_export_plugin(export_plugin);
	for (const Ref<EditorExportPlugin> &plugin : other_plugins) {
		editor_export->add_export_plugin(plugin);
	}
}
#endif

void initialize_grit_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SERVERS) {
		#ifdef TOOLS_ENABLED
		if (p_level == MODULE_INITIALIZATION_LEVEL_EDITOR) {
			GDREGISTER_CLASS(GritPiper);
		}
		#endif
		return;
	}

	grit_registry = memnew(GritRegistry);
	grit_registry->activate();

#ifdef TOOLS_ENABLED
	GLOBAL_DEF(GritExportPlugin::SETTING_ENABLED, true);
	GLOBAL_DEF(PropertyInfo(Variant::STRING, GritExportPlugin::SETTING_GENERATED_DIRECTORY, PROPERTY_HINT_DIR), "res://.godot/grit/generated");
	//EditorNode::add_init_callback(grit_editor_init);
	
	GDScriptCompiler::set_code_generator_factory(grit_create_code_generator);
#else
	if (grit_registry->is_active()) {
		GDScriptCompiler::set_code_generator_factory(grit_create_code_generator);
	}
#endif
}

void uninitialize_grit_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SERVERS) {
		return;
	}

	GDScriptCompiler::set_code_generator_factory(nullptr);
	GritRuntime::clear_property_accessors();
	memdelete(grit_registry);
	grit_registry = nullptr;
}
