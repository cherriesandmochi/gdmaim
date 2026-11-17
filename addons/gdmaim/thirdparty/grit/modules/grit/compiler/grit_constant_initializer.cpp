#include "grit_cpp_emitter.h"

#include "modules/gdscript/gdscript.h"
#include "modules/gdscript/gdscript_utility_callable.h"
#include "modules/gdscript/gdscript_utility_functions.h"

#include "core/config/engine.h"
#include "core/io/resource.h"

#include <cstdio>

#include "grit/editor/grit_piper.h"

String GritCppEmitter::string_literal(const String &p_text) {
	return "String::utf8(\"" + p_text.c_escape() + "\")";
}

static String object_constant_initializer(const Variant &p_value) {
	const Object *object = p_value.get_validated_object();
	if (!object) {
		return p_value.get_type() == Variant::OBJECT && p_value.operator Object *() == nullptr ? String("Variant()") : String();
	}
	const GDScriptNativeClass *native_class = Object::cast_to<GDScriptNativeClass>(object);
	if (native_class) {
		return "GritRuntime::native_class(StringName(String::utf8(\"" + String(native_class->get_name()).c_escape() + "\")))";
	}
	const Script *script = Object::cast_to<Script>(object);
	if (script) {
		const String reference = GritType::script_reference(script);
		if (reference.is_empty()) {
			return String();
		}
		return "GritRuntime::load_script(p_script, String::utf8(\"" + reference.c_escape() + "\"))";
	}
	List<Engine::Singleton> singletons;
	Engine::get_singleton()->get_singletons(&singletons);
	for (const Engine::Singleton &singleton : singletons) {
		if (singleton.ptr == object) {
			return "GritRuntime::engine_singleton(StringName(String::utf8(\"" + String(singleton.name).c_escape() + "\")))";
		}
	}
	const Resource *resource = Object::cast_to<Resource>(object);
	if (resource && !resource->get_path().is_empty() && !resource->get_path().contains("::")) {
		return "GritRuntime::load_resource(String::utf8(\"" + GritPiper::get_path(resource->get_path()).c_escape() + "\"))";
	}
	return String();
}

static String real_literal(double p_value) {
	if (Math::is_nan(p_value)) {
		return "Math::NaN";
	}
	if (Math::is_inf(p_value)) {
		return p_value > 0 ? "Math::INF" : "(-Math::INF)";
	}
	char buffer[64];
	snprintf(buffer, sizeof(buffer), "%a", p_value);
	return String(buffer);
}

static String vector2_literal(const Vector2 &p_value) {
	return "Vector2(real_t(" + real_literal(p_value.x) + "), real_t(" + real_literal(p_value.y) + "))";
}

static String vector3_literal(const Vector3 &p_value) {
	return "Vector3(real_t(" + real_literal(p_value.x) + "), real_t(" + real_literal(p_value.y) + "), real_t(" + real_literal(p_value.z) + "))";
}

static String vector4_literal(const Vector4 &p_value) {
	return "Vector4(real_t(" + real_literal(p_value.x) + "), real_t(" + real_literal(p_value.y) + "), real_t(" + real_literal(p_value.z) + "), real_t(" + real_literal(p_value.w) + "))";
}

static String math_literal(const Variant &p_value) {
	switch (p_value.get_type()) {
		case Variant::VECTOR2:
			return vector2_literal(p_value);
		case Variant::VECTOR2I: {
			const Vector2i value = p_value;
			return vformat("Vector2i(%d, %d)", value.x, value.y);
		}
		case Variant::RECT2: {
			const Rect2 value = p_value;
			return "Rect2(" + vector2_literal(value.position) + ", " + vector2_literal(value.size) + ")";
		}
		case Variant::RECT2I: {
			const Rect2i value = p_value;
			return vformat("Rect2i(%d, %d, %d, %d)", value.position.x, value.position.y, value.size.x, value.size.y);
		}
		case Variant::VECTOR3:
			return vector3_literal(p_value);
		case Variant::VECTOR3I: {
			const Vector3i value = p_value;
			return vformat("Vector3i(%d, %d, %d)", value.x, value.y, value.z);
		}
		case Variant::TRANSFORM2D: {
			const Transform2D value = p_value;
			return "Transform2D(" + vector2_literal(value.columns[0]) + ", " + vector2_literal(value.columns[1]) + ", " + vector2_literal(value.columns[2]) + ")";
		}
		case Variant::VECTOR4:
			return vector4_literal(p_value);
		case Variant::VECTOR4I: {
			const Vector4i value = p_value;
			return vformat("Vector4i(%d, %d, %d, %d)", value.x, value.y, value.z, value.w);
		}
		case Variant::PLANE: {
			const Plane value = p_value;
			return "Plane(" + vector3_literal(value.normal) + ", real_t(" + real_literal(value.d) + "))";
		}
		case Variant::QUATERNION: {
			const Quaternion value = p_value;
			return "Quaternion(real_t(" + real_literal(value.x) + "), real_t(" + real_literal(value.y) + "), real_t(" + real_literal(value.z) + "), real_t(" + real_literal(value.w) + "))";
		}
		case Variant::AABB: {
			const AABB value = p_value;
			return "AABB(" + vector3_literal(value.position) + ", " + vector3_literal(value.size) + ")";
		}
		case Variant::BASIS: {
			const Basis value = p_value;
			return "Basis(" + vector3_literal(value.get_column(0)) + ", " + vector3_literal(value.get_column(1)) + ", " + vector3_literal(value.get_column(2)) + ")";
		}
		case Variant::TRANSFORM3D: {
			const Transform3D value = p_value;
			return "Transform3D(Basis(" + vector3_literal(value.basis.get_column(0)) + ", " + vector3_literal(value.basis.get_column(1)) + ", " + vector3_literal(value.basis.get_column(2)) + "), " + vector3_literal(value.origin) + ")";
		}
		case Variant::PROJECTION: {
			const Projection value = p_value;
			return "Projection(" + vector4_literal(value.columns[0]) + ", " + vector4_literal(value.columns[1]) + ", " + vector4_literal(value.columns[2]) + ", " + vector4_literal(value.columns[3]) + ")";
		}
		case Variant::COLOR: {
			const Color value = p_value;
			return "Color(float(" + real_literal(value.r) + "), float(" + real_literal(value.g) + "), float(" + real_literal(value.b) + "), float(" + real_literal(value.a) + "))";
		}
		default:
			return String();
	}
}

static String element_script_initializer(const Variant &p_script) {
	const Script *script = Object::cast_to<Script>(p_script.operator Object *());
	if (!script) {
		return "GritRuntime::no_script()";
	}
	const String reference = GritType::script_reference(script);
	if (reference.is_empty()) {
		return String();
	}
	return "GritRuntime::load_script(p_script, String::utf8(\"" + reference.c_escape() + "\"))";
}

static String element_type_arguments(Variant::Type p_builtin_type, const StringName &p_native_type, const Variant &p_script) {
	const String script = element_script_initializer(p_script);
	if (script.is_empty()) {
		return String();
	}
	return "static_cast<Variant::Type>(" + itos(p_builtin_type) + "), StringName(String::utf8(\"" + String(p_native_type).c_escape() + "\")), " + script;
}

static String value_initializer(const Variant &p_value) {
	switch (p_value.get_type()) {
		case Variant::NIL:
			return "Variant()";
		case Variant::BOOL:
			return bool(p_value) ? "Variant(true)" : "Variant(false)";
		case Variant::INT:
			return "Variant(" + (int64_t(p_value) == INT64_MIN ? String("INT64_MIN") : "int64_t(" + itos(p_value) + ")") + ")";
		case Variant::FLOAT:
			return "Variant(" + real_literal(p_value) + ")";
		case Variant::STRING:
			return "Variant(String::utf8(\"" + String(p_value).c_escape() + "\"))";
		case Variant::STRING_NAME:
			return "Variant(StringName(String::utf8(\"" + String(p_value).c_escape() + "\")))";
		case Variant::NODE_PATH:
			return "Variant(NodePath(String::utf8(\"" + String(p_value).c_escape() + "\")))";
		case Variant::OBJECT:
			return object_constant_initializer(p_value);
		case Variant::ARRAY: {
			const Array array = p_value;
			const String type = element_type_arguments((Variant::Type)array.get_typed_builtin(), array.get_typed_class_name(), array.get_typed_script());
			if (type.is_empty()) {
				return String();
			}
			String elements;
			for (int i = 0; i < array.size(); i++) {
				const String element = value_initializer(array[i]);
				if (element.is_empty()) {
					return String();
				}
				elements += (i > 0 ? ", " : "") + element;
			}
			return "GritRuntime::array_constant({ " + elements + " }, " + type + ", " + String(array.is_read_only() ? "true" : "false") + ")";
		}
		case Variant::DICTIONARY: {
			const Dictionary dictionary = p_value;
			const String key_type = element_type_arguments((Variant::Type)dictionary.get_typed_key_builtin(), dictionary.get_typed_key_class_name(), dictionary.get_typed_key_script());
			const String value_type = element_type_arguments((Variant::Type)dictionary.get_typed_value_builtin(), dictionary.get_typed_value_class_name(), dictionary.get_typed_value_script());
			if (key_type.is_empty() || value_type.is_empty()) {
				return String();
			}
			String pairs;
			const Array keys = dictionary.keys();
			for (int i = 0; i < keys.size(); i++) {
				const String key = value_initializer(keys[i]);
				const String value = value_initializer(dictionary[keys[i]]);
				if (key.is_empty() || value.is_empty()) {
					return String();
				}
				pairs += (i > 0 ? ", " : "") + key + ", " + value;
			}
			return "GritRuntime::dictionary_constant({ " + pairs + " }, " + key_type + ", " + value_type + ", " + String(dictionary.is_read_only() ? "true" : "false") + ")";
		}
		case Variant::RID:
			return p_value.operator ::RID() == ::RID() ? String("Variant(RID())") : String();
		case Variant::CALLABLE: {
			const Callable callable = p_value.operator Callable();
			if (callable == Callable()) {
				return "Variant(Callable())";
			}
			const StringName method = callable.get_method();
			if (method != StringName() && (Variant::has_utility_function(method) || GDScriptUtilityFunctions::function_exists(method)) && Callable(memnew(GDScriptUtilityCallable(method))) == callable) {
				return "GritRuntime::utility_callable(StringName(String::utf8(\"" + String(method).c_escape() + "\")))";
			}
			return String();
		}
		case Variant::SIGNAL:
			return p_value.operator Signal() == Signal() ? String("Variant(Signal())") : String();
		case Variant::PACKED_BYTE_ARRAY:
		case Variant::PACKED_INT32_ARRAY:
		case Variant::PACKED_INT64_ARRAY:
		case Variant::PACKED_FLOAT32_ARRAY:
		case Variant::PACKED_FLOAT64_ARRAY:
		case Variant::PACKED_STRING_ARRAY:
		case Variant::PACKED_VECTOR2_ARRAY:
		case Variant::PACKED_VECTOR3_ARRAY:
		case Variant::PACKED_COLOR_ARRAY:
		case Variant::PACKED_VECTOR4_ARRAY: {
			const Array array = p_value;
			String elements;
			for (int i = 0; i < array.size(); i++) {
				const String element = value_initializer(array[i]);
				if (element.is_empty()) {
					return String();
				}
				elements += (i > 0 ? ", " : "") + element;
			}
			return "GritRuntime::packed_array_constant(static_cast<Variant::Type>(" + itos(p_value.get_type()) + "), { " + elements + " })";
		}
		default: {
			const String literal = math_literal(p_value);
			return literal.is_empty() ? String() : "Variant(" + literal + ")";
		}
	}
}

bool GritCppEmitter::can_emit_constant(const Variant &p_value) {
	return !value_initializer(p_value).is_empty();
}

String GritCppEmitter::constant_initializer(const Variant &p_value) {
	return value_initializer(p_value);
}

