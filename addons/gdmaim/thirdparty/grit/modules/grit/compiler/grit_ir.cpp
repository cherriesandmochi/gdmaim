#include "grit_ir.h"

#include "grit/runtime/grit_registry.h"

#include "modules/gdscript/gdscript.h"

#include "grit/editor/grit_piper.h"

GritType GritType::from(const GDScriptDataType &p_type) {
	GritType type;
	switch (p_type.kind) {
		case GDScriptDataType::VARIANT:
			type.kind = Kind::VARIANT;
			break;
		case GDScriptDataType::BUILTIN:
			type.kind = Kind::BUILTIN;
			break;
		case GDScriptDataType::NATIVE:
			type.kind = Kind::NATIVE;
			break;
		case GDScriptDataType::SCRIPT:
		case GDScriptDataType::GDSCRIPT:
			type.kind = Kind::SCRIPT;
			break;
	}
	type.builtin_type = p_type.builtin_type;
	type.native_type = p_type.native_type;
	if (p_type.script_type) {
		type.script = script_reference(p_type.script_type);
	}
	for (const GDScriptDataType &element : p_type.container_element_types) {
		type.elements.push_back(from(element));
	}
	return type;
}

String GritType::script_reference(const Script *p_script) {
	const GDScript *gdscript = Object::cast_to<GDScript>(p_script);
	if (!gdscript) {
		return GritPiper::get_path(p_script->get_path());
	}
	return GritPiper::get_path(GritRegistry::make_class_path(gdscript));
}

bool GritType::is_referencable() const {
	if (kind == Kind::SCRIPT && script.is_empty()) {
		return false;
	}
	for (const GritType &element_type : elements) {
		if (!element_type.is_referencable()) {
			return false;
		}
	}
	return true;
}

GritType GritType::element(int p_index) const {
	if (p_index < 0 || p_index >= int(elements.size())) {
		return GritType();
	}
	return elements[p_index];
}

bool GritType::can_contain_object() const {
	if (kind != Kind::BUILTIN) {
		return true;
	}
	switch (builtin_type) {
		case Variant::ARRAY:
			return elements.is_empty() || elements[0].can_contain_object();
		case Variant::DICTIONARY:
			return elements.is_empty() || element(0).can_contain_object() || element(1).can_contain_object();
		case Variant::NIL:
		case Variant::OBJECT:
			return true;
		default:
			return false;
	}
}

bool GritType::operator==(const GritType &p_other) const {
	if (kind != p_other.kind || builtin_type != p_other.builtin_type || native_type != p_other.native_type || script != p_other.script || elements.size() != p_other.elements.size()) {
		return false;
	}
	for (uint32_t i = 0; i < elements.size(); i++) {
		if (!(elements[i] == p_other.elements[i])) {
			return false;
		}
	}
	return true;
}
