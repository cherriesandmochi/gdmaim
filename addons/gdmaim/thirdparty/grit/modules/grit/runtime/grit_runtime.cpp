#include "grit_runtime.h"

#include "modules/gdscript/gdscript_cache.h"
#include "modules/gdscript/gdscript_lambda_callable.h"
#include "modules/gdscript/gdscript_utility_callable.h"

#include "core/config/engine.h"
#include "core/debugger/script_debugger.h"
#include "core/core_string_names.h"
#include "core/io/resource_loader.h"
#include "core/object/class_db.h"

namespace GritRuntime {

static String script_display_name(const Ref<Script> &p_script) {
#ifdef DEBUG_ENABLED
	return GDScript::debug_get_script_name(p_script);
#else
	return p_script.is_valid() ? p_script->get_path().get_file() : String();
#endif
}

static String element_type_name(Variant::Type p_builtin_type, const StringName &p_native_type, const Ref<Script> &p_script_type) {
	if (p_script_type.is_valid() && p_script_type->is_valid()) {
		return script_display_name(p_script_type);
	} else if (p_native_type != StringName()) {
		return p_native_type.operator String();
	}
	return Variant::get_type_name(p_builtin_type);
}

static String value_type_name(const Variant *p_value) {
	if (p_value->get_type() == Variant::OBJECT) {
		bool was_freed = false;
		Object *object = p_value->get_validated_object_with_check(was_freed);
		if (!object) {
			return was_freed ? "previously freed" : "null instance";
		}
		if (object->is_class_ptr(GDScriptNativeClass::get_class_ptr_static())) {
			return Object::cast_to<GDScriptNativeClass>(object)->get_name();
		}
		String name = object->get_class();
		if (object->get_script_instance()) {
			name += " (" + script_display_name(object->get_script_instance()->get_script()) + ")";
		}
		return name;
	}
	if (p_value->get_type() == Variant::ARRAY) {
		String name = "Array";
		const Array *array = VariantInternal::get_array(p_value);
		if (array->is_typed()) {
			name += "[" + element_type_name((Variant::Type)array->get_typed_builtin(), array->get_typed_class_name(), array->get_typed_script()) + "]";
		}
		return name;
	}
	if (p_value->get_type() == Variant::DICTIONARY) {
		String name = "Dictionary";
		const Dictionary *dictionary = VariantInternal::get_dictionary(p_value);
		if (dictionary->is_typed()) {
			name += "[" + element_type_name((Variant::Type)dictionary->get_typed_key_builtin(), dictionary->get_typed_key_class_name(), dictionary->get_typed_key_script()) +
					", " + element_type_name((Variant::Type)dictionary->get_typed_value_builtin(), dictionary->get_typed_value_class_name(), dictionary->get_typed_value_script()) + "]";
		}
		return name;
	}
	return Variant::get_type_name(p_value->get_type());
}

#ifdef DEBUG_ENABLED
static String key_description(const Variant &p_key) {
	String description = p_key.operator String();
	if (!description.is_empty()) {
		return "'" + description + "'";
	}
	return "of type '" + value_type_name(&p_key) + "'";
}
#endif

static bool script_inherits(const Object *p_object, const Script *p_script_type) {
	if (!p_object || !p_object->get_script_instance()) {
		return false;
	}
	const Script *script = p_object->get_script_instance()->get_script().ptr();
	while (script) {
		if (script == p_script_type) {
			return true;
		}
		script = script->get_base_script().ptr();
	}
	return false;
}

#ifdef DEBUG_ENABLED
void debug_line(CallScope &r_scope) {
	ScriptDebugger *debugger = EngineDebugger::get_script_debugger();
	bool do_break = false;
	if (unlikely(debugger->get_lines_left() > 0)) {
		if (debugger->get_depth() <= 0) {
			debugger->set_lines_left(debugger->get_lines_left() - 1);
		}
		if (debugger->get_lines_left() <= 0) {
			do_break = true;
		}
	}
	if (debugger->is_breakpoint(r_scope.line, r_scope.function->get_source())) {
		do_break = true;
	}
	if (unlikely(do_break)) {
		GDScriptLanguage::get_singleton()->debug_break("Breakpoint", true);
	}
	EngineDebugger::get_singleton()->line_poll();
}
#endif

void CallScope::enter(GDScriptInstance *p_instance) {
	GDScriptLanguage::get_singleton()->enter_function(&level, p_instance, function, nullptr, &ip, &line);
}

void CallScope::exit() {
	GDScriptLanguage::get_singleton()->exit_function();
}

Variant class_of(const GritFrame &p_frame) {
	GDScript *script = p_frame.instance ? GritRuntimeAccess::get_instance_script(p_frame.instance) : GritRuntimeAccess::get_function_script(p_frame.function);
	return Variant(script);
}

void report_error(const GritFrame &p_frame, int p_line, const String &p_message) {
#ifdef DEBUG_ENABLED
	const bool instance_valid_with_script = GritRuntimeAccess::is_instance_valid(p_frame.instance);
	const GDScript *function_script = p_frame.function->get_script();
	const GDScript *script = p_frame.instance ? GritRuntimeAccess::get_instance_script(p_frame.instance) : function_script;

	String file;
	if (instance_valid_with_script && function_script && !function_script->get_script_path().is_empty()) {
		file = function_script->get_script_path();
	} else if (script) {
		file = script->get_script_path();
	}
	if (file.is_empty()) {
		file = "<built-in>";
	}

	String function_name = p_frame.function->get_name();
	if (instance_valid_with_script && GritRuntimeAccess::get_instance_script(p_frame.instance)->get_local_name() != StringName()) {
		function_name = String(GritRuntimeAccess::get_instance_script(p_frame.instance)->get_local_name()) + "." + function_name;
	}

	_err_print_error(function_name.utf8().get_data(), file.utf8().get_data(), p_line, p_message, false, ERR_HANDLER_SCRIPT);
	GDScriptLanguage::get_singleton()->debug_break(p_message, false);
#endif
}

Variant default_return(const GritFrame &p_frame) {
	return GritRuntimeAccess::get_default_return(p_frame.function);
}

Variant error_return(const GritFrame &p_frame) {
#ifdef DEBUG_ENABLED
	return default_return(p_frame);
#else
	return Variant();
#endif
}

bool member_access_error(const GritFrame &p_frame, int p_line) {
	report_error(p_frame, p_line, "Cannot access member without instance.");
	return false;
}

bool check_argument_count(const GritFrame &p_frame, int p_argcount, int p_required, int p_total, Callable::CallError &r_error) {
	if (p_argcount > p_total) {
		r_error.error = Callable::CallError::CALL_ERROR_TOO_MANY_ARGUMENTS;
		r_error.expected = p_total;
		return false;
	}
	if (p_argcount < p_required) {
		r_error.error = Callable::CallError::CALL_ERROR_TOO_FEW_ARGUMENTS;
		r_error.expected = p_required;
		return false;
	}
	return true;
}

Array rest_arguments(const Variant **p_args, int p_argcount, int p_parameter_count) {
	Array rest;
	if (p_argcount > p_parameter_count) {
		rest.resize(p_argcount - p_parameter_count);
		for (int i = 0; i < p_argcount - p_parameter_count; i++) {
			rest[i] = *p_args[i + p_parameter_count];
		}
	}
	return rest;
}

template <typename T>
static bool read_builtin_argument(const Variant **p_args, int p_index, Variant::Type p_type, T &r_value, Callable::CallError &r_error) {
	const Variant *argument = p_args[p_index];
	if (Variant::can_convert_strict(argument->get_type(), p_type)) {
		Callable::CallError construct_error;
		Variant converted;
		Variant::construct(p_type, converted, &argument, 1, construct_error);
		if (construct_error.error == Callable::CallError::CALL_OK) {
			r_value = converted;
			return true;
		}
	}

	r_error.error = Callable::CallError::CALL_ERROR_INVALID_ARGUMENT;
	r_error.argument = p_index;
	r_error.expected = p_type;
	return false;
}

bool read_converted_argument(const Variant **p_args, int p_index, int64_t &r_value, Callable::CallError &r_error) {
	return read_builtin_argument(p_args, p_index, Variant::INT, r_value, r_error);
}

bool read_converted_argument(const Variant **p_args, int p_index, double &r_value, Callable::CallError &r_error) {
	return read_builtin_argument(p_args, p_index, Variant::FLOAT, r_value, r_error);
}

bool read_converted_argument(const Variant **p_args, int p_index, bool &r_value, Callable::CallError &r_error) {
	return read_builtin_argument(p_args, p_index, Variant::BOOL, r_value, r_error);
}

bool read_typed_argument(const GritFrame &p_frame, const Variant **p_args, int p_index, Variant &r_value, Callable::CallError &r_error) {
	const GDScriptDataType &type = GritRuntimeAccess::get_argument_type(p_frame.function, p_index);
	const Variant *argument = p_args[p_index];
	if (!type.has_type() || type.is_type(*argument, false)) {
		r_value = *argument;
		return true;
	}
	if (!type.is_type(*argument, true)) {
		r_error.error = Callable::CallError::CALL_ERROR_INVALID_ARGUMENT;
		r_error.argument = p_index;
		r_error.expected = type.builtin_type;
		return false;
	}
	if (type.kind != GDScriptDataType::BUILTIN) {
		r_value = *argument;
		return true;
	}
	if (type.builtin_type == Variant::DICTIONARY && type.has_container_element_types()) {
		const GDScriptDataType &key_type = type.get_container_element_type_or_variant(0);
		const GDScriptDataType &value_type = type.get_container_element_type_or_variant(1);
		r_value = Dictionary(argument->operator Dictionary(), key_type.builtin_type, key_type.native_type, key_type.script_type, value_type.builtin_type, value_type.native_type, value_type.script_type);
		return true;
	}
	if (type.builtin_type == Variant::ARRAY && type.has_container_element_type(0)) {
		const GDScriptDataType &element_type = type.container_element_types[0];
		r_value = Array(argument->operator Array(), element_type.builtin_type, element_type.native_type, element_type.script_type);
		return true;
	}
	Variant converted;
	Variant::construct(type.builtin_type, converted, &argument, 1, r_error);
	if (unlikely(r_error.error)) {
		r_error.error = Callable::CallError::CALL_ERROR_INVALID_ARGUMENT;
		r_error.argument = p_index;
		r_error.expected = type.builtin_type;
		return false;
	}
	r_value = converted;
	return true;
}

Variant load_resource(const String &p_path) {
	return ResourceLoader::load(p_path);
}

static const GDScript *root_of(const GDScript *p_script) {
	while (p_script && !p_script->is_root_script()) {
		p_script = GritRuntimeAccess::get_owner_script(p_script);
	}
	return p_script;
}

static int root_path_length(const GDScript *p_context_root, const String &p_reference) {
	if (p_context_root) {
		const String &context_path = p_context_root->get_script_path();
		if (p_reference == context_path || p_reference.begins_with(context_path + "::")) {
			return context_path.length();
		}
	}
	int separator = p_reference.find("::");
	if (separator >= 0 && p_reference.substr(0, separator).get_extension() != "gd") {
		separator = p_reference.find("::", separator + 2);
	}
	return separator < 0 ? p_reference.length() : separator;
}

Variant load_script(const GDScript *p_context, const String &p_reference) {
	const GDScript *context_root = root_of(p_context);
	const int root_length = root_path_length(context_root, p_reference);
	const String path = p_reference.substr(0, root_length);

	Ref<Script> root;
	if (context_root && context_root->get_script_path() == path) {
		root = Ref<GDScript>(const_cast<GDScript *>(context_root));
	} else {
		root = GDScriptCache::get_cached_script(path);
		if (root.is_null()) {
			root = ResourceLoader::load(path);
		}
	}
	if (root.is_null() || root_length == p_reference.length()) {
		return root;
	}

	const GDScript *current = Object::cast_to<GDScript>(root.ptr());
	for (const String &name : p_reference.substr(root_length + 2).split("::")) {
		if (!current) {
			return Variant();
		}
		const Ref<GDScript> *subclass = current->get_subclasses().getptr(name);
		current = subclass ? subclass->ptr() : nullptr;
	}
	return Variant(current);
}

Variant engine_singleton(const StringName &p_name) {
	return Engine::get_singleton()->get_singleton_object(p_name);
}

Variant native_class(const StringName &p_name) {
	GDScriptLanguage *language = GDScriptLanguage::get_singleton();
	const int *index = language->get_global_map().getptr(p_name);
	ERR_FAIL_NULL_V_MSG(index, Variant(), vformat("Grit: native class \"%s\" is not registered.", p_name));
	return language->get_global_array()[*index];
}

Variant utility_callable(const StringName &p_name) {
	return Callable(memnew(GDScriptUtilityCallable(p_name)));
}

Variant packed_array_constant(Variant::Type p_type, std::initializer_list<Variant> p_elements) {
	Array elements;
	for (const Variant &element : p_elements) {
		elements.push_back(element);
	}
	const Variant source = elements;
	const Variant *arguments[] = { &source };
	Callable::CallError call_error;
	Variant result;
	Variant::construct(p_type, result, arguments, 1, call_error);
	return result;
}

Variant array_constant(std::initializer_list<Variant> p_elements, Variant::Type p_builtin_type, const StringName &p_native_type, const Variant &p_script_type, bool p_read_only) {
	Array array;
	if (p_builtin_type != Variant::NIL) {
		array.set_typed(p_builtin_type, p_native_type, p_script_type);
	}
	for (const Variant &element : p_elements) {
		array.push_back(element);
	}
	if (p_read_only) {
		array.make_read_only();
	}
	return array;
}

Variant dictionary_constant(std::initializer_list<Variant> p_pairs, Variant::Type p_key_builtin_type, const StringName &p_key_native_type, const Variant &p_key_script_type, Variant::Type p_value_builtin_type, const StringName &p_value_native_type, const Variant &p_value_script_type, bool p_read_only) {
	Dictionary dictionary;
	if (p_key_builtin_type != Variant::NIL || p_value_builtin_type != Variant::NIL) {
		dictionary.set_typed(p_key_builtin_type, p_key_native_type, p_key_script_type, p_value_builtin_type, p_value_native_type, p_value_script_type);
	}
	const Variant *pair = p_pairs.begin();
	while (pair != p_pairs.end()) {
		dictionary[pair[0]] = pair[1];
		pair += 2;
	}
	if (p_read_only) {
		dictionary.make_read_only();
	}
	return dictionary;
}

Variant typed_array(Variant::Type p_builtin_type, const StringName &p_native_type, const Variant &p_script_type) {
	Array array;
	array.set_typed(p_builtin_type, p_native_type, p_script_type);
	return array;
}

Variant typed_dictionary(Variant::Type p_key_builtin_type, const StringName &p_key_native_type, const Variant &p_key_script_type, Variant::Type p_value_builtin_type, const StringName &p_value_native_type, const Variant &p_value_script_type) {
	Dictionary dictionary;
	dictionary.set_typed(p_key_builtin_type, p_key_native_type, p_key_script_type, p_value_builtin_type, p_value_native_type, p_value_script_type);
	return dictionary;
}

bool assign_typed_builtin(Variant &r_target, const Variant &p_source, Variant::Type p_type, const GritFrame &p_frame, int p_line) {
	if (p_source.get_type() == p_type) {
		r_target = p_source;
		return true;
	}
#ifdef DEBUG_ENABLED
	if (!Variant::can_convert_strict(p_source.get_type(), p_type)) {
		report_error(p_frame, p_line, "Trying to assign value of type '" + Variant::get_type_name(p_source.get_type()) + "' to a variable of type '" + Variant::get_type_name(p_type) + "'.");
		return false;
	}
#endif
	Callable::CallError call_error;
	const Variant *source = &p_source;
	Variant converted;
	Variant::construct(p_type, converted, &source, 1, call_error);
	r_target = converted;
	return true;
}

bool assign_typed_array(Variant &r_target, const Variant &p_source, Variant::Type p_builtin_type, const StringName &p_native_type, const Variant &p_script_type, const GritFrame &p_frame, int p_line) {
	if (p_source.get_type() != Variant::ARRAY) {
		report_error(p_frame, p_line, vformat(R"(Trying to assign a value of type "%s" to a variable of type "Array[%s]".)", value_type_name(&p_source), element_type_name(p_builtin_type, p_native_type, p_script_type)));
		return false;
	}
	const Array *array = VariantInternal::get_array(&p_source);
	if (array->get_typed_builtin() != ((uint32_t)p_builtin_type) || array->get_typed_class_name() != p_native_type || array->get_typed_script() != p_script_type) {
		report_error(p_frame, p_line, vformat(R"(Trying to assign an array of type "%s" to a variable of type "Array[%s]".)", value_type_name(&p_source), element_type_name(p_builtin_type, p_native_type, p_script_type)));
		return false;
	}
	r_target = p_source;
	return true;
}

bool assign_typed_dictionary(Variant &r_target, const Variant &p_source, Variant::Type p_key_builtin_type, const StringName &p_key_native_type, const Variant &p_key_script_type, Variant::Type p_value_builtin_type, const StringName &p_value_native_type, const Variant &p_value_script_type, const GritFrame &p_frame, int p_line) {
	if (p_source.get_type() != Variant::DICTIONARY) {
		report_error(p_frame, p_line, vformat(R"(Trying to assign a value of type "%s" to a variable of type "Dictionary[%s, %s]".)", value_type_name(&p_source), element_type_name(p_key_builtin_type, p_key_native_type, p_key_script_type), element_type_name(p_value_builtin_type, p_value_native_type, p_value_script_type)));
		return false;
	}
	const Dictionary *dictionary = VariantInternal::get_dictionary(&p_source);
	if (dictionary->get_typed_key_builtin() != ((uint32_t)p_key_builtin_type) || dictionary->get_typed_key_class_name() != p_key_native_type || dictionary->get_typed_key_script() != p_key_script_type ||
			dictionary->get_typed_value_builtin() != ((uint32_t)p_value_builtin_type) || dictionary->get_typed_value_class_name() != p_value_native_type || dictionary->get_typed_value_script() != p_value_script_type) {
		report_error(p_frame, p_line, vformat(R"(Trying to assign a dictionary of type "%s" to a variable of type "Dictionary[%s, %s]".)", value_type_name(&p_source), element_type_name(p_key_builtin_type, p_key_native_type, p_key_script_type), element_type_name(p_value_builtin_type, p_value_native_type, p_value_script_type)));
		return false;
	}
	r_target = p_source;
	return true;
}

bool assign_typed_native(Variant &r_target, const Variant &p_source, const StringName &p_native_type, const GritFrame &p_frame, int p_line) {
#ifdef DEBUG_ENABLED
	if (p_source.get_type() != Variant::OBJECT && p_source.get_type() != Variant::NIL) {
		report_error(p_frame, p_line, "Trying to assign value of type '" + Variant::get_type_name(p_source.get_type()) + "' to a variable of type '" + p_native_type + "'.");
		return false;
	}
	if (p_source.get_type() == Variant::OBJECT) {
		bool was_freed = false;
		Object *object = p_source.get_validated_object_with_check(was_freed);
		if (!object && was_freed) {
			report_error(p_frame, p_line, "Trying to assign invalid previously freed instance.");
			return false;
		}
		if (object && !ClassDB::is_parent_class(object->get_class_name(), p_native_type)) {
			report_error(p_frame, p_line, "Trying to assign value of type '" + object->get_class_name() + "' to a variable of type '" + p_native_type + "'.");
			return false;
		}
	}
#endif
	r_target = p_source;
	return true;
}

bool assign_typed_script(Variant &r_target, const Variant &p_source, const Variant &p_script_type, const GritFrame &p_frame, int p_line) {
#ifdef DEBUG_ENABLED
	const Script *base_type = Object::cast_to<Script>(p_script_type.operator Object *());
	if (!base_type) {
		return false;
	}
	if (p_source.get_type() != Variant::OBJECT && p_source.get_type() != Variant::NIL) {
		report_error(p_frame, p_line, "Trying to assign a non-object value to a variable of type '" + base_type->get_path().get_file() + "'.");
		return false;
	}
	if (p_source.get_type() == Variant::OBJECT) {
		bool was_freed = false;
		Object *object = p_source.get_validated_object_with_check(was_freed);
		if (!object && was_freed) {
			report_error(p_frame, p_line, "Trying to assign invalid previously freed instance.");
			return false;
		}
		if (object) {
			if (!object->get_script_instance()) {
				report_error(p_frame, p_line, "Trying to assign value of type '" + object->get_class_name() + "' to a variable of type '" + base_type->get_path().get_file() + "'.");
				return false;
			}
			if (!script_inherits(object, base_type)) {
				report_error(p_frame, p_line, "Trying to assign value of type '" + object->get_script_instance()->get_script()->get_path().get_file() + "' to a variable of type '" + base_type->get_path().get_file() + "'.");
				return false;
			}
		}
	}
#endif
	r_target = p_source;
	return true;
}

bool evaluate(Variant::Operator p_operator, const Variant &p_left, const Variant &p_right, Variant &r_target, const GritFrame &p_frame, int p_line) {
	bool valid = false;
	Variant result;
	Variant::evaluate(p_operator, p_left, p_right, result, valid);
	if (!valid) {
		if (result.get_type() == Variant::STRING) {
			report_error(p_frame, p_line, result.operator String() + " in operator '" + Variant::get_operator_name(p_operator) + "'.");
		} else {
			report_error(p_frame, p_line, "Invalid operands '" + Variant::get_type_name(p_left.get_type()) + "' and '" + Variant::get_type_name(p_right.get_type()) + "' in operator '" + Variant::get_operator_name(p_operator) + "'.");
		}
		return false;
	}
	r_target = std::move(result);
	return true;
}

bool is_typed_array(const Variant &p_value, Variant::Type p_builtin_type, const StringName &p_native_type, const Variant &p_script_type) {
	if (p_value.get_type() != Variant::ARRAY) {
		return false;
	}
	const Array *array = VariantInternal::get_array(&p_value);
	return array->get_typed_builtin() == ((uint32_t)p_builtin_type) && array->get_typed_class_name() == p_native_type && array->get_typed_script() == p_script_type;
}

bool is_typed_dictionary(const Variant &p_value, Variant::Type p_key_builtin_type, const StringName &p_key_native_type, const Variant &p_key_script_type, Variant::Type p_value_builtin_type, const StringName &p_value_native_type, const Variant &p_value_script_type) {
	if (p_value.get_type() != Variant::DICTIONARY) {
		return false;
	}
	const Dictionary *dictionary = VariantInternal::get_dictionary(&p_value);
	return dictionary->get_typed_key_builtin() == ((uint32_t)p_key_builtin_type) && dictionary->get_typed_key_class_name() == p_key_native_type && dictionary->get_typed_key_script() == p_key_script_type &&
			dictionary->get_typed_value_builtin() == ((uint32_t)p_value_builtin_type) && dictionary->get_typed_value_class_name() == p_value_native_type && dictionary->get_typed_value_script() == p_value_script_type;
}

bool type_test_native(const Variant &p_value, const StringName &p_native_type, bool &r_result, const GritFrame &p_frame, int p_line) {
	bool was_freed = false;
	Object *object = p_value.get_validated_object_with_check(was_freed);
	if (was_freed) {
		report_error(p_frame, p_line, "Left operand of 'is' is a previously freed instance.");
		return false;
	}
	r_result = object && ClassDB::is_parent_class(object->get_class_name(), p_native_type);
	return true;
}

bool type_test_script(const Variant &p_value, const Variant &p_script_type, bool &r_result, const GritFrame &p_frame, int p_line) {
	const Script *script_type = Object::cast_to<Script>(p_script_type.operator Object *());
	if (!script_type) {
		return false;
	}
	bool was_freed = false;
	Object *object = p_value.get_validated_object_with_check(was_freed);
	if (was_freed) {
		report_error(p_frame, p_line, "Left operand of 'is' is a previously freed instance.");
		return false;
	}
	r_result = script_inherits(object, script_type);
	return true;
}

bool cast_to_builtin(const Variant &p_value, Variant::Type p_type, Variant &r_target, const GritFrame &p_frame, int p_line) {
#ifdef DEBUG_ENABLED
	if (p_value.operator Object *() && !p_value.get_validated_object()) {
		report_error(p_frame, p_line, "Trying to cast a freed object.");
		return false;
	}
#endif
	Callable::CallError call_error;
	const Variant *source = &p_value;
	Variant result;
	Variant::construct(p_type, result, &source, 1, call_error);
#ifdef DEBUG_ENABLED
	if (call_error.error != Callable::CallError::CALL_OK) {
		report_error(p_frame, p_line, "Invalid cast: could not convert value to '" + Variant::get_type_name(p_type) + "'.");
		return false;
	}
#endif
	r_target = std::move(result);
	return true;
}

bool cast_to_native(const Variant &p_value, const StringName &p_native_type, Variant &r_target, const GritFrame &p_frame, int p_line) {
#ifdef DEBUG_ENABLED
	if (p_value.operator Object *() && !p_value.get_validated_object()) {
		report_error(p_frame, p_line, "Trying to cast a freed object.");
		return false;
	}
	if (p_value.get_type() != Variant::OBJECT && p_value.get_type() != Variant::NIL) {
		report_error(p_frame, p_line, "Invalid cast: can't convert a non-object value to an object type.");
		return false;
	}
#endif
	const Object *object = p_value.operator Object *();
	if (object && !ClassDB::is_parent_class(object->get_class_name(), p_native_type)) {
		r_target = Variant();
	} else {
		r_target = p_value;
	}
	return true;
}

bool cast_to_script(const Variant &p_value, const Variant &p_script_type, Variant &r_target, const GritFrame &p_frame, int p_line) {
	const Script *base_type = Object::cast_to<Script>(p_script_type.operator Object *());
	if (!base_type) {
		return false;
	}
#ifdef DEBUG_ENABLED
	if (p_value.operator Object *() && !p_value.get_validated_object()) {
		report_error(p_frame, p_line, "Trying to cast a freed object.");
		return false;
	}
	if (p_value.get_type() != Variant::OBJECT && p_value.get_type() != Variant::NIL) {
		report_error(p_frame, p_line, "Trying to assign a non-object value to a variable of type '" + base_type->get_path().get_file() + "'.");
		return false;
	}
#endif
	const bool valid = p_value.get_type() != Variant::NIL && script_inherits(p_value.operator Object *(), base_type);
	if (valid) {
		r_target = p_value;
	} else {
		r_target = Variant();
	}
	return true;
}

bool get_keyed(const Variant &p_base, const Variant &p_key, Variant &r_target, const GritFrame &p_frame, int p_line) {
	bool valid = false;
#ifdef DEBUG_ENABLED
	Variant::VariantGetError error_code = Variant::GET_OK;
	Variant result = p_base.get(p_key, &valid, &error_code);
	if (!valid) {
		if (error_code == Variant::VariantGetError::GET_INDEXED_ERR) {
			report_error(p_frame, p_line, "Invalid access of index " + key_description(p_key) + " on a base object of type: '" + value_type_name(&p_base) + "'.");
		} else {
			report_error(p_frame, p_line, "Invalid access to property or key " + key_description(p_key) + " on a base object of type '" + value_type_name(&p_base) + "'.");
		}
		return false;
	}
	r_target = std::move(result);
#else
	r_target = p_base.get(p_key, &valid);
#endif
	return true;
}

#ifdef DEBUG_ENABLED
static String set_error_message(const Variant &p_base, const Variant &p_key, const Variant &p_value, bool p_indexed_error) {
	if (p_base.is_read_only()) {
		return "Invalid assignment on read-only value (on base: '" + value_type_name(&p_base) + "').";
	}
	const Object *object = p_base.get_validated_object();
	const String key = p_key.operator String();
	if (object && ClassDB::has_property(object->get_class_name(), key) && ClassDB::get_property_setter(object->get_class_name(), key) == StringName()) {
		return vformat(R"(Cannot set value into property "%s" (on base "%s") because it is read-only.)", key, value_type_name(&p_base));
	}
	if (p_indexed_error) {
		return "Invalid assignment of index " + key_description(p_key) + " (on base: '" + value_type_name(&p_base) + "') with value of type '" + value_type_name(&p_value) + "'.";
	}
	return "Invalid assignment of property or key " + key_description(p_key) + " with value of type '" + value_type_name(&p_value) + "' on a base object of type '" + value_type_name(&p_base) + "'.";
}
#endif

bool set_keyed(Variant &r_base, const Variant &p_key, const Variant &p_value, const GritFrame &p_frame, int p_line) {
	bool valid = false;
#ifdef DEBUG_ENABLED
	Variant::VariantSetError error_code = Variant::SET_OK;
	r_base.set(p_key, p_value, &valid, &error_code);
	if (!valid) {
		report_error(p_frame, p_line, set_error_message(r_base, p_key, p_value, error_code == Variant::VariantSetError::SET_INDEXED_ERR));
		return false;
	}
#else
	r_base.set(p_key, p_value, &valid);
#endif
	return true;
}

bool missing_key(const Variant &p_base, const Variant &p_key, const GritFrame &p_frame, int p_line) {
#ifdef DEBUG_ENABLED
	report_error(p_frame, p_line, "Invalid access to property or key " + key_description(p_key) + " on a base object of type '" + value_type_name(&p_base) + "'.");
	return false;
#else
	return true;
#endif
}

bool keyed_set_failed(const Variant &p_base, const Variant &p_key, const Variant &p_value, const GritFrame &p_frame, int p_line) {
#ifdef DEBUG_ENABLED
	if (p_base.is_read_only()) {
		report_error(p_frame, p_line, "Invalid assignment on read-only value (on base: '" + value_type_name(&p_base) + "').");
	} else {
		report_error(p_frame, p_line, "Invalid assignment of property or key " + key_description(p_key) + " with value of type '" + value_type_name(&p_value) + "' on a base object of type '" + value_type_name(&p_base) + "'.");
	}
	return false;
#else
	return true;
#endif
}

bool get_keyed_validated(Variant::ValidatedKeyedGetter p_getter, const Variant &p_base, const Variant &p_key, Variant &r_target, const GritFrame &p_frame, int p_line) {
	bool valid = false;
#ifdef DEBUG_ENABLED
	Variant result;
	p_getter(&p_base, &p_key, &result, &valid);
	if (!valid) {
		return missing_key(p_base, p_key, p_frame, p_line);
	}
	r_target = std::move(result);
#else
	p_getter(&p_base, &p_key, &r_target, &valid);
#endif
	return true;
}

bool set_keyed_validated(Variant::ValidatedKeyedSetter p_setter, Variant &r_base, const Variant &p_key, const Variant &p_value, const GritFrame &p_frame, int p_line) {
	bool valid = false;
	p_setter(&r_base, &p_key, &p_value, &valid);
	if (!valid) {
		return keyed_set_failed(r_base, p_key, p_value, p_frame, p_line);
	}
	return true;
}

bool get_indexed_validated(Variant::ValidatedIndexedGetter p_getter, const Variant &p_base, const Variant &p_index, Variant &r_target, const GritFrame &p_frame, int p_line) {
	prepare(r_target, Variant::get_indexed_element_type(p_base.get_type()));
	bool out_of_bounds = false;
	p_getter(&p_base, *VariantInternal::get_int(&p_index), &r_target, &out_of_bounds);
#ifdef DEBUG_ENABLED
	if (out_of_bounds) {
		report_error(p_frame, p_line, "Out of bounds get index " + key_description(p_index) + " (on base: '" + value_type_name(&p_base) + "')");
		return false;
	}
#endif
	return true;
}

bool set_indexed_validated(Variant::ValidatedIndexedSetter p_setter, Variant &r_base, const Variant &p_index, const Variant &p_value, const GritFrame &p_frame, int p_line) {
	bool out_of_bounds = false;
	p_setter(&r_base, *VariantInternal::get_int(&p_index), &p_value, &out_of_bounds);
#ifdef DEBUG_ENABLED
	if (out_of_bounds) {
		if (r_base.is_read_only()) {
			report_error(p_frame, p_line, "Invalid assignment on read-only value (on base: '" + value_type_name(&r_base) + "').");
		} else {
			report_error(p_frame, p_line, "Out of bounds set index " + key_description(p_index) + " (on base: '" + value_type_name(&r_base) + "')");
		}
		return false;
	}
#endif
	return true;
}

bool out_of_bounds_get(const Variant &p_base, int64_t p_index, const GritFrame &p_frame, int p_line) {
#ifdef DEBUG_ENABLED
	report_error(p_frame, p_line, "Out of bounds get index " + key_description(Variant(p_index)) + " (on base: '" + value_type_name(&p_base) + "')");
	return false;
#else
	return true;
#endif
}

bool out_of_bounds_set(const Variant &p_base, int64_t p_index, const GritFrame &p_frame, int p_line) {
#ifdef DEBUG_ENABLED
	if (p_base.is_read_only()) {
		report_error(p_frame, p_line, "Invalid assignment on read-only value (on base: '" + value_type_name(&p_base) + "').");
	} else {
		report_error(p_frame, p_line, "Out of bounds set index " + key_description(Variant(p_index)) + " (on base: '" + value_type_name(&p_base) + "')");
	}
	return false;
#else
	return true;
#endif
}

bool get_named(const Variant &p_base, const StringName &p_name, Variant &r_target, const GritFrame &p_frame, int p_line) {
	bool valid = false;
#ifdef DEBUG_ENABLED
	Variant result = p_base.get_named(p_name, valid);
	if (!valid) {
		report_error(p_frame, p_line, "Invalid access to property or key '" + p_name.operator String() + "' on a base object of type '" + value_type_name(&p_base) + "'.");
		return false;
	}
	r_target = std::move(result);
#else
	r_target = p_base.get_named(p_name, valid);
#endif
	return true;
}

bool set_named(Variant &r_base, const StringName &p_name, const Variant &p_value, const GritFrame &p_frame, int p_line) {
	bool valid = false;
	r_base.set_named(p_name, p_value, valid);
#ifdef DEBUG_ENABLED
	if (!valid) {
		if (r_base.is_read_only()) {
			report_error(p_frame, p_line, "Invalid assignment on read-only value (on base: '" + value_type_name(&r_base) + "').");
			return false;
		}
		const Object *object = r_base.get_validated_object();
		if (object && ClassDB::has_property(object->get_class_name(), p_name) && ClassDB::get_property_setter(object->get_class_name(), p_name) == StringName()) {
			report_error(p_frame, p_line, vformat(R"(Cannot set value into property "%s" (on base "%s") because it is read-only.)", String(p_name), value_type_name(&r_base)));
		} else {
			report_error(p_frame, p_line, "Invalid assignment of property or key '" + String(p_name) + "' with value of type '" + value_type_name(&p_value) + "' on a base object of type '" + value_type_name(&r_base) + "'.");
		}
		return false;
	}
#endif
	return true;
}

bool get_property(const StringName &p_name, Variant &r_target, const GritFrame &p_frame, int p_line) {
	if (!p_frame.instance) {
		return member_access_error(p_frame, p_line);
	}
	[[maybe_unused]] const bool found = ClassDB::get_property(GritRuntimeAccess::get_owner(p_frame.instance), p_name, r_target);
#ifdef DEBUG_ENABLED
	if (!found) {
		report_error(p_frame, p_line, "Internal error getting property: " + String(p_name));
		return false;
	}
#endif
	return true;
}

bool set_property(const StringName &p_name, const Variant &p_value, const GritFrame &p_frame, int p_line) {
	if (!p_frame.instance) {
		return member_access_error(p_frame, p_line);
	}
	bool valid = false;
	[[maybe_unused]] const bool found = ClassDB::set_property(GritRuntimeAccess::get_owner(p_frame.instance), p_name, p_value, &valid);
#ifdef DEBUG_ENABLED
	if (!found) {
		report_error(p_frame, p_line, "Internal error setting property: " + String(p_name));
		return false;
	}
	if (!valid) {
		report_error(p_frame, p_line, "Error setting property '" + String(p_name) + "' with value of type " + Variant::get_type_name(p_value.get_type()) + ".");
		return false;
	}
#endif
	return true;
}

struct PropertyKey {
	const GDType *type = nullptr;
	StringName name;

	bool operator==(const PropertyKey &p_other) const { return type == p_other.type && name == p_other.name; }
};

struct PropertyKeyHasher {
	static uint32_t hash(const PropertyKey &p_key) {
		return hash_fmix32(hash_murmur3_one_64(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(p_key.type)), p_key.name.hash()));
	}
};

static Mutex property_mutex;
static HashMap<PropertyKey, PropertyAccessors *, PropertyKeyHasher> property_accessors_by_key;

static bool overrides_object_call(const StringName &p_class) {
	return ClassDB::is_parent_class(p_class, Script::get_class_static());
}

static void resolve_accessor(const StringName &p_class, const StringName &p_method_name, MethodBind *p_direct_method, int p_index, bool p_is_setter, PropertyAccessor &r_accessor) {
	r_accessor.method_name = p_method_name;
	r_accessor.index = p_index;
	r_accessor.is_call = p_direct_method == nullptr;
	r_accessor.method = r_accessor.is_call ? ClassDB::get_method(p_class, p_method_name) : p_direct_method;

	const MethodBind *method = r_accessor.method;
	if (!method || method->is_vararg() || (r_accessor.is_call && overrides_object_call(p_class))) {
		return;
	}
	const int index_count = p_index >= 0 ? 1 : 0;
	if (method->get_argument_count() != index_count + (p_is_setter ? 1 : 0)) {
		return;
	}
	if (index_count > 0 && method->get_argument_type(0) != Variant::INT) {
		return;
	}
	if (!p_is_setter && !method->has_return()) {
		return;
	}
	const Variant::Type type = method->get_argument_type(p_is_setter ? index_count : -1);
	if (type == Variant::OBJECT) {
		r_accessor.value_type = Variant::OBJECT;
		const StringName argument_class = p_is_setter ? method->get_argument_info(index_count).class_name : method->get_return_info().class_name;
		r_accessor.object_class = argument_class;
		if (!p_is_setter) {
			r_accessor.returns_ref = argument_class != StringName() && ClassDB::is_parent_class(argument_class, SNAME("RefCounted"));
		}
	} else if (type < Variant::VARIANT_MAX) {
		r_accessor.value_type = type;
	}
}

void resolve_property_accessors(const StringName &p_class, const StringName &p_name, PropertyAccessors &r_accessors) {
	ClassDB::Locker::Lock lock(ClassDB::Locker::STATE_READ);
	const ClassDB::ClassInfo *type = ClassDB::classes.getptr(p_class);

	for (const ClassDB::ClassInfo *check = type; check; check = check->inherits_ptr) {
		const ClassDB::PropertySetGet *property = check->property_setget.getptr(p_name);
		if (property) {
			if (property->getter != StringName()) {
				resolve_accessor(p_class, property->getter, property->index < 0 ? property->_getptr : nullptr, property->index, false, r_accessors.getter);
			}
			break;
		}
		if (check->gdtype->get_integer_constant_map(true).has(p_name) || check->method_map.has(p_name) || check->gdtype->get_signal_map(true).has(p_name)) {
			break;
		}
	}

	for (const ClassDB::ClassInfo *check = type; check; check = check->inherits_ptr) {
		const ClassDB::PropertySetGet *property = check->property_setget.getptr(p_name);
		if (property) {
			if (property->setter != StringName()) {
				resolve_accessor(p_class, property->setter, property->_setptr, property->index, true, r_accessors.setter);
			}
			break;
		}
	}
}

const PropertyAccessors *insert_property_accessors(PropertyCache &r_cache, const GDType *p_type, const StringName &p_name) {
	const PropertyAccessors *accessors = nullptr;
	{
		MutexLock lock(property_mutex);
		const PropertyKey key = { p_type, p_name };
		PropertyAccessors **existing = property_accessors_by_key.getptr(key);
		if (existing) {
			accessors = *existing;
		} else {
			PropertyAccessors *created = memnew(PropertyAccessors);
			created->type = p_type;
			resolve_property_accessors(p_type->get_name(), p_name, *created);
			property_accessors_by_key.insert(key, created);
			accessors = created;
		}
	}
	for (std::atomic<const PropertyAccessors *> &slot : r_cache.slots) {
		const PropertyAccessors *expected = nullptr;
		if (slot.compare_exchange_strong(expected, accessors, std::memory_order_acq_rel, std::memory_order_acquire) || expected->type == p_type) {
			break;
		}
	}
	return accessors;
}

void clear_property_accessors() {
	MutexLock lock(property_mutex);
	for (KeyValue<PropertyKey, PropertyAccessors *> &entry : property_accessors_by_key) {
		memdelete(entry.value);
	}
	property_accessors_by_key = HashMap<PropertyKey, PropertyAccessors *, PropertyKeyHasher>();
}

static GDScriptInstance *as_gdscript_instance(ScriptInstance *p_instance) {
	if (p_instance->is_placeholder() || p_instance->get_language() != GDScriptLanguage::get_singleton()) {
		return nullptr;
	}
	return static_cast<GDScriptInstance *>(p_instance);
}

bool script_intercepts_get(ScriptInstance *p_instance, const StringName &p_name) {
	const GDScriptInstance *instance = as_gdscript_instance(p_instance);
	if (!instance) {
		return true;
	}
	const GDScript *script = GritRuntimeAccess::get_instance_script(instance);
	if (GritRuntimeAccess::get_member_indices(script).has(p_name)) {
		return true;
	}
	const StringName &getter_name = GritRuntimeAccess::get_getter_name();
	for (const GDScript *level = script; level; level = GritRuntimeAccess::get_base_script(level)) {
		if (GritRuntimeAccess::get_constants(level).has(p_name) || GritRuntimeAccess::get_static_variable_indices(level).has(p_name) || GritRuntimeAccess::get_signals(level).has(p_name) || GritRuntimeAccess::get_subclasses(level).has(p_name)) {
			return true;
		}
		if (GritRuntimeAccess::is_script_valid(level)) {
			const HashMap<StringName, GDScriptFunction *> &functions = GritRuntimeAccess::get_member_functions(level);
			if (functions.has(p_name) || functions.has(getter_name)) {
				return true;
			}
		}
	}
	return false;
}

bool script_intercepts_set(ScriptInstance *p_instance, const StringName &p_name) {
	const GDScriptInstance *instance = as_gdscript_instance(p_instance);
	if (!instance) {
		return true;
	}
	const GDScript *script = GritRuntimeAccess::get_instance_script(instance);
	if (GritRuntimeAccess::get_member_indices(script).has(p_name)) {
		return true;
	}
	const StringName &setter_name = GritRuntimeAccess::get_setter_name();
	for (const GDScript *level = script; level; level = GritRuntimeAccess::get_base_script(level)) {
		if (GritRuntimeAccess::get_static_variable_indices(level).has(p_name)) {
			return true;
		}
		if (GritRuntimeAccess::is_script_valid(level) && GritRuntimeAccess::get_member_functions(level).has(setter_name)) {
			return true;
		}
	}
	return false;
}

bool script_intercepts_call(ScriptInstance *p_instance, const StringName &p_method) {
	GDScriptInstance *instance = as_gdscript_instance(p_instance);
	return !instance || find_instance_function(instance, p_method) != nullptr;
}

void call_property_accessor(const PropertyAccessor &p_accessor, Object *p_object, const void *p_value, void *r_value) {
	const int64_t index = p_accessor.index;
	const void *arguments[2] = {};
	int argument_count = 0;
	if (p_accessor.index >= 0) {
		arguments[argument_count++] = &index;
	}
	if (p_value) {
		arguments[argument_count++] = p_value;
	}
#ifdef DEBUG_ENABLED
	ObjectID locked_object;
	if (p_accessor.is_call) {
		locked_object = p_object->get_instance_id();
		GritRuntimeAccess::lock_object(p_object);
	}
#endif
	p_accessor.method->ptrcall(p_object, arguments, r_value);
#ifdef DEBUG_ENABLED
	if (locked_object.is_valid()) {
		Object *object = ObjectDB::get_instance(locked_object);
		if (likely(object)) {
			GritRuntimeAccess::unlock_object(object);
		}
	}
#endif
}

Variant read_object_property(const PropertyAccessor &p_getter, Object *p_object) {
	const int64_t index = p_getter.index;
	const void *arguments[1] = { &index };
	const void **argument_ptr = p_getter.index >= 0 ? arguments : nullptr;
#ifdef DEBUG_ENABLED
	ObjectID locked_object;
	if (p_getter.is_call) {
		locked_object = p_object->get_instance_id();
		GritRuntimeAccess::lock_object(p_object);
	}
#endif
	Variant result;
	if (p_getter.returns_ref) {
		Ref<RefCounted> reference;
		p_getter.method->ptrcall(p_object, argument_ptr, &reference);
		result = reference.ptr();
	} else {
		Object *object = nullptr;
		p_getter.method->ptrcall(p_object, argument_ptr, &object);
		result = object;
	}
#ifdef DEBUG_ENABLED
	if (locked_object.is_valid()) {
		Object *object = ObjectDB::get_instance(locked_object);
		if (likely(object)) {
			GritRuntimeAccess::unlock_object(object);
		}
	}
#endif
	return result;
}

Variant *static_variable(const Variant &p_class, int p_index) {
	GDScript *script = Object::cast_to<GDScript>(p_class.operator Object *());
	if (!script) {
		return nullptr;
	}
	Vector<Variant> &variables = GritRuntimeAccess::get_static_variables(script);
	if (p_index < 0 || p_index >= variables.size()) {
		return nullptr;
	}
	return &variables.write[p_index];
}

bool get_global(const StringName &p_name, Variant &r_target, const GritFrame &p_frame, int p_line) {
	GDScriptLanguage *language = GDScriptLanguage::get_singleton();
	const int *index = language->get_global_map().getptr(p_name);
	if (index) {
		r_target = language->get_global_array()[*index];
		return true;
	}
	const Variant *named = language->get_named_globals_map().getptr(p_name);
	if (named) {
		r_target = *named;
		return true;
	}
	report_error(p_frame, p_line, vformat(R"(Trying to access non-existent autoload singleton "%s".)", p_name));
	return false;
}

bool construct(Variant::Type p_type, const Variant **p_args, int p_argcount, Variant &r_target, const GritFrame &p_frame, int p_line) {
	Callable::CallError call_error;
	Variant result;
	Variant::construct(p_type, result, p_args, p_argcount, call_error);
#ifdef DEBUG_ENABLED
	if (call_error.error != Callable::CallError::CALL_OK) {
		report_error(p_frame, p_line, GritRuntimeAccess::get_call_error(p_frame.function, "'" + Variant::get_type_name(p_type) + "' constructor", p_args, p_argcount, result, call_error));
		return false;
	}
#endif
	r_target = std::move(result);
	return true;
}

Variant array_literal(const Variant **p_args, int p_argcount) {
	Array array;
	array.resize(p_argcount);
	for (int i = 0; i < p_argcount; i++) {
		array[i] = *p_args[i];
	}
	return array;
}

Variant typed_array_literal(const Variant **p_args, int p_argcount, Variant::Type p_builtin_type, const StringName &p_native_type, const Variant &p_script_type) {
	Array array;
	array.set_typed(p_builtin_type, p_native_type, p_script_type);
	array.resize(p_argcount);
	for (int i = 0; i < p_argcount; i++) {
		array.set(i, *p_args[i]);
	}
	return array;
}

Variant dictionary_literal(const Variant **p_args, int p_pair_count) {
	Dictionary dictionary;
	dictionary.reserve(p_pair_count);
	for (int i = 0; i < p_pair_count; i++) {
		dictionary[*p_args[i * 2]] = *p_args[i * 2 + 1];
	}
	return dictionary;
}

Variant typed_dictionary_literal(const Variant **p_args, int p_pair_count, Variant::Type p_key_builtin_type, const StringName &p_key_native_type, const Variant &p_key_script_type, Variant::Type p_value_builtin_type, const StringName &p_value_native_type, const Variant &p_value_script_type) {
	Dictionary dictionary;
	dictionary.set_typed(p_key_builtin_type, p_key_native_type, p_key_script_type, p_value_builtin_type, p_value_native_type, p_value_script_type);
	dictionary.reserve(p_pair_count);
	for (int i = 0; i < p_pair_count; i++) {
		dictionary.set(*p_args[i * 2], *p_args[i * 2 + 1]);
	}
	return dictionary;
}

#ifdef DEBUG_ENABLED
static String call_error_method_name(const String &p_method, const Variant **p_args, int p_argcount, Callable::CallError &r_error) {
	if (p_method == "call" && p_argcount >= 1) {
		if (r_error.error == Callable::CallError::CALL_ERROR_INVALID_ARGUMENT) {
			r_error.argument += 1;
		}
		return String(*p_args[0]) + " (via call)";
	}
	return p_method;
}
#endif

bool call(Variant &r_base, const StringName &p_method, const Variant **p_args, int p_argcount, Variant *r_return, bool p_is_async, const GritFrame &p_frame, int p_line) {
#ifdef DEBUG_ENABLED
	const Variant::Type base_type = r_base.get_type();
	const Object *base_object = r_base.get_validated_object();
	const StringName base_class = base_object ? base_object->get_class_name() : StringName();
#endif
	Variant result;
	Callable::CallError call_error;
	r_base.callp(p_method, p_args, p_argcount, result, call_error);
	if (r_return) {
		*r_return = result;
#ifdef DEBUG_ENABLED
		if (r_return->get_type() == Variant::NIL) {
			bool returns_void = false;
			if (base_type == Variant::OBJECT) {
				if (base_object) {
					const MethodBind *method = ClassDB::get_method(base_class, p_method);
					returns_void = p_method == CoreStringName(free_) || (method && !method->has_return());
				}
			} else {
				returns_void = Variant::has_builtin_method(base_type, p_method) && !Variant::has_builtin_method_return_value(base_type, p_method);
			}
			if (returns_void) {
				report_error(p_frame, p_line, R"(Trying to get a return value of a method that returns "void")");
				return false;
			}
		}
		if (!p_is_async && r_return->get_type() == Variant::OBJECT) {
			bool was_freed = false;
			const Object *object = r_return->get_validated_object_with_check(was_freed);
			if (object && object->is_class_ptr(GDScriptFunctionState::get_class_ptr_static())) {
				report_error(p_frame, p_line, R"(Trying to call an async function without "await".)");
				return false;
			}
		}
#endif
	}
#ifdef DEBUG_ENABLED
	if (call_error.error != Callable::CallError::CALL_OK) {
		String method = p_method;
		const String base = value_type_name(&r_base);
		bool is_callable = false;
		if (method == "call") {
			if (p_argcount >= 1 && r_base.get_type() != Variant::CALLABLE) {
				method = String(*p_args[0]) + " (via call)";
				if (call_error.error == Callable::CallError::CALL_ERROR_INVALID_ARGUMENT) {
					call_error.argument += 1;
				}
			} else {
				method = r_base.operator String() + " (Callable)";
				is_callable = true;
			}
		} else if (method == "free") {
			if (call_error.error == Callable::CallError::CALL_ERROR_INVALID_METHOD) {
				if (r_base.is_ref_counted()) {
					report_error(p_frame, p_line, "Attempted to free a RefCounted object.");
					return false;
				} else if (r_base.get_type() == Variant::OBJECT) {
					report_error(p_frame, p_line, "Attempted to free a locked object (calling or emitting).");
					return false;
				}
			}
		} else if (method == "call_recursive" && base == "TreeItem") {
			if (p_argcount >= 1) {
				method = String(*p_args[0]) + " (via TreeItem.call_recursive)";
				if (call_error.error == Callable::CallError::CALL_ERROR_INVALID_ARGUMENT) {
					call_error.argument += 1;
				}
			}
		}

		if (is_callable) {
			report_error(p_frame, p_line, GritRuntimeAccess::get_callable_call_error(p_frame.function, vformat("function '%s'", method), r_base, p_args, p_argcount, result, call_error));
		} else {
			report_error(p_frame, p_line, GritRuntimeAccess::get_call_error(p_frame.function, vformat("function '%s' in base '%s'", method, base), p_args, p_argcount, result, call_error));
		}
		return false;
	}
#endif
	return true;
}

static bool resolve_method_base(const MethodBind *p_method, const Variant &p_base, Object *&r_object, const GritFrame &p_frame, int p_line) {
	bool was_freed = false;
	r_object = p_base.get_validated_object_with_check(was_freed);
	if (was_freed) {
		report_error(p_frame, p_line, "Cannot call method '" + p_method->get_name() + "' on a previously freed instance.");
		return false;
	}
	if (!r_object) {
		report_error(p_frame, p_line, "Cannot call method '" + p_method->get_name() + "' on a null value.");
		return false;
	}
	return true;
}

GDScriptInstance *gdscript_instance_of(const Variant &p_value) {
	if (p_value.get_type() != Variant::OBJECT) {
		return nullptr;
	}
	bool was_freed = false;
	Object *object = p_value.get_validated_object_with_check(was_freed);
	if (!object) {
		return nullptr;
	}
	ScriptInstance *instance = object->get_script_instance();
	if (!instance || instance->is_placeholder() || instance->get_language() != GDScriptLanguage::get_singleton()) {
		return nullptr;
	}
	return static_cast<GDScriptInstance *>(instance);
}

static const GritRuntimeAccess::MemberInfo *script_member_info(GDScriptInstance *p_instance, const StringName &p_name) {
	const GritRuntimeAccess::MemberInfo *member = GritRuntimeAccess::get_member_indices(GritRuntimeAccess::get_instance_script(p_instance)).getptr(p_name);
	if (!member || member->index < 0 || member->index >= GritRuntimeAccess::get_member_count(p_instance)) {
		return nullptr;
	}
	return member;
}

const Variant *readable_script_member(const Variant &p_base, const StringName &p_name) {
	GDScriptInstance *instance = gdscript_instance_of(p_base);
	if (!instance) {
		return nullptr;
	}
	const GritRuntimeAccess::MemberInfo *member = script_member_info(instance, p_name);
	if (!member || (member->getter != StringName() && GritRuntimeAccess::is_script_valid(GritRuntimeAccess::get_instance_script(instance)))) {
		return nullptr;
	}
	return &GritRuntimeAccess::get_members(instance)[member->index];
}

Variant *writable_script_member(const Variant &p_base, const StringName &p_name, const Variant &p_value) {
	GDScriptInstance *instance = gdscript_instance_of(p_base);
	if (!instance) {
		return nullptr;
	}
	const GritRuntimeAccess::MemberInfo *member = script_member_info(instance, p_name);
	if (!member || !member->data_type.is_type(p_value) || (member->setter != StringName() && GritRuntimeAccess::is_script_valid(GritRuntimeAccess::get_instance_script(instance)))) {
		return nullptr;
	}
	mark_edited(GritRuntimeAccess::get_owner(instance));
	return &GritRuntimeAccess::get_members(instance)[member->index];
}

void stack_overflow(GDScriptFunction *p_function, GDScriptInstance *p_instance) {
#ifdef DEBUG_ENABLED
	const bool instance_valid = p_instance && ObjectDB::get_instance(GritRuntimeAccess::get_owner_id(p_instance)) != nullptr && GritRuntimeAccess::get_instance_script(p_instance)->is_valid();
	String file;
	if (instance_valid && !GritRuntimeAccess::get_instance_script(p_instance)->get_script_path().is_empty()) {
		file = GritRuntimeAccess::get_instance_script(p_instance)->get_script_path();
	} else if (GritRuntimeAccess::get_function_script(p_function)) {
		file = GritRuntimeAccess::get_function_script(p_function)->get_script_path();
	}
	if (file.is_empty()) {
		file = "<built-in>";
	}
	String function_name = p_function->get_name();
	if (instance_valid && GritRuntimeAccess::get_instance_script(p_instance)->get_local_name() != StringName()) {
		function_name = String(GritRuntimeAccess::get_instance_script(p_instance)->get_local_name()) + "." + function_name;
	}
	const char *message = "Stack overflow. Check for infinite recursion in your script.";
	_err_print_error(function_name.utf8().get_data(), file.utf8().get_data(), GritRuntimeAccess::get_initial_line(p_function), message, false, ERR_HANDLER_SCRIPT);
	GDScriptLanguage::get_singleton()->debug_break(message, false);
#endif
}

bool check_async_result(const Variant &p_result, const GritFrame &p_frame, int p_line) {
	bool was_freed = false;
	const Object *object = p_result.get_validated_object_with_check(was_freed);
	if (object && object->is_class_ptr(GDScriptFunctionState::get_class_ptr_static())) {
		report_error(p_frame, p_line, R"(Trying to call an async function without "await".)");
		return false;
	}
	return true;
}

bool create_lambda(int p_index, const Variant **p_captures, int p_capture_count, bool p_use_self, Variant &r_target, const GritFrame &p_frame, int p_line) {
	GDScriptFunction *lambda = GritRuntimeAccess::get_lambda(p_frame.function, p_index);
	if (unlikely(!lambda)) {
		report_error(p_frame, p_line, "Grit: lambda function is not available.");
		return false;
	}
	Vector<Variant> captures;
	captures.resize(p_capture_count);
	for (int i = 0; i < p_capture_count; i++) {
		captures.write[i] = *p_captures[i];
	}
	if (!p_use_self) {
		GDScript *script = p_frame.instance ? GritRuntimeAccess::get_instance_script(p_frame.instance) : GritRuntimeAccess::get_function_script(p_frame.function);
		r_target = Callable(memnew(GDScriptLambdaCallable(Ref<GDScript>(script), lambda, captures)));
		return true;
	}
	if (unlikely(!p_frame.instance)) {
		report_error(p_frame, p_line, "Cannot create a lambda that uses self without an instance.");
		return false;
	}
	Object *owner = GritRuntimeAccess::get_owner(p_frame.instance);
	RefCounted *reference = Object::cast_to<RefCounted>(owner);
	if (reference) {
		r_target = Callable(memnew(GDScriptLambdaSelfCallable(Ref<RefCounted>(reference), lambda, captures)));
	} else {
		r_target = Callable(memnew(GDScriptLambdaSelfCallable(owner, lambda, captures)));
	}
	return true;
}

AwaitAction await_operand(const Variant &p_operand, Signal &r_signal, Variant &r_result, const GritFrame &p_frame, int p_line) {
	Variant result = p_operand;
	if (p_operand.get_type() == Variant::OBJECT) {
		bool was_freed = false;
		Object *object = p_operand.get_validated_object_with_check(was_freed);
		if (was_freed) {
			report_error(p_frame, p_line, "Trying to await on a freed object.");
			return AwaitAction::ERROR;
		}
		if (object && object->is_class_ptr(GDScriptFunctionState::get_class_ptr_static())) {
			result = Signal(object, SNAME("completed"));
		}
	}
	if (result.get_type() != Variant::SIGNAL) {
		r_result = result;
		return AwaitAction::CONTINUE;
	}
	r_signal = result;
	return AwaitAction::SUSPEND;
}

Ref<GDScriptFunctionState> await_state(const GritFrame &p_frame, GDScriptFunction::CallState *p_resumed, int p_resume, int p_slot_count, int p_line, Variant *&r_slots) {
	Ref<GDScriptFunctionState> state;
	state.instantiate();
	GritRuntimeAccess::set_state_function(state.ptr(), p_frame.function);

	GDScriptFunction::CallState &call_state = GritRuntimeAccess::get_call_state(state.ptr());
	const int stack_size = GDScriptFunction::FIXED_ADDRESSES_MAX + p_slot_count;
	call_state.stack.resize(sizeof(Variant) * stack_size);
	Variant *stack = reinterpret_cast<Variant *>(call_state.stack.ptrw());
	for (int i = GDScriptFunction::FIXED_ADDRESSES_MAX; i < stack_size; i++) {
		memnew_placement(&stack[i], Variant);
	}
	call_state.stack_size = stack_size;
	call_state.ip = p_resume;
	call_state.line = p_line;

	GDScript *script = GritRuntimeAccess::get_function_script(p_frame.function);
	call_state.script = script;
	GritRuntimeAccess::register_pending_state(state.ptr(), script, p_frame.instance);
#ifdef DEBUG_ENABLED
	call_state.function_name = p_frame.function->get_name();
	call_state.script_path = script->get_script_path();
#endif
	call_state.defarg = 0;
	if (p_resumed) {
		call_state.completed = p_resumed->completed;
	} else {
		call_state.completed = Signal(state.ptr(), SNAME("completed"));
	}

	r_slots = stack + GDScriptFunction::FIXED_ADDRESSES_MAX;
	return state;
}

Variant await_suspend(const Ref<GDScriptFunctionState> &p_state, Signal &r_signal, bool &r_awaited, const GritFrame &p_frame, int p_line) {
	const Variant state = p_state;
	const Error error = r_signal.connect(Callable(p_state.ptr(), "_signal_callback").bind(state), Object::CONNECT_ONE_SHOT);
	if (error != OK) {
		report_error(p_frame, p_line, "Error connecting to signal: " + r_signal.get_name() + " during await.");
		return error_return(p_frame);
	}
	r_awaited = true;
	return state;
}

void complete_resumed(GDScriptFunction::CallState *p_state, const Variant &p_result) {
	const Variant *arguments[1] = { &p_result };
	p_state->completed.emit(arguments, 1);
}

bool call_method_bind(MethodBind *p_method, const Variant &p_base, const Variant **p_args, int p_argcount, Variant *r_return, const GritFrame &p_frame, int p_line) {
	Object *object = nullptr;
	if (!resolve_method_base(p_method, p_base, object, p_frame, p_line)) {
		return false;
	}
	Callable::CallError call_error;
	Variant result = p_method->call(object, p_args, p_argcount, call_error);
	if (r_return) {
		*r_return = result;
	}
#ifdef DEBUG_ENABLED
	if (call_error.error != Callable::CallError::CALL_OK) {
		const String method = call_error_method_name(p_method->get_name(), p_args, p_argcount, call_error);
		if (method == "free" && call_error.error == Callable::CallError::CALL_ERROR_INVALID_METHOD) {
			if (p_base.is_ref_counted()) {
				report_error(p_frame, p_line, "Attempted to free a RefCounted object.");
				return false;
			} else if (p_base.get_type() == Variant::OBJECT) {
				report_error(p_frame, p_line, "Attempted to free a locked object (calling or emitting).");
				return false;
			}
		}
		report_error(p_frame, p_line, GritRuntimeAccess::get_call_error(p_frame.function, "function '" + method + "' in base '" + value_type_name(&p_base) + "'", p_args, p_argcount, result, call_error));
		return false;
	}
#endif
	return true;
}

bool call_method_bind_validated(MethodBind *p_method, const Variant &p_base, const Variant **p_args, Variant &r_return, const GritFrame &p_frame, int p_line) {
	Object *object = nullptr;
	if (!resolve_method_base(p_method, p_base, object, p_frame, p_line)) {
		return false;
	}
	if (p_method->has_return()) {
		prepare(r_return, p_method->get_return_info().type);
		p_method->validated_call(object, p_args, &r_return);
	} else {
		VariantInternal::initialize(&r_return, Variant::NIL);
		p_method->validated_call(object, p_args, nullptr);
	}
	return true;
}

bool call_method_bind_pointer(MethodBind *p_method, const Variant &p_base, const void **p_args, void *r_return, const GritFrame &p_frame, int p_line) {
	Object *object = nullptr;
	if (!resolve_method_base(p_method, p_base, object, p_frame, p_line)) {
		return false;
	}
	p_method->ptrcall(object, p_args, r_return);
	return true;
}

bool call_utility(const StringName &p_function, const Variant **p_args, int p_argcount, Variant &r_return, const GritFrame &p_frame, int p_line) {
	Callable::CallError call_error;
	Variant result;
	Variant::call_utility_function(p_function, &result, p_args, p_argcount, call_error);
#ifdef DEBUG_ENABLED
	if (call_error.error != Callable::CallError::CALL_OK) {
		r_return = result;
		if (result.get_type() == Variant::STRING && !result.operator String().is_empty()) {
			report_error(p_frame, p_line, vformat(R"*(Error calling utility function "%s()": %s)*", p_function, result));
		} else {
			report_error(p_frame, p_line, GritRuntimeAccess::get_call_error(p_frame.function, vformat(R"*(utility function "%s()")*", p_function), p_args, p_argcount, result, call_error));
		}
		return false;
	}
#endif
	r_return = std::move(result);
	return true;
}

bool call_gdscript_utility(GDScriptUtilityFunctions::FunctionPtr p_function, const StringName &p_name, const Variant **p_args, int p_argcount, Variant &r_return, const GritFrame &p_frame, int p_line) {
	Callable::CallError call_error;
	Variant result;
	p_function(&result, p_args, p_argcount, call_error);
#ifdef DEBUG_ENABLED
	if (call_error.error != Callable::CallError::CALL_OK) {
		r_return = result;
		if (result.get_type() == Variant::STRING && !result.operator String().is_empty()) {
			report_error(p_frame, p_line, vformat(R"*(Error calling GDScript utility function "%s()": %s)*", p_name, result));
		} else {
			report_error(p_frame, p_line, GritRuntimeAccess::get_call_error(p_frame.function, vformat(R"*(GDScript utility function "%s()")*", p_name), p_args, p_argcount, result, call_error));
		}
		return false;
	}
#endif
	r_return = std::move(result);
	return true;
}

bool call_builtin_static(Variant::Type p_type, const StringName &p_method, const Variant **p_args, int p_argcount, Variant &r_return, const GritFrame &p_frame, int p_line) {
	Callable::CallError call_error;
	Variant result;
	Variant::call_static(p_type, p_method, p_args, p_argcount, result, call_error);
#ifdef DEBUG_ENABLED
	if (call_error.error != Callable::CallError::CALL_OK) {
		r_return = result;
		report_error(p_frame, p_line, GritRuntimeAccess::get_call_error(p_frame.function, "static function '" + p_method.operator String() + "' in type '" + Variant::get_type_name(p_type) + "'", p_args, p_argcount, result, call_error));
		return false;
	}
#endif
	r_return = std::move(result);
	return true;
}

bool call_native_static(MethodBind *p_method, const Variant **p_args, int p_argcount, Variant &r_return, const GritFrame &p_frame, int p_line) {
	Callable::CallError call_error;
	r_return = p_method->call(nullptr, p_args, p_argcount, call_error);
	if (call_error.error != Callable::CallError::CALL_OK) {
		report_error(p_frame, p_line, GritRuntimeAccess::get_call_error(p_frame.function, "static function '" + p_method->get_name().operator String() + "' in type '" + p_method->get_instance_class().operator String() + "'", p_args, p_argcount, r_return, call_error));
		return false;
	}
	return true;
}

void call_native_static_validated(MethodBind *p_method, const Variant **p_args, Variant &r_return) {
	if (p_method->has_return()) {
		prepare(r_return, p_method->get_return_info().type);
		p_method->validated_call(nullptr, p_args, &r_return);
	} else {
		VariantInternal::initialize(&r_return, Variant::NIL);
		p_method->validated_call(nullptr, p_args, nullptr);
	}
}

bool call_super(const StringName &p_method, const Variant **p_args, int p_argcount, Variant &r_return, const GritFrame &p_frame, int p_line) {
	const GDScript *script = GritRuntimeAccess::get_function_script(p_frame.function);
	GDScriptFunction *const *function = nullptr;
	while (GritRuntimeAccess::get_base_script(script)) {
		script = GritRuntimeAccess::get_base_script(script);
		function = GritRuntimeAccess::get_member_functions(script).getptr(p_method);
		if (function) {
			break;
		}
	}

	Callable::CallError call_error;
	if (function) {
		r_return = (*function)->call(p_frame.instance, p_args, p_argcount, call_error);
	} else if (GritRuntimeAccess::get_native_class(script)) {
		if (p_method != GDScriptLanguage::get_singleton()->strings._init) {
			MethodBind *method = ClassDB::get_method(GritRuntimeAccess::get_native_class(script)->get_name(), p_method);
			if (!method) {
				call_error.error = Callable::CallError::CALL_ERROR_INVALID_METHOD;
			} else if (!p_frame.instance) {
				return member_access_error(p_frame, p_line);
			} else {
				r_return = method->call(GritRuntimeAccess::get_owner(p_frame.instance), p_args, p_argcount, call_error);
			}
		}
	} else if (p_method != GDScriptLanguage::get_singleton()->strings._init) {
		call_error.error = Callable::CallError::CALL_ERROR_INVALID_METHOD;
	}

	if (call_error.error != Callable::CallError::CALL_OK) {
		report_error(p_frame, p_line, GritRuntimeAccess::get_call_error(p_frame.function, "function '" + p_method.operator String() + "'", p_args, p_argcount, r_return, call_error));
		return false;
	}
	return true;
}

bool check_assert(bool p_condition, const Variant &p_message, bool p_has_message, const GritFrame &p_frame, int p_line) {
	if (p_condition) {
		return true;
	}
	String message;
	if (p_has_message && p_message.get_type() != Variant::NIL) {
		message = p_message;
	}
	report_error(p_frame, p_line, message.is_empty() ? String("Assertion failed.") : "Assertion failed: " + message);
	return false;
}

bool return_typed_builtin(const Variant &p_value, Variant::Type p_type, Variant &r_return, const GritFrame &p_frame, int p_line) {
	if (p_value.get_type() == p_type) {
		r_return = p_value;
		return true;
	}
	Callable::CallError call_error;
	if (Variant::can_convert_strict(p_value.get_type(), p_type)) {
		const Variant *source = &p_value;
		Variant::construct(p_type, r_return, &source, 1, call_error);
		return true;
	}
	report_error(p_frame, p_line, vformat(R"(Trying to return a value of type "%s" from a function whose return type is "%s".)", value_type_name(&p_value), Variant::get_type_name(p_type)));
	Variant::construct(p_type, r_return, nullptr, 0, call_error);
	return false;
}

bool return_typed_array(const Variant &p_value, Variant::Type p_builtin_type, const StringName &p_native_type, const Variant &p_script_type, Variant &r_return, const GritFrame &p_frame, int p_line) {
	if (p_value.get_type() != Variant::ARRAY) {
		report_error(p_frame, p_line, vformat(R"(Trying to return a value of type "%s" from a function whose return type is "Array[%s]".)", value_type_name(&p_value), Variant::get_type_name(p_builtin_type)));
		return false;
	}
	if (!is_typed_array(p_value, p_builtin_type, p_native_type, p_script_type)) {
		report_error(p_frame, p_line, vformat(R"(Trying to return a value of type "%s" from a function whose return type is "Array[%s]".)", value_type_name(&p_value), element_type_name(p_builtin_type, p_native_type, p_script_type)));
		return false;
	}
	r_return = p_value;
	return true;
}

bool return_typed_dictionary(const Variant &p_value, Variant::Type p_key_builtin_type, const StringName &p_key_native_type, const Variant &p_key_script_type, Variant::Type p_value_builtin_type, const StringName &p_value_native_type, const Variant &p_value_script_type, Variant &r_return, const GritFrame &p_frame, int p_line) {
	if (!is_typed_dictionary(p_value, p_key_builtin_type, p_key_native_type, p_key_script_type, p_value_builtin_type, p_value_native_type, p_value_script_type)) {
		report_error(p_frame, p_line, vformat(R"(Trying to return a value of type "%s" from a function whose return type is "Dictionary[%s, %s]".)", value_type_name(&p_value), element_type_name(p_key_builtin_type, p_key_native_type, p_key_script_type), element_type_name(p_value_builtin_type, p_value_native_type, p_value_script_type)));
		return false;
	}
	r_return = p_value;
	return true;
}

bool return_typed_native(const Variant &p_value, const StringName &p_native_type, Variant &r_return, const GritFrame &p_frame, int p_line) {
	if (p_value.get_type() != Variant::OBJECT && p_value.get_type() != Variant::NIL) {
		report_error(p_frame, p_line, vformat(R"(Trying to return a value of type "%s" from a function whose return type is "%s".)", value_type_name(&p_value), p_native_type));
		return false;
	}
	bool was_freed = false;
	const Object *object = p_value.get_validated_object_with_check(was_freed);
	if (was_freed) {
		report_error(p_frame, p_line, "Trying to return a previously freed instance.");
		return false;
	}
	if (object && !ClassDB::is_parent_class(object->get_class_name(), p_native_type)) {
		report_error(p_frame, p_line, vformat(R"(Trying to return a value of type "%s" from a function whose return type is "%s".)", value_type_name(&p_value), p_native_type));
		return false;
	}
	r_return = p_value;
	return true;
}

bool return_typed_script(const Variant &p_value, const Variant &p_script_type, Variant &r_return, const GritFrame &p_frame, int p_line) {
	const Script *base_type = Object::cast_to<Script>(p_script_type.operator Object *());
	if (!base_type) {
		return false;
	}
	const String type_name = script_display_name(Ref<Script>(base_type));
	if (p_value.get_type() != Variant::OBJECT && p_value.get_type() != Variant::NIL) {
		report_error(p_frame, p_line, vformat(R"(Trying to return a value of type "%s" from a function whose return type is "%s".)", value_type_name(&p_value), type_name));
		return false;
	}
	bool was_freed = false;
	const Object *object = p_value.get_validated_object_with_check(was_freed);
	if (was_freed) {
		report_error(p_frame, p_line, "Trying to return a previously freed instance.");
		return false;
	}
	if (object && !script_inherits(object, base_type)) {
		report_error(p_frame, p_line, vformat(R"(Trying to return a value of type "%s" from a function whose return type is "%s".)", value_type_name(&p_value), type_name));
		return false;
	}
	r_return = p_value;
	return true;
}

bool iterate_begin(const Variant &p_container, Variant &r_counter, Variant &r_iterator, bool &r_continue, const GritFrame &p_frame, int p_line) {
	r_counter = Variant();
	bool valid = false;
	r_continue = p_container.iter_init(r_counter, valid);
	if (!r_continue) {
#ifdef DEBUG_ENABLED
		if (!valid) {
			report_error(p_frame, p_line, "Unable to iterate on object of type '" + Variant::get_type_name(p_container.get_type()) + "'.");
			return false;
		}
#endif
		return true;
	}
	r_iterator = p_container.iter_get(r_counter, valid);
#ifdef DEBUG_ENABLED
	if (!valid) {
		report_error(p_frame, p_line, "Unable to obtain iterator object of type '" + Variant::get_type_name(p_container.get_type()) + "'.");
		return false;
	}
#endif
	return true;
}

bool iterate_next(const Variant &p_container, Variant &r_counter, Variant &r_iterator, bool &r_continue, const GritFrame &p_frame, int p_line) {
	bool valid = false;
	r_continue = p_container.iter_next(r_counter, valid);
	if (!r_continue) {
#ifdef DEBUG_ENABLED
		if (!valid) {
			report_error(p_frame, p_line, "Unable to iterate on object of type '" + Variant::get_type_name(p_container.get_type()) + "' (type changed since first iteration?).");
			return false;
		}
#endif
		return true;
	}
	r_iterator = p_container.iter_get(r_counter, valid);
#ifdef DEBUG_ENABLED
	if (!valid) {
		report_error(p_frame, p_line, "Unable to obtain iterator object of type '" + Variant::get_type_name(p_container.get_type()) + "' (but was obtained on first iteration?).");
		return false;
	}
#endif
	return true;
}

}
