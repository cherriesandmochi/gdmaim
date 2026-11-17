#pragma once

#include "modules/gdscript/gdscript.h"
#include "modules/gdscript/gdscript_function.h"

class GritRuntimeAccess {
public:
	using CallLevel = GDScriptLanguage::CallLevel;
	using MemberInfo = GDScript::MemberInfo;

	static _FORCE_INLINE_ Object *get_owner(const GDScriptInstance *p_instance) { return p_instance->owner; }
	static _FORCE_INLINE_ GDScript *get_instance_script(const GDScriptInstance *p_instance) { return p_instance->script.ptr(); }
	static _FORCE_INLINE_ bool is_instance_valid(const GDScriptInstance *p_instance) {
		return p_instance && ObjectDB::get_instance(p_instance->owner_id) != nullptr && p_instance->script.is_valid();
	}
	static _FORCE_INLINE_ Variant *get_members(GDScriptInstance *p_instance) { return p_instance->members.ptrw(); }
	static _FORCE_INLINE_ int get_member_count(const GDScriptInstance *p_instance) { return p_instance->members.size(); }

	static _FORCE_INLINE_ GDScript *get_function_script(const GDScriptFunction *p_function) { return p_function->_script; }
	static _FORCE_INLINE_ const Variant *get_native_constants(const GDScriptFunction *p_function) { return p_function->native_constants.ptr(); }
	static _FORCE_INLINE_ void set_native_constants(GDScriptFunction *p_function, const Vector<Variant> &p_constants) { p_function->native_constants = p_constants; }
	static _FORCE_INLINE_ const GDScriptDataType &get_argument_type(const GDScriptFunction *p_function, int p_index) { return p_function->argument_types[p_index]; }
	static _FORCE_INLINE_ Variant get_default_return(GDScriptFunction *p_function) { return p_function->_get_default_variant_for_data_type(p_function->return_type); }
	static _FORCE_INLINE_ String get_call_error(const GDScriptFunction *p_function, const String &p_where, const Variant **p_arguments, int p_argument_count, const Variant &p_return, const Callable::CallError &p_error) {
		return p_function->_get_call_error(p_where, p_arguments, p_argument_count, p_return, p_error);
	}
	static _FORCE_INLINE_ String get_callable_call_error(const GDScriptFunction *p_function, const String &p_where, const Callable &p_callable, const Variant **p_arguments, int p_argument_count, const Variant &p_return, const Callable::CallError &p_error) {
		return p_function->_get_callable_call_error(p_where, p_callable, p_arguments, p_argument_count, p_return, p_error);
	}

	static _FORCE_INLINE_ GDScriptFunction *get_lambda(const GDScriptFunction *p_function, int p_index) {
		return p_index >= 0 && p_index < p_function->lambdas.size() ? p_function->lambdas[p_index] : nullptr;
	}

	static _FORCE_INLINE_ GDScriptFunction::CallState &get_call_state(GDScriptFunctionState *p_state) { return p_state->state; }
	static _FORCE_INLINE_ void set_state_function(GDScriptFunctionState *p_state, GDScriptFunction *p_function) { p_state->function = p_function; }
	static void register_pending_state(GDScriptFunctionState *p_state, GDScript *p_script, GDScriptInstance *p_instance) {
		MutexLock lock(GDScriptLanguage::get_singleton()->mutex);
		p_script->pending_func_states.add(&p_state->scripts_list);
		if (p_instance) {
			p_state->state.instance = p_instance;
			p_instance->pending_func_states.add(&p_state->instances_list);
		} else {
			p_state->state.instance = nullptr;
		}
	}

	static _FORCE_INLINE_ int &get_call_depth() { return GDScriptFunction::call_depth; }
	static _FORCE_INLINE_ int get_initial_line(const GDScriptFunction *p_function) { return p_function->_initial_line; }
	static _FORCE_INLINE_ bool is_script_valid(const GDScript *p_script) { return p_script->valid; }
	static _FORCE_INLINE_ ObjectID get_owner_id(const GDScriptInstance *p_instance) { return p_instance->owner_id; }
#ifdef DEBUG_ENABLED
	static _FORCE_INLINE_ void lock_object(Object *p_object) { p_object->_lock_index.ref(); }
	static _FORCE_INLINE_ void unlock_object(Object *p_object) { p_object->_lock_index.unref(); }
#endif
#ifdef TOOLS_ENABLED
	static _FORCE_INLINE_ void mark_edited(Object *p_object) { p_object->_edited = true; }
#endif
	static _FORCE_INLINE_ const GDType *get_type(const Object *p_object) { return p_object->_gdtype_ptr; }
	static _FORCE_INLINE_ bool has_extension(const Object *p_object) { return p_object->_extension != nullptr; }
	static _FORCE_INLINE_ const StringName &get_getter_name() { return GDScriptLanguage::get_singleton()->strings._get; }
	static _FORCE_INLINE_ const StringName &get_setter_name() { return GDScriptLanguage::get_singleton()->strings._set; }
	static _FORCE_INLINE_ const HashMap<StringName, MemberInfo> &get_member_indices(const GDScript *p_script) { return p_script->member_indices; }
	static _FORCE_INLINE_ const HashMap<StringName, MemberInfo> &get_static_variable_indices(const GDScript *p_script) { return p_script->static_variables_indices; }
	static _FORCE_INLINE_ const HashMap<StringName, Variant> &get_constants(const GDScript *p_script) { return p_script->constants; }
	static _FORCE_INLINE_ const HashMap<StringName, MethodInfo> &get_signals(const GDScript *p_script) { return p_script->_signals; }
	static _FORCE_INLINE_ const HashMap<StringName, Ref<GDScript>> &get_subclasses(const GDScript *p_script) { return p_script->subclasses; }

	static _FORCE_INLINE_ Vector<Variant> &get_static_variables(GDScript *p_script) { return p_script->static_variables; }
	static _FORCE_INLINE_ GDScript *get_base_script(const GDScript *p_script) { return p_script->base.ptr(); }
	static _FORCE_INLINE_ GDScript *get_owner_script(const GDScript *p_script) { return p_script->_owner; }
	static _FORCE_INLINE_ GDScriptNativeClass *get_native_class(const GDScript *p_script) { return p_script->native.ptr(); }
	static _FORCE_INLINE_ const HashMap<StringName, GDScriptFunction *> &get_member_functions(const GDScript *p_script) { return p_script->member_functions; }
};
