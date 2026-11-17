#pragma once

#include "grit_function_nesting.h"
#include "grit_signature.h"

#include "modules/gdscript/gdscript_byte_codegen.h"

class GritRuntimeCodeGenerator : public GDScriptByteCodeGenerator {
	struct NestedFunction {
		GDScriptFunction *function = nullptr;
		String signature;
	};

	GritFunctionNesting<NestedFunction> nesting;
	GDScript *script = nullptr;
	StringName function_name;
	GritFunctionSignature signature;
	int initial_line = 0;

	static void attach(GDScript *p_script, GDScriptFunction *p_function, const String &p_key, const String &p_signature);

public:
	virtual uint32_t add_parameter(const StringName &p_name, bool p_is_optional, const GDScriptDataType &p_type) override;
	virtual void write_start(GDScript *p_script, const StringName &p_function_name, bool p_static, Variant p_rpc_config, const GDScriptDataType &p_return_type) override;
	virtual void set_initial_line(int p_line) override;
	virtual void write_newline(int p_line) override;
	virtual void write_assert(const Address &p_test, const Address &p_message) override;
	virtual GDScriptFunction *write_end() override;
};
