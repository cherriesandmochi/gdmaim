#include "grit_signature.h"

#include "modules/gdscript/gdscript.h"

String GritFunctionSignature::describe() const {
	PackedStringArray parameters;
	for (const GDScriptDataType &parameter_type : parameter_types) {
		parameters.push_back(describe_type(parameter_type));
	}

	String description = is_static ? "static " : "";
	description += "(" + String(", ").join(parameters) + ") -> " + describe_type(return_type);
	if (optional_parameter_count > 0) {
		description += vformat(" optional %d", optional_parameter_count);
	}
	return description;
}

String GritFunctionSignature::describe_type(const GDScriptDataType &p_type) {
	switch (p_type.kind) {
		case GDScriptDataType::VARIANT:
			return "Variant";
		case GDScriptDataType::BUILTIN: {
			String name = Variant::get_type_name(p_type.builtin_type);
			if (p_type.has_container_element_types()) {
				PackedStringArray elements;
				for (const GDScriptDataType &element_type : p_type.container_element_types) {
					elements.push_back(describe_type(element_type));
				}
				name += "[" + String(", ").join(elements) + "]";
			}
			return name;
		}
		case GDScriptDataType::NATIVE:
			return p_type.native_type;
		case GDScriptDataType::SCRIPT:
		case GDScriptDataType::GDSCRIPT: {
			const GDScript *gdscript = Object::cast_to<GDScript>(p_type.script_type);
			if (gdscript) {
				return "Script(" + gdscript->get_fully_qualified_name() + ")";
			}
			return "Script(" + (p_type.script_type ? p_type.script_type->get_path() : String()) + ")";
		}
	}
	return "Unknown";
}
