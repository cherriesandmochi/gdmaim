#pragma once

#include "modules/gdscript/gdscript_function.h"

struct GritFunctionSignature {
	bool is_static = false;
	GDScriptDataType return_type;
	Vector<GDScriptDataType> parameter_types;
	int optional_parameter_count = 0;

	String describe() const;

	static String describe_type(const GDScriptDataType &p_type);
};
