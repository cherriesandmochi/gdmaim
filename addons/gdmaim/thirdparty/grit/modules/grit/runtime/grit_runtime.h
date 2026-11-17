#pragma once

#include "grit_runtime_access.h"

#include "modules/gdscript/gdscript_utility_functions.h"

#include "core/debugger/engine_debugger.h"
#include "core/object/method_bind.h"
#include "core/variant/variant.h"
#include "core/variant/variant_internal.h"

#include <atomic>

struct GritFrame {
	GDScriptFunction *function = nullptr;
	GDScriptInstance *instance = nullptr;
};

namespace GritRuntime {

_FORCE_INLINE_ int64_t add_int(int64_t p_left, int64_t p_right) {
	return static_cast<int64_t>(static_cast<uint64_t>(p_left) + static_cast<uint64_t>(p_right));
}

_FORCE_INLINE_ int64_t subtract_int(int64_t p_left, int64_t p_right) {
	return static_cast<int64_t>(static_cast<uint64_t>(p_left) - static_cast<uint64_t>(p_right));
}

_FORCE_INLINE_ int64_t multiply_int(int64_t p_left, int64_t p_right) {
	return static_cast<int64_t>(static_cast<uint64_t>(p_left) * static_cast<uint64_t>(p_right));
}

_FORCE_INLINE_ int64_t negate_int(int64_t p_value) {
	return static_cast<int64_t>(0 - static_cast<uint64_t>(p_value));
}

_FORCE_INLINE_ int64_t divide_int(int64_t p_left, int64_t p_right) {
	if (p_right == -1) {
		return negate_int(p_left);
	}
	return p_left / p_right;
}

_FORCE_INLINE_ int64_t modulo_int(int64_t p_left, int64_t p_right) {
	if (p_right == -1) {
		return 0;
	}
	return p_left % p_right;
}

_FORCE_INLINE_ int64_t shift_left_int(int64_t p_left, int64_t p_right) {
	return static_cast<int64_t>(static_cast<uint64_t>(p_left) << (static_cast<uint64_t>(p_right) & 63));
}

_FORCE_INLINE_ int64_t shift_right_int(int64_t p_left, int64_t p_right) {
	const uint64_t shift = static_cast<uint64_t>(p_right) & 63;
	if (p_left >= 0) {
		return static_cast<int64_t>(static_cast<uint64_t>(p_left) >> shift);
	}
	return static_cast<int64_t>(~(~static_cast<uint64_t>(p_left) >> shift));
}

_FORCE_INLINE_ int64_t float_to_int(double p_value) {
	if (!(p_value >= -9223372036854775808.0 && p_value < 9223372036854775808.0)) {
		return INT64_MIN;
	}
	return static_cast<int64_t>(p_value);
}

_FORCE_INLINE_ bool range_continues(int64_t p_counter, int64_t p_to, int64_t p_step) {
	if (p_step > 0) {
		return p_counter < p_to;
	}
	if (p_step < 0) {
		return p_counter > p_to;
	}
	return false;
}

_FORCE_INLINE_ int64_t unbox_int(const Variant &p_value) {
	if (likely(p_value.get_type() == Variant::INT)) {
		return *VariantInternal::get_int(&p_value);
	}
	return p_value.operator int64_t();
}

_FORCE_INLINE_ double unbox_float(const Variant &p_value) {
	if (likely(p_value.get_type() == Variant::FLOAT)) {
		return *VariantInternal::get_float(&p_value);
	}
	return p_value.operator double();
}

_FORCE_INLINE_ bool unbox_bool(const Variant &p_value) {
	if (likely(p_value.get_type() == Variant::BOOL)) {
		return *VariantInternal::get_bool(&p_value);
	}
	return p_value.booleanize();
}

template <typename T>
_FORCE_INLINE_ T unbox(const Variant &p_value, Variant::Type p_type) {
	if (likely(p_value.get_type() == p_type)) {
		return VariantInternalAccessor<T>::get(&p_value);
	}
	return p_value.operator T();
}

template <typename T>
_FORCE_INLINE_ T unbox_take(Variant &r_value, Variant::Type p_type) {
	if (likely(r_value.get_type() == p_type)) {
		return std::move(VariantInternalAccessor<T>::get(&r_value));
	}
	return r_value.operator T();
}

template <typename T>
_FORCE_INLINE_ const T *internal_pointer(const Variant &p_value) {
	return &VariantInternalAccessor<T>::get(&p_value);
}

template <typename T>
_FORCE_INLINE_ T *internal_pointer(Variant &r_value) {
	return &VariantInternalAccessor<T>::get(&r_value);
}

_FORCE_INLINE_ void prepare(Variant &r_value, Variant::Type p_type) {
	if (r_value.get_type() != p_type) {
		VariantInternal::initialize(&r_value, p_type);
	}
}

template <typename T>
_FORCE_INLINE_ bool truth(const T &p_value) {
	return !(p_value == T());
}

_FORCE_INLINE_ bool truth(const NodePath &p_value) {
	return !p_value.is_empty();
}

_FORCE_INLINE_ bool truth(const Callable &p_value) {
	return !p_value.is_null();
}

_FORCE_INLINE_ bool truth(const Signal &p_value) {
	return !p_value.is_null();
}

class CallScope {
	GritRuntimeAccess::CallLevel level;
	int ip = 0;
	bool is_tracked = false;

	void enter(GDScriptInstance *p_instance);
	void exit();

public:
	GDScriptFunction *function = nullptr;
	int line = 0;
#ifdef DEBUG_ENABLED
	bool is_debugging = false;
#endif

	CallScope(GDScriptFunction *p_function, GDScriptInstance *p_instance, int p_line) :
			function(p_function), line(p_line) {
#ifdef DEBUG_ENABLED
		is_debugging = EngineDebugger::is_active();
#endif
		is_tracked = GDScriptLanguage::get_singleton()->should_track_call_stack();
		if (unlikely(is_tracked)) {
			enter(p_instance);
		}
	}

	~CallScope() {
		if (unlikely(is_tracked)) {
			exit();
		}
	}

	CallScope(const CallScope &) = delete;
	CallScope &operator=(const CallScope &) = delete;
};

#ifdef DEBUG_ENABLED
void debug_line(CallScope &r_scope);
#endif

_FORCE_INLINE_ void line_reached(CallScope &r_scope, int p_line) {
	r_scope.line = p_line;
#ifdef DEBUG_ENABLED
	if (unlikely(r_scope.is_debugging)) {
		debug_line(r_scope);
	}
#endif
}

_FORCE_INLINE_ Variant self_of(GDScriptInstance *p_instance) {
	return p_instance ? Variant(GritRuntimeAccess::get_owner(p_instance)) : Variant();
}

_FORCE_INLINE_ Variant *members_of(GDScriptInstance *p_instance) {
	return p_instance ? GritRuntimeAccess::get_members(p_instance) : nullptr;
}

Variant class_of(const GritFrame &p_frame);

void report_error(const GritFrame &p_frame, int p_line, const String &p_message);
Variant default_return(const GritFrame &p_frame);
Variant error_return(const GritFrame &p_frame);
bool member_access_error(const GritFrame &p_frame, int p_line);

bool check_argument_count(const GritFrame &p_frame, int p_argcount, int p_required, int p_total, Callable::CallError &r_error);
Array rest_arguments(const Variant **p_args, int p_argcount, int p_parameter_count);
bool read_converted_argument(const Variant **p_args, int p_index, int64_t &r_value, Callable::CallError &r_error);
bool read_converted_argument(const Variant **p_args, int p_index, double &r_value, Callable::CallError &r_error);
bool read_converted_argument(const Variant **p_args, int p_index, bool &r_value, Callable::CallError &r_error);
bool read_typed_argument(const GritFrame &p_frame, const Variant **p_args, int p_index, Variant &r_value, Callable::CallError &r_error);

_FORCE_INLINE_ bool read_argument(const GritFrame &p_frame, const Variant **p_args, int p_index, int64_t &r_value, Callable::CallError &r_error) {
	if (likely(p_args[p_index]->get_type() == Variant::INT)) {
		r_value = *VariantInternal::get_int(p_args[p_index]);
		return true;
	}
	return read_converted_argument(p_args, p_index, r_value, r_error);
}

_FORCE_INLINE_ bool read_argument(const GritFrame &p_frame, const Variant **p_args, int p_index, double &r_value, Callable::CallError &r_error) {
	if (likely(p_args[p_index]->get_type() == Variant::FLOAT)) {
		r_value = *VariantInternal::get_float(p_args[p_index]);
		return true;
	}
	return read_converted_argument(p_args, p_index, r_value, r_error);
}

_FORCE_INLINE_ bool read_argument(const GritFrame &p_frame, const Variant **p_args, int p_index, bool &r_value, Callable::CallError &r_error) {
	if (likely(p_args[p_index]->get_type() == Variant::BOOL)) {
		r_value = *VariantInternal::get_bool(p_args[p_index]);
		return true;
	}
	return read_converted_argument(p_args, p_index, r_value, r_error);
}

_FORCE_INLINE_ bool read_argument(const GritFrame &p_frame, const Variant **p_args, int p_index, Variant &r_value, Callable::CallError &r_error) {
	return read_typed_argument(p_frame, p_args, p_index, r_value, r_error);
}

template <typename T>
_FORCE_INLINE_ bool read_native_argument(const GritFrame &p_frame, const Variant **p_args, int p_index, Variant::Type p_type, bool p_exact_check, T &r_value, Callable::CallError &r_error) {
	if (likely(p_exact_check && p_args[p_index]->get_type() == p_type)) {
		r_value = VariantInternalAccessor<T>::get(p_args[p_index]);
		return true;
	}
	Variant converted;
	if (unlikely(!read_typed_argument(p_frame, p_args, p_index, converted, r_error))) {
		return false;
	}
	r_value = unbox<T>(converted, p_type);
	return true;
}

_FORCE_INLINE_ Variant no_script() {
	return Variant(static_cast<Object *>(nullptr));
}

Variant load_resource(const String &p_path);
Variant load_script(const GDScript *p_context, const String &p_reference);
Variant native_class(const StringName &p_name);
Variant engine_singleton(const StringName &p_name);
Variant packed_array_constant(Variant::Type p_type, std::initializer_list<Variant> p_elements);
Variant utility_callable(const StringName &p_name);
Variant array_constant(std::initializer_list<Variant> p_elements, Variant::Type p_builtin_type, const StringName &p_native_type, const Variant &p_script_type, bool p_read_only);
Variant dictionary_constant(std::initializer_list<Variant> p_pairs, Variant::Type p_key_builtin_type, const StringName &p_key_native_type, const Variant &p_key_script_type, Variant::Type p_value_builtin_type, const StringName &p_value_native_type, const Variant &p_value_script_type, bool p_read_only);
Variant typed_array(Variant::Type p_builtin_type, const StringName &p_native_type, const Variant &p_script_type);
Variant typed_dictionary(Variant::Type p_key_builtin_type, const StringName &p_key_native_type, const Variant &p_key_script_type, Variant::Type p_value_builtin_type, const StringName &p_value_native_type, const Variant &p_value_script_type);

bool assign_typed_builtin(Variant &r_target, const Variant &p_source, Variant::Type p_type, const GritFrame &p_frame, int p_line);
bool assign_typed_array(Variant &r_target, const Variant &p_source, Variant::Type p_builtin_type, const StringName &p_native_type, const Variant &p_script_type, const GritFrame &p_frame, int p_line);
bool assign_typed_dictionary(Variant &r_target, const Variant &p_source, Variant::Type p_key_builtin_type, const StringName &p_key_native_type, const Variant &p_key_script_type, Variant::Type p_value_builtin_type, const StringName &p_value_native_type, const Variant &p_value_script_type, const GritFrame &p_frame, int p_line);
bool assign_typed_native(Variant &r_target, const Variant &p_source, const StringName &p_native_type, const GritFrame &p_frame, int p_line);
bool assign_typed_script(Variant &r_target, const Variant &p_source, const Variant &p_script_type, const GritFrame &p_frame, int p_line);

bool evaluate(Variant::Operator p_operator, const Variant &p_left, const Variant &p_right, Variant &r_target, const GritFrame &p_frame, int p_line);

_FORCE_INLINE_ void store_int(Variant &r_target, int64_t p_value) {
	prepare(r_target, Variant::INT);
	*VariantInternal::get_int(&r_target) = p_value;
}

_FORCE_INLINE_ void store_float(Variant &r_target, double p_value) {
	prepare(r_target, Variant::FLOAT);
	*VariantInternal::get_float(&r_target) = p_value;
}

_FORCE_INLINE_ void store_bool(Variant &r_target, bool p_value) {
	prepare(r_target, Variant::BOOL);
	*VariantInternal::get_bool(&r_target) = p_value;
}

template <Variant::Operator OP, typename T>
_FORCE_INLINE_ bool compare_numbers(T p_left, T p_right, Variant &r_target) {
	switch (OP) {
		case Variant::OP_EQUAL:
			store_bool(r_target, p_left == p_right);
			return true;
		case Variant::OP_NOT_EQUAL:
			store_bool(r_target, p_left != p_right);
			return true;
		case Variant::OP_LESS:
			store_bool(r_target, p_left < p_right);
			return true;
		case Variant::OP_LESS_EQUAL:
			store_bool(r_target, p_left <= p_right);
			return true;
		case Variant::OP_GREATER:
			store_bool(r_target, p_left > p_right);
			return true;
		case Variant::OP_GREATER_EQUAL:
			store_bool(r_target, p_left >= p_right);
			return true;
		default:
			return false;
	}
}

template <Variant::Operator OP>
_FORCE_INLINE_ bool evaluate_integers(int64_t p_left, int64_t p_right, Variant &r_target) {
	switch (OP) {
		case Variant::OP_ADD:
			store_int(r_target, add_int(p_left, p_right));
			return true;
		case Variant::OP_SUBTRACT:
			store_int(r_target, subtract_int(p_left, p_right));
			return true;
		case Variant::OP_MULTIPLY:
			store_int(r_target, multiply_int(p_left, p_right));
			return true;
		case Variant::OP_DIVIDE:
			if (p_right == 0) {
				return false;
			}
			store_int(r_target, divide_int(p_left, p_right));
			return true;
		case Variant::OP_MODULE:
			if (p_right == 0) {
				return false;
			}
			store_int(r_target, modulo_int(p_left, p_right));
			return true;
		default:
			return compare_numbers<OP>(p_left, p_right, r_target);
	}
}

template <Variant::Operator OP>
_FORCE_INLINE_ bool evaluate_floats(double p_left, double p_right, Variant &r_target) {
	switch (OP) {
		case Variant::OP_ADD:
			store_float(r_target, p_left + p_right);
			return true;
		case Variant::OP_SUBTRACT:
			store_float(r_target, p_left - p_right);
			return true;
		case Variant::OP_MULTIPLY:
			store_float(r_target, p_left * p_right);
			return true;
		case Variant::OP_DIVIDE:
			store_float(r_target, p_left / p_right);
			return true;
		default:
			return compare_numbers<OP>(p_left, p_right, r_target);
	}
}

template <Variant::Operator OP>
_FORCE_INLINE_ bool evaluate_dynamic(const Variant &p_left, const Variant &p_right, Variant &r_target, const GritFrame &p_frame, int p_line) {
	const Variant::Type left_type = p_left.get_type();
	const Variant::Type right_type = p_right.get_type();
	if (likely(left_type == Variant::INT && right_type == Variant::INT)) {
		if (evaluate_integers<OP>(*VariantInternal::get_int(&p_left), *VariantInternal::get_int(&p_right), r_target)) {
			return true;
		}
	} else if ((left_type == Variant::INT || left_type == Variant::FLOAT) && (right_type == Variant::INT || right_type == Variant::FLOAT)) {
		const double left = left_type == Variant::INT ? static_cast<double>(*VariantInternal::get_int(&p_left)) : *VariantInternal::get_float(&p_left);
		const double right = right_type == Variant::INT ? static_cast<double>(*VariantInternal::get_int(&p_right)) : *VariantInternal::get_float(&p_right);
		if (evaluate_floats<OP>(left, right, r_target)) {
			return true;
		}
	}
	return evaluate(OP, p_left, p_right, r_target, p_frame, p_line);
}

bool is_typed_array(const Variant &p_value, Variant::Type p_builtin_type, const StringName &p_native_type, const Variant &p_script_type);
bool is_typed_dictionary(const Variant &p_value, Variant::Type p_key_builtin_type, const StringName &p_key_native_type, const Variant &p_key_script_type, Variant::Type p_value_builtin_type, const StringName &p_value_native_type, const Variant &p_value_script_type);
bool type_test_native(const Variant &p_value, const StringName &p_native_type, bool &r_result, const GritFrame &p_frame, int p_line);
bool type_test_script(const Variant &p_value, const Variant &p_script_type, bool &r_result, const GritFrame &p_frame, int p_line);
bool cast_to_builtin(const Variant &p_value, Variant::Type p_type, Variant &r_target, const GritFrame &p_frame, int p_line);
bool cast_to_native(const Variant &p_value, const StringName &p_native_type, Variant &r_target, const GritFrame &p_frame, int p_line);
bool cast_to_script(const Variant &p_value, const Variant &p_script_type, Variant &r_target, const GritFrame &p_frame, int p_line);

bool get_keyed(const Variant &p_base, const Variant &p_key, Variant &r_target, const GritFrame &p_frame, int p_line);
bool set_keyed(Variant &r_base, const Variant &p_key, const Variant &p_value, const GritFrame &p_frame, int p_line);
bool missing_key(const Variant &p_base, const Variant &p_key, const GritFrame &p_frame, int p_line);

_FORCE_INLINE_ const Variant *dictionary_entry(const Dictionary &p_dictionary, const Variant &p_key) {
	return p_dictionary.getptr(p_key);
}
bool keyed_set_failed(const Variant &p_base, const Variant &p_key, const Variant &p_value, const GritFrame &p_frame, int p_line);
bool get_keyed_validated(Variant::ValidatedKeyedGetter p_getter, const Variant &p_base, const Variant &p_key, Variant &r_target, const GritFrame &p_frame, int p_line);
bool set_keyed_validated(Variant::ValidatedKeyedSetter p_setter, Variant &r_base, const Variant &p_key, const Variant &p_value, const GritFrame &p_frame, int p_line);
bool get_indexed_validated(Variant::ValidatedIndexedGetter p_getter, const Variant &p_base, const Variant &p_index, Variant &r_target, const GritFrame &p_frame, int p_line);
bool set_indexed_validated(Variant::ValidatedIndexedSetter p_setter, Variant &r_base, const Variant &p_index, const Variant &p_value, const GritFrame &p_frame, int p_line);
bool out_of_bounds_get(const Variant &p_base, int64_t p_index, const GritFrame &p_frame, int p_line);
bool out_of_bounds_set(const Variant &p_base, int64_t p_index, const GritFrame &p_frame, int p_line);

_FORCE_INLINE_ bool normalize_index(int64_t &r_index, int64_t p_size) {
	if (r_index < 0) {
		r_index += p_size;
	}
	return r_index >= 0 && r_index < p_size;
}

template <typename T>
_FORCE_INLINE_ const T *packed_element(const Vector<T> &p_array, int64_t p_index) {
	if (unlikely(!normalize_index(p_index, p_array.size()))) {
		return nullptr;
	}
	return p_array.ptr() + p_index;
}

template <typename T>
_FORCE_INLINE_ T *packed_slot(Vector<T> &r_array, int64_t p_index) {
	if (unlikely(!normalize_index(p_index, r_array.size()))) {
		return nullptr;
	}
	return r_array.ptrw() + p_index;
}

_FORCE_INLINE_ const Variant *array_element(const Array &p_array, int64_t p_index) {
	if (unlikely(!normalize_index(p_index, p_array.size()))) {
		return nullptr;
	}
	return &p_array[p_index];
}

_FORCE_INLINE_ bool array_set(Array &r_array, int64_t p_index, const Variant &p_value) {
	if (unlikely(r_array.is_read_only() || !normalize_index(p_index, r_array.size()))) {
		return false;
	}
	r_array.set(p_index, p_value);
	return true;
}
_FORCE_INLINE_ int64_t iteration_count(const Variant &p_container) {
	return p_container.get_type() == Variant::INT ? *VariantInternal::get_int(&p_container) : 0;
}

template <typename T>
_FORCE_INLINE_ int64_t iteration_size(const Variant &p_container, Variant::Type p_type) {
	return p_container.get_type() == p_type ? VariantInternalAccessor<T>::get(&p_container).size() : 0;
}

template <typename T>
_FORCE_INLINE_ decltype(auto) iteration_element(const Variant &p_container, int64_t p_index) {
	return VariantInternalAccessor<T>::get(&p_container).get(p_index);
}

bool get_named(const Variant &p_base, const StringName &p_name, Variant &r_target, const GritFrame &p_frame, int p_line);
bool set_named(Variant &r_base, const StringName &p_name, const Variant &p_value, const GritFrame &p_frame, int p_line);
bool get_property(const StringName &p_name, Variant &r_target, const GritFrame &p_frame, int p_line);
bool set_property(const StringName &p_name, const Variant &p_value, const GritFrame &p_frame, int p_line);

struct PropertyAccessor {
	MethodBind *method = nullptr;
	StringName method_name;
	int index = -1;
	Variant::Type value_type = Variant::VARIANT_MAX;
	bool is_call = false;
	bool returns_ref = false;
	StringName object_class;
};

struct PropertyAccessors {
	const GDType *type = nullptr;
	PropertyAccessor getter;
	PropertyAccessor setter;
};

struct PropertyCache {
	static constexpr int SIZE = 4;
	std::atomic<const PropertyAccessors *> slots[SIZE];
};

void resolve_property_accessors(const StringName &p_class, const StringName &p_name, PropertyAccessors &r_accessors);
const PropertyAccessors *insert_property_accessors(PropertyCache &r_cache, const GDType *p_type, const StringName &p_name);
void clear_property_accessors();
bool script_intercepts_get(ScriptInstance *p_instance, const StringName &p_name);
bool script_intercepts_set(ScriptInstance *p_instance, const StringName &p_name);
bool script_intercepts_call(ScriptInstance *p_instance, const StringName &p_method);
void call_property_accessor(const PropertyAccessor &p_accessor, Object *p_object, const void *p_value, void *r_value);

_FORCE_INLINE_ Object *object_of(const Variant &p_value) {
	return p_value.get_type() == Variant::OBJECT ? p_value.get_validated_object() : nullptr;
}

_FORCE_INLINE_ const PropertyAccessors *property_accessors(Object *p_object, PropertyCache &r_cache, const StringName &p_name) {
	const GDType *type = GritRuntimeAccess::get_type(p_object);
	if (unlikely(!type)) {
		return nullptr;
	}
	for (std::atomic<const PropertyAccessors *> &slot : r_cache.slots) {
		const PropertyAccessors *accessors = slot.load(std::memory_order_acquire);
		if (!accessors) {
			break;
		}
		if (likely(accessors->type == type)) {
			return accessors;
		}
	}
	return insert_property_accessors(r_cache, type, p_name);
}

_FORCE_INLINE_ const PropertyAccessor *usable_accessor(const PropertyAccessor &p_accessor, Object *p_object, Variant::Type p_type) {
	if (p_accessor.value_type != p_type) {
		return nullptr;
	}
	if (p_accessor.is_call) {
		ScriptInstance *instance = p_object->get_script_instance();
		if (instance && script_intercepts_call(instance, p_accessor.method_name)) {
			return nullptr;
		}
	}
	return &p_accessor;
}

_FORCE_INLINE_ const PropertyAccessor *member_getter(Object *p_object, PropertyCache &r_cache, const StringName &p_name, Variant::Type p_type) {
	const PropertyAccessors *accessors = property_accessors(p_object, r_cache, p_name);
	return accessors ? usable_accessor(accessors->getter, p_object, p_type) : nullptr;
}

_FORCE_INLINE_ const PropertyAccessor *member_setter(Object *p_object, PropertyCache &r_cache, const StringName &p_name, Variant::Type p_type) {
	const PropertyAccessors *accessors = property_accessors(p_object, r_cache, p_name);
	return accessors ? usable_accessor(accessors->setter, p_object, p_type) : nullptr;
}

_FORCE_INLINE_ const PropertyAccessor *named_getter(Object *p_object, PropertyCache &r_cache, const StringName &p_name, Variant::Type p_type) {
	if (unlikely(!p_object) || GritRuntimeAccess::has_extension(p_object)) {
		return nullptr;
	}
	ScriptInstance *instance = p_object->get_script_instance();
	if (instance && script_intercepts_get(instance, p_name)) {
		return nullptr;
	}
	return member_getter(p_object, r_cache, p_name, p_type);
}

_FORCE_INLINE_ const PropertyAccessor *named_setter(Object *p_object, PropertyCache &r_cache, const StringName &p_name, Variant::Type p_type) {
	if (unlikely(!p_object) || GritRuntimeAccess::has_extension(p_object)) {
		return nullptr;
	}
	ScriptInstance *instance = p_object->get_script_instance();
	if (instance && script_intercepts_set(instance, p_name)) {
		return nullptr;
	}
	return member_setter(p_object, r_cache, p_name, p_type);
}

Variant read_object_property(const PropertyAccessor &p_getter, Object *p_object);

_FORCE_INLINE_ void read_property(const PropertyAccessor &p_getter, Object *p_object, void *r_value) {
	if (likely(p_getter.index < 0 && !p_getter.is_call)) {
		p_getter.method->ptrcall(p_object, nullptr, r_value);
	} else {
		call_property_accessor(p_getter, p_object, nullptr, r_value);
	}
}

_FORCE_INLINE_ void write_property(const PropertyAccessor &p_setter, Object *p_object, const void *p_value) {
	if (likely(p_setter.index < 0 && !p_setter.is_call)) {
		const void *arguments[1] = { p_value };
		p_setter.method->ptrcall(p_object, arguments, nullptr);
	} else {
		call_property_accessor(p_setter, p_object, p_value, nullptr);
	}
}

_FORCE_INLINE_ void mark_edited([[maybe_unused]] Object *p_object) {
#ifdef TOOLS_ENABLED
	GritRuntimeAccess::mark_edited(p_object);
#endif
}

Variant *static_variable(const Variant &p_class, int p_index);
bool get_global(const StringName &p_name, Variant &r_target, const GritFrame &p_frame, int p_line);

bool construct(Variant::Type p_type, const Variant **p_args, int p_argcount, Variant &r_target, const GritFrame &p_frame, int p_line);
Variant array_literal(const Variant **p_args, int p_argcount);
Variant typed_array_literal(const Variant **p_args, int p_argcount, Variant::Type p_builtin_type, const StringName &p_native_type, const Variant &p_script_type);
Variant dictionary_literal(const Variant **p_args, int p_pair_count);
Variant typed_dictionary_literal(const Variant **p_args, int p_pair_count, Variant::Type p_key_builtin_type, const StringName &p_key_native_type, const Variant &p_key_script_type, Variant::Type p_value_builtin_type, const StringName &p_value_native_type, const Variant &p_value_script_type);

bool call(Variant &r_base, const StringName &p_method, const Variant **p_args, int p_argcount, Variant *r_return, bool p_is_async, const GritFrame &p_frame, int p_line);
_FORCE_INLINE_ GDScriptFunction *find_instance_function(GDScriptInstance *p_instance, const StringName &p_name) {
	GDScript *script = GritRuntimeAccess::get_instance_script(p_instance);
	while (script) {
		if (likely(GritRuntimeAccess::is_script_valid(script))) {
			GDScriptFunction *const *function = GritRuntimeAccess::get_member_functions(script).getptr(p_name);
			if (function) {
				return *function;
			}
		}
		script = GritRuntimeAccess::get_base_script(script);
	}
	return nullptr;
}

_FORCE_INLINE_ GDScriptFunction *find_static_function(const Variant &p_class, const StringName &p_name) {
	GDScript *script = Object::cast_to<GDScript>(p_class.get_validated_object());
	while (script) {
		if (likely(GritRuntimeAccess::is_script_valid(script))) {
			GDScriptFunction *const *function = GritRuntimeAccess::get_member_functions(script).getptr(p_name);
			if (function) {
				return (*function)->is_static() ? *function : nullptr;
			}
		}
		script = GritRuntimeAccess::get_base_script(script);
	}
	return nullptr;
}

_FORCE_INLINE_ bool is_freed_object(const Variant &p_value) {
	if (p_value.get_type() != Variant::OBJECT) {
		return false;
	}
	bool was_freed = false;
	p_value.get_validated_object_with_check(was_freed);
	return was_freed;
}

GDScriptInstance *gdscript_instance_of(const Variant &p_value);
const Variant *readable_script_member(const Variant &p_base, const StringName &p_name);
Variant *writable_script_member(const Variant &p_base, const StringName &p_name, const Variant &p_value);
void stack_overflow(GDScriptFunction *p_function, GDScriptInstance *p_instance);
bool check_async_result(const Variant &p_result, const GritFrame &p_frame, int p_line);

_FORCE_INLINE_ bool check_call_result(const Variant &p_result, const GritFrame &p_frame, int p_line) {
#ifdef DEBUG_ENABLED
	if (unlikely(p_result.get_type() == Variant::OBJECT)) {
		return check_async_result(p_result, p_frame, p_line);
	}
#endif
	return true;
}

class DirectCall {
#ifdef DEBUG_ENABLED
	ObjectID locked_object;
#endif
	bool is_entered = false;

public:
	_FORCE_INLINE_ bool entered() const { return is_entered; }

	DirectCall(GDScriptFunction *p_function, GDScriptInstance *p_instance, bool p_lock_owner) {
#ifdef DEBUG_ENABLED
		if (p_lock_owner) {
			Object *owner = GritRuntimeAccess::get_owner(p_instance);
			locked_object = owner->get_instance_id();
			GritRuntimeAccess::lock_object(owner);
		}
#endif
		int &depth = GritRuntimeAccess::get_call_depth();
		if (unlikely(++depth > GDScriptFunction::MAX_CALL_DEPTH)) {
			depth--;
			stack_overflow(p_function, p_instance);
			return;
		}
		is_entered = true;
	}

	~DirectCall() {
		if (is_entered) {
			GritRuntimeAccess::get_call_depth()--;
		}
#ifdef DEBUG_ENABLED
		if (locked_object.is_valid()) {
			Object *owner = ObjectDB::get_instance(locked_object);
			if (likely(owner)) {
				GritRuntimeAccess::unlock_object(owner);
			}
		}
#endif
	}

	DirectCall(const DirectCall &) = delete;
	DirectCall &operator=(const DirectCall &) = delete;
};

bool create_lambda(int p_index, const Variant **p_captures, int p_capture_count, bool p_use_self, Variant &r_target, const GritFrame &p_frame, int p_line);

enum class AwaitAction {
	CONTINUE,
	SUSPEND,
	ERROR,
};

AwaitAction await_operand(const Variant &p_operand, Signal &r_signal, Variant &r_result, const GritFrame &p_frame, int p_line);
Ref<GDScriptFunctionState> await_state(const GritFrame &p_frame, GDScriptFunction::CallState *p_resumed, int p_resume, int p_slot_count, int p_line, Variant *&r_slots);
Variant await_suspend(const Ref<GDScriptFunctionState> &p_state, Signal &r_signal, bool &r_awaited, const GritFrame &p_frame, int p_line);
void complete_resumed(GDScriptFunction::CallState *p_state, const Variant &p_result);

class ResumedSlots {
	Variant *slots = nullptr;
	int count = 0;

public:
	_FORCE_INLINE_ Variant &operator[](int p_index) { return slots[p_index]; }

	explicit ResumedSlots(GDScriptFunction::CallState *p_state) {
		slots = reinterpret_cast<Variant *>(const_cast<uint8_t *>(p_state->stack.ptr())) + GDScriptFunction::FIXED_ADDRESSES_MAX;
		count = MAX(p_state->stack_size - GDScriptFunction::FIXED_ADDRESSES_MAX, 0);
		p_state->stack_size = 0;
	}

	~ResumedSlots() {
		for (int i = 0; i < count; i++) {
			slots[i].~Variant();
		}
	}

	ResumedSlots(const ResumedSlots &) = delete;
	ResumedSlots &operator=(const ResumedSlots &) = delete;
};
bool call_method_bind(MethodBind *p_method, const Variant &p_base, const Variant **p_args, int p_argcount, Variant *r_return, const GritFrame &p_frame, int p_line);
bool call_method_bind_validated(MethodBind *p_method, const Variant &p_base, const Variant **p_args, Variant &r_return, const GritFrame &p_frame, int p_line);
bool call_method_bind_pointer(MethodBind *p_method, const Variant &p_base, const void **p_args, void *r_return, const GritFrame &p_frame, int p_line);
bool call_utility(const StringName &p_function, const Variant **p_args, int p_argcount, Variant &r_return, const GritFrame &p_frame, int p_line);
bool call_gdscript_utility(GDScriptUtilityFunctions::FunctionPtr p_function, const StringName &p_name, const Variant **p_args, int p_argcount, Variant &r_return, const GritFrame &p_frame, int p_line);
bool call_builtin_static(Variant::Type p_type, const StringName &p_method, const Variant **p_args, int p_argcount, Variant &r_return, const GritFrame &p_frame, int p_line);
bool call_native_static(MethodBind *p_method, const Variant **p_args, int p_argcount, Variant &r_return, const GritFrame &p_frame, int p_line);
void call_native_static_validated(MethodBind *p_method, const Variant **p_args, Variant &r_return);
bool call_super(const StringName &p_method, const Variant **p_args, int p_argcount, Variant &r_return, const GritFrame &p_frame, int p_line);

bool check_assert(bool p_condition, const Variant &p_message, bool p_has_message, const GritFrame &p_frame, int p_line);
bool return_typed_builtin(const Variant &p_value, Variant::Type p_type, Variant &r_return, const GritFrame &p_frame, int p_line);
bool return_typed_array(const Variant &p_value, Variant::Type p_builtin_type, const StringName &p_native_type, const Variant &p_script_type, Variant &r_return, const GritFrame &p_frame, int p_line);
bool return_typed_dictionary(const Variant &p_value, Variant::Type p_key_builtin_type, const StringName &p_key_native_type, const Variant &p_key_script_type, Variant::Type p_value_builtin_type, const StringName &p_value_native_type, const Variant &p_value_script_type, Variant &r_return, const GritFrame &p_frame, int p_line);
bool return_typed_native(const Variant &p_value, const StringName &p_native_type, Variant &r_return, const GritFrame &p_frame, int p_line);
bool return_typed_script(const Variant &p_value, const Variant &p_script_type, Variant &r_return, const GritFrame &p_frame, int p_line);

bool iterate_begin(const Variant &p_container, Variant &r_counter, Variant &r_iterator, bool &r_continue, const GritFrame &p_frame, int p_line);
bool iterate_next(const Variant &p_container, Variant &r_counter, Variant &r_iterator, bool &r_continue, const GritFrame &p_frame, int p_line);

}
