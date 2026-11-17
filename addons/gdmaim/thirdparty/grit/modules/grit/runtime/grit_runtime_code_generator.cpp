#include "grit_runtime_code_generator.h"

#include "grit_registry.h"
#include "grit_runtime_access.h"

#include "modules/gdscript/gdscript.h"

void GritRuntimeCodeGenerator::attach(GDScript *p_script, GDScriptFunction *p_function, const String &p_key, const String &p_signature) {
	const GritRegistry::NativeFunction *native_function = GritRegistry::get_singleton()->find(p_key, p_signature);
	if (!native_function) {
		if (GritRegistry::get_singleton()->is_stripped(p_key)) {
			CRASH_NOW_MSG(vformat("Grit: \"%s\" was stripped at export but its native code cannot be attached.", p_key));
		}
		return;
	}
	if (native_function->constants) {
		Vector<Variant> constants;
		native_function->constants(p_script, constants);
		GritRuntimeAccess::set_native_constants(p_function, constants);
	}
	p_function->set_native_call(native_function->call);
	print_verbose(vformat("Grit: running \"%s\" natively.", p_key));
}

uint32_t GritRuntimeCodeGenerator::add_parameter(const StringName &p_name, bool p_is_optional, const GDScriptDataType &p_type) {
	signature.parameter_types.push_back(p_type);
	if (p_is_optional) {
		signature.optional_parameter_count++;
	}
	return GDScriptByteCodeGenerator::add_parameter(p_name, p_is_optional, p_type);
}

void GritRuntimeCodeGenerator::write_start(GDScript *p_script, const StringName &p_function_name, bool p_static, Variant p_rpc_config, const GDScriptDataType &p_return_type) {
	script = p_script;
	function_name = p_function_name;
	signature.is_static = p_static;
	signature.return_type = p_return_type;
	GDScriptByteCodeGenerator::write_start(p_script, p_function_name, p_static, p_rpc_config, p_return_type);
}

void GritRuntimeCodeGenerator::set_initial_line(int p_line) {
	initial_line = p_line;
	GDScriptByteCodeGenerator::set_initial_line(p_line);
}

void GritRuntimeCodeGenerator::write_newline(int p_line) {
	nesting.mark_newline();
	GDScriptByteCodeGenerator::write_newline(p_line);
}

void GritRuntimeCodeGenerator::write_assert(const Address &p_test, const Address &p_message) {
	GDScriptByteCodeGenerator::write_assert(p_test, p_message);
	nesting.mark_assert();
}

GDScriptFunction *GritRuntimeCodeGenerator::write_end() {
	GDScriptFunction *compiled_function = GDScriptByteCodeGenerator::write_end();
	const String class_path = script ? GritRegistry::make_class_path(script) : String();
	const bool is_native_candidate = compiled_function && !class_path.is_empty();
	const String key = is_native_candidate ? GritRegistry::make_key(class_path, function_name, initial_line) : String();

	if (nesting.is_nested()) {
		if (is_native_candidate) {
			NestedFunction nested;
			nested.function = compiled_function;
			nested.signature = signature.describe();
			nesting.add(function_name, initial_line, std::move(nested));
		}
		return compiled_function;
	}

	if (is_native_candidate) {
		attach(script, compiled_function, key, signature.describe());
	}
	nesting.resolve(key, [this](const String &p_key, NestedFunction &p_nested) {
		attach(script, p_nested.function, p_key, p_nested.signature);
	});
	return compiled_function;
}
