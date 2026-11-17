#include "grit_recording_code_generator.h"

#include "grit/runtime/grit_runtime_access.h"

#include "modules/gdscript/gdscript.h"

#include "core/object/method_bind.h"

thread_local GritScriptRecording *GritScriptRecording::current = nullptr;

GritScriptRecording::GritScriptRecording(const GDScript *p_script) {
	script = p_script;
	previous = current;
	current = this;
}

GritScriptRecording::~GritScriptRecording() {
	current = previous;
}

bool GritScriptRecording::should_record(const GDScript *p_script) const {
	const GDScript *root = p_script;
	while (root && !root->is_root_script()) {
		root = GritRuntimeAccess::get_owner_script(root);
	}
	if (!root || GritRegistry::make_class_path(p_script).is_empty()) {
		return false;
	}
	if (script == nullptr) {
		return root->get_script_path().is_absolute_path();
	}
	return script == root;
}

void GritRecordingCodeGenerator::reject(const String &p_reason) {
	if (is_recording) {
		builder.reject(p_reason);
	}
}

uint32_t GritRecordingCodeGenerator::add_parameter(const StringName &p_name, bool p_is_optional, const GDScriptDataType &p_type) {
	const uint32_t address = bytecode.add_parameter(p_name, p_is_optional, p_type);
	signature.parameter_types.push_back(p_type);
	if (p_is_optional) {
		signature.optional_parameter_count++;
	}
	if (is_recording) {
		builder.add_parameter(address, p_name, p_is_optional, p_type);
	}
	return address;
}

uint32_t GritRecordingCodeGenerator::add_local(const StringName &p_name, const GDScriptDataType &p_type) {
	const uint32_t address = bytecode.add_local(p_name, p_type);
	if (is_recording) {
		builder.add_local(address, p_name, p_type);
	}
	return address;
}

uint32_t GritRecordingCodeGenerator::add_local_constant(const StringName &p_name, const Variant &p_constant) {
	const uint32_t index = bytecode.add_local_constant(p_name, p_constant);
	if (is_recording) {
		builder.add_constant(index, p_constant);
	}
	return index;
}

uint32_t GritRecordingCodeGenerator::add_or_get_constant(const Variant &p_constant) {
	const uint32_t index = bytecode.add_or_get_constant(p_constant);
	if (is_recording) {
		builder.add_constant(index, p_constant);
	}
	return index;
}

uint32_t GritRecordingCodeGenerator::add_or_get_name(const StringName &p_name) {
	return bytecode.add_or_get_name(p_name);
}

uint32_t GritRecordingCodeGenerator::add_temporary(const GDScriptDataType &p_type) {
	const uint32_t slot = bytecode.add_temporary(p_type);
	if (is_recording) {
		builder.add_temporary(slot, p_type);
	}
	return slot;
}

void GritRecordingCodeGenerator::pop_temporary() {
	bytecode.pop_temporary();
	if (is_recording) {
		builder.pop_temporary();
	}
}

void GritRecordingCodeGenerator::clear_temporaries() {
	bytecode.clear_temporaries();
	if (is_recording) {
		builder.clear_temporaries();
	}
}

void GritRecordingCodeGenerator::clear_address(const Address &p_address) {
	bytecode.clear_address(p_address);
	if (is_recording) {
		builder.clear_address(p_address);
	}
}

bool GritRecordingCodeGenerator::is_local_dirty(const Address &p_address) const {
	return bytecode.is_local_dirty(p_address);
}

void GritRecordingCodeGenerator::start_parameters() {
	bytecode.start_parameters();
	if (is_recording) {
		builder.start_parameters();
	}
}

void GritRecordingCodeGenerator::end_parameters() {
	bytecode.end_parameters();
	if (is_recording) {
		builder.end_parameters();
	}
}

void GritRecordingCodeGenerator::start_block() {
	bytecode.start_block();
	if (is_recording) {
		builder.start_block();
	}
}

void GritRecordingCodeGenerator::end_block() {
	bytecode.end_block();
}

void GritRecordingCodeGenerator::write_start(GDScript *p_script, const StringName &p_function_name, bool p_static, Variant p_rpc_config, const GDScriptDataType &p_return_type) {
	bytecode.write_start(p_script, p_function_name, p_static, p_rpc_config, p_return_type);
	signature.is_static = p_static;
	signature.return_type = p_return_type;

	recording = GritScriptRecording::get_current();
	is_recording = recording && recording->should_record(p_script);
	class_path = is_recording ? GritRegistry::make_class_path(p_script) : String();
	native_base = is_recording ? p_script->get_instance_base_type() : StringName();
	if (is_recording) {
		builder.begin(p_script->get_script_path(), p_function_name, p_return_type);
	}
}

GDScriptFunction *GritRecordingCodeGenerator::write_end() {
	GDScriptFunction *function = bytecode.write_end();
	String root_key;
	if (is_recording) {
		GritFunction recorded = builder.finish(initial_line, signature.describe());
		recorded.registry_key = GritRegistry::make_key(class_path, recorded.name, recorded.initial_line);
		recorded.class_path = class_path;
		recorded.native_base = native_base;
		root_key = recorded.registry_key;
		if (nesting.is_nested()) {
			const StringName name = recorded.name;
			nesting.add(name, initial_line, std::move(recorded));
		} else {
			recording->functions.push_back(recorded);
		}
		is_recording = false;
	}
	if (!nesting.is_nested()) {
		nesting.resolve(root_key, [this](const String &p_key, GritFunction &p_nested) {
			p_nested.registry_key = p_key;
			if (recording) {
				recording->functions.push_back(p_nested);
			}
		});
	}
	return function;
}

#ifdef DEBUG_ENABLED
void GritRecordingCodeGenerator::set_signature(const String &p_signature) {
	bytecode.set_signature(p_signature);
}
#endif

void GritRecordingCodeGenerator::set_initial_line(int p_line) {
	bytecode.set_initial_line(p_line);
	initial_line = p_line;
}

void GritRecordingCodeGenerator::write_type_adjust(const Address &p_target, Variant::Type p_new_type) {
	bytecode.write_type_adjust(p_target, p_new_type);
	reject("type adjustment");
}

void GritRecordingCodeGenerator::write_unary_operator(const Address &p_target, Variant::Operator p_operator, const Address &p_left_operand) {
	bytecode.write_unary_operator(p_target, p_operator, p_left_operand);
	if (is_recording) {
		builder.write_unary_operator(p_target, p_operator, p_left_operand);
	}
}

void GritRecordingCodeGenerator::write_binary_operator(const Address &p_target, Variant::Operator p_operator, const Address &p_left_operand, const Address &p_right_operand) {
	bytecode.write_binary_operator(p_target, p_operator, p_left_operand, p_right_operand);
	if (is_recording) {
		builder.write_binary_operator(p_target, p_operator, p_left_operand, p_right_operand);
	}
}

void GritRecordingCodeGenerator::write_type_test(const Address &p_target, const Address &p_source, const GDScriptDataType &p_type) {
	bytecode.write_type_test(p_target, p_source, p_type);
	if (is_recording) {
		builder.write_type_test(p_target, p_source, p_type);
	}
}

void GritRecordingCodeGenerator::write_and_left_operand(const Address &p_left_operand) {
	bytecode.write_and_left_operand(p_left_operand);
	if (is_recording) {
		builder.write_and_left_operand(p_left_operand);
	}
}

void GritRecordingCodeGenerator::write_and_right_operand(const Address &p_right_operand) {
	bytecode.write_and_right_operand(p_right_operand);
	if (is_recording) {
		builder.write_and_right_operand(p_right_operand);
	}
}

void GritRecordingCodeGenerator::write_end_and(const Address &p_target) {
	bytecode.write_end_and(p_target);
	if (is_recording) {
		builder.write_end_and(p_target);
	}
}

void GritRecordingCodeGenerator::write_or_left_operand(const Address &p_left_operand) {
	bytecode.write_or_left_operand(p_left_operand);
	if (is_recording) {
		builder.write_or_left_operand(p_left_operand);
	}
}

void GritRecordingCodeGenerator::write_or_right_operand(const Address &p_right_operand) {
	bytecode.write_or_right_operand(p_right_operand);
	if (is_recording) {
		builder.write_or_right_operand(p_right_operand);
	}
}

void GritRecordingCodeGenerator::write_end_or(const Address &p_target) {
	bytecode.write_end_or(p_target);
	if (is_recording) {
		builder.write_end_or(p_target);
	}
}

void GritRecordingCodeGenerator::write_start_ternary(const Address &p_target) {
	bytecode.write_start_ternary(p_target);
	if (is_recording) {
		builder.write_start_ternary(p_target);
	}
}

void GritRecordingCodeGenerator::write_ternary_condition(const Address &p_condition) {
	bytecode.write_ternary_condition(p_condition);
	if (is_recording) {
		builder.write_ternary_condition(p_condition);
	}
}

void GritRecordingCodeGenerator::write_ternary_true_expr(const Address &p_expr) {
	bytecode.write_ternary_true_expr(p_expr);
	if (is_recording) {
		builder.write_ternary_true_expr(p_expr);
	}
}

void GritRecordingCodeGenerator::write_ternary_false_expr(const Address &p_expr) {
	bytecode.write_ternary_false_expr(p_expr);
	if (is_recording) {
		builder.write_ternary_false_expr(p_expr);
	}
}

void GritRecordingCodeGenerator::write_end_ternary() {
	bytecode.write_end_ternary();
	if (is_recording) {
		builder.write_end_ternary();
	}
}

void GritRecordingCodeGenerator::write_set(const Address &p_target, const Address &p_index, const Address &p_source) {
	bytecode.write_set(p_target, p_index, p_source);
	if (is_recording) {
		builder.write_set(p_target, p_index, p_source);
	}
}

void GritRecordingCodeGenerator::write_get(const Address &p_target, const Address &p_index, const Address &p_source) {
	bytecode.write_get(p_target, p_index, p_source);
	if (is_recording) {
		builder.write_get(p_target, p_index, p_source);
	}
}

void GritRecordingCodeGenerator::write_set_named(const Address &p_target, const StringName &p_name, const Address &p_source) {
	bytecode.write_set_named(p_target, p_name, p_source);
	if (is_recording) {
		builder.write_set_named(p_target, p_name, p_source);
	}
}

void GritRecordingCodeGenerator::write_get_named(const Address &p_target, const StringName &p_name, const Address &p_source) {
	bytecode.write_get_named(p_target, p_name, p_source);
	if (is_recording) {
		builder.write_get_named(p_target, p_name, p_source);
	}
}

void GritRecordingCodeGenerator::write_set_member(const Address &p_value, const StringName &p_name) {
	bytecode.write_set_member(p_value, p_name);
	if (is_recording) {
		builder.write_set_member(p_value, p_name);
	}
}

void GritRecordingCodeGenerator::write_get_member(const Address &p_target, const StringName &p_name) {
	bytecode.write_get_member(p_target, p_name);
	if (is_recording) {
		builder.write_get_member(p_target, p_name);
	}
}

void GritRecordingCodeGenerator::write_set_static_variable(const Address &p_value, const Address &p_class, int p_index) {
	bytecode.write_set_static_variable(p_value, p_class, p_index);
	if (is_recording) {
		builder.write_set_static_variable(p_value, p_class, p_index);
	}
}

void GritRecordingCodeGenerator::write_get_static_variable(const Address &p_target, const Address &p_class, int p_index) {
	bytecode.write_get_static_variable(p_target, p_class, p_index);
	if (is_recording) {
		builder.write_get_static_variable(p_target, p_class, p_index);
	}
}

void GritRecordingCodeGenerator::write_assign(const Address &p_target, const Address &p_source) {
	bytecode.write_assign(p_target, p_source);
	if (is_recording) {
		builder.write_assign(p_target, p_source);
	}
}

void GritRecordingCodeGenerator::write_assign_with_conversion(const Address &p_target, const Address &p_source) {
	bytecode.write_assign_with_conversion(p_target, p_source);
	if (is_recording) {
		builder.write_assign_with_conversion(p_target, p_source);
	}
}

void GritRecordingCodeGenerator::write_assign_null(const Address &p_target) {
	bytecode.write_assign_null(p_target);
	if (is_recording) {
		builder.write_assign_constant(p_target, Variant());
	}
}

void GritRecordingCodeGenerator::write_assign_true(const Address &p_target) {
	bytecode.write_assign_true(p_target);
	if (is_recording) {
		builder.write_assign_constant(p_target, true);
	}
}

void GritRecordingCodeGenerator::write_assign_false(const Address &p_target) {
	bytecode.write_assign_false(p_target);
	if (is_recording) {
		builder.write_assign_constant(p_target, false);
	}
}

void GritRecordingCodeGenerator::write_assign_default_parameter(const Address &p_dst, const Address &p_src, bool p_use_conversion) {
	bytecode.write_assign_default_parameter(p_dst, p_src, p_use_conversion);
	if (is_recording) {
		builder.write_assign_default_parameter(p_dst, p_src, p_use_conversion);
	}
}

void GritRecordingCodeGenerator::write_store_global(const Address &p_dst, int p_global_index) {
	bytecode.write_store_global(p_dst, p_global_index);
	if (is_recording) {
		builder.write_store_global(p_dst, p_global_index);
	}
}

void GritRecordingCodeGenerator::write_store_named_global(const Address &p_dst, const StringName &p_global) {
	bytecode.write_store_named_global(p_dst, p_global);
	if (is_recording) {
		builder.write_store_named_global(p_dst, p_global);
	}
}

void GritRecordingCodeGenerator::write_cast(const Address &p_target, const Address &p_source, const GDScriptDataType &p_type) {
	bytecode.write_cast(p_target, p_source, p_type);
	if (is_recording) {
		builder.write_cast(p_target, p_source, p_type);
	}
}

void GritRecordingCodeGenerator::write_call(const Address &p_target, const Address &p_base, const StringName &p_function_name, const Vector<Address> &p_arguments) {
	bytecode.write_call(p_target, p_base, p_function_name, p_arguments);
	if (is_recording) {
		builder.write_call(p_target, p_base, p_function_name, p_arguments);
	}
}

void GritRecordingCodeGenerator::write_super_call(const Address &p_target, const StringName &p_function_name, const Vector<Address> &p_arguments) {
	bytecode.write_super_call(p_target, p_function_name, p_arguments);
	if (is_recording) {
		builder.write_super_call(p_target, p_function_name, p_arguments);
	}
}

void GritRecordingCodeGenerator::write_call_async(const Address &p_target, const Address &p_base, const StringName &p_function_name, const Vector<Address> &p_arguments) {
	bytecode.write_call_async(p_target, p_base, p_function_name, p_arguments);
	if (is_recording) {
		builder.write_call_async(p_target, p_base, p_function_name, p_arguments);
	}
}

void GritRecordingCodeGenerator::write_call_utility(const Address &p_target, const StringName &p_function, const Vector<Address> &p_arguments) {
	bytecode.write_call_utility(p_target, p_function, p_arguments);
	if (is_recording) {
		builder.write_call_utility(p_target, p_function, p_arguments);
	}
}

void GritRecordingCodeGenerator::write_call_gdscript_utility(const Address &p_target, const StringName &p_function, const Vector<Address> &p_arguments) {
	bytecode.write_call_gdscript_utility(p_target, p_function, p_arguments);
	if (is_recording) {
		builder.write_call_gdscript_utility(p_target, p_function, p_arguments);
	}
}

void GritRecordingCodeGenerator::write_call_builtin_type(const Address &p_target, const Address &p_base, Variant::Type p_type, const StringName &p_method, const Vector<Address> &p_arguments) {
	bytecode.write_call_builtin_type(p_target, p_base, p_type, p_method, p_arguments);
	if (is_recording) {
		builder.write_call_builtin_type(p_target, &p_base, p_type, p_method, p_arguments);
	}
}

void GritRecordingCodeGenerator::write_call_builtin_type_static(const Address &p_target, Variant::Type p_type, const StringName &p_method, const Vector<Address> &p_arguments) {
	bytecode.write_call_builtin_type_static(p_target, p_type, p_method, p_arguments);
	if (is_recording) {
		builder.write_call_builtin_type(p_target, nullptr, p_type, p_method, p_arguments);
	}
}

void GritRecordingCodeGenerator::write_call_native_static(const Address &p_target, const StringName &p_class, const StringName &p_method, const Vector<Address> &p_arguments) {
	bytecode.write_call_native_static(p_target, p_class, p_method, p_arguments);
	if (is_recording) {
		builder.write_call_native_static(p_target, p_class, p_method, p_arguments, false);
	}
}

void GritRecordingCodeGenerator::write_call_native_static_validated(const Address &p_target, MethodBind *p_method, const Vector<Address> &p_arguments) {
	bytecode.write_call_native_static_validated(p_target, p_method, p_arguments);
	if (is_recording) {
		builder.write_call_native_static(p_target, p_method->get_instance_class(), p_method->get_name(), p_arguments, true);
	}
}

void GritRecordingCodeGenerator::write_call_method_bind(const Address &p_target, const Address &p_base, MethodBind *p_method, const Vector<Address> &p_arguments) {
	bytecode.write_call_method_bind(p_target, p_base, p_method, p_arguments);
	if (is_recording) {
		builder.write_call_method_bind(p_target, p_base, p_method, p_arguments, false);
	}
}

void GritRecordingCodeGenerator::write_call_method_bind_validated(const Address &p_target, const Address &p_base, MethodBind *p_method, const Vector<Address> &p_arguments) {
	bytecode.write_call_method_bind_validated(p_target, p_base, p_method, p_arguments);
	if (is_recording) {
		builder.write_call_method_bind(p_target, p_base, p_method, p_arguments, true);
	}
}

void GritRecordingCodeGenerator::write_call_self(const Address &p_target, const StringName &p_function_name, const Vector<Address> &p_arguments) {
	bytecode.write_call_self(p_target, p_function_name, p_arguments);
	if (is_recording) {
		builder.write_call(p_target, Address(Address::SELF), p_function_name, p_arguments);
	}
}

void GritRecordingCodeGenerator::write_call_self_async(const Address &p_target, const StringName &p_function_name, const Vector<Address> &p_arguments) {
	bytecode.write_call_self_async(p_target, p_function_name, p_arguments);
	if (is_recording) {
		builder.write_call_async(p_target, Address(Address::SELF), p_function_name, p_arguments);
	}
}

void GritRecordingCodeGenerator::write_call_script_function(const Address &p_target, const Address &p_base, const StringName &p_function_name, const Vector<Address> &p_arguments) {
	bytecode.write_call_script_function(p_target, p_base, p_function_name, p_arguments);
	if (is_recording) {
		builder.write_call(p_target, p_base, p_function_name, p_arguments);
	}
}

void GritRecordingCodeGenerator::write_lambda(const Address &p_target, GDScriptFunction *p_function, const Vector<Address> &p_captures, bool p_use_self) {
	bytecode.write_lambda(p_target, p_function, p_captures, p_use_self);
	if (is_recording) {
		builder.write_lambda(p_target, p_captures, p_use_self);
	}
}

void GritRecordingCodeGenerator::write_construct(const Address &p_target, Variant::Type p_type, const Vector<Address> &p_arguments) {
	bytecode.write_construct(p_target, p_type, p_arguments);
	if (is_recording) {
		builder.write_construct(p_target, p_type, p_arguments);
	}
}

void GritRecordingCodeGenerator::write_construct_array(const Address &p_target, const Vector<Address> &p_arguments) {
	bytecode.write_construct_array(p_target, p_arguments);
	if (is_recording) {
		builder.write_construct_array(p_target, nullptr, p_arguments);
	}
}

void GritRecordingCodeGenerator::write_construct_typed_array(const Address &p_target, const GDScriptDataType &p_element_type, const Vector<Address> &p_arguments) {
	bytecode.write_construct_typed_array(p_target, p_element_type, p_arguments);
	if (is_recording) {
		builder.write_construct_array(p_target, &p_element_type, p_arguments);
	}
}

void GritRecordingCodeGenerator::write_construct_dictionary(const Address &p_target, const Vector<Address> &p_arguments) {
	bytecode.write_construct_dictionary(p_target, p_arguments);
	if (is_recording) {
		builder.write_construct_dictionary(p_target, nullptr, nullptr, p_arguments);
	}
}

void GritRecordingCodeGenerator::write_construct_typed_dictionary(const Address &p_target, const GDScriptDataType &p_key_type, const GDScriptDataType &p_value_type, const Vector<Address> &p_arguments) {
	bytecode.write_construct_typed_dictionary(p_target, p_key_type, p_value_type, p_arguments);
	if (is_recording) {
		builder.write_construct_dictionary(p_target, &p_key_type, &p_value_type, p_arguments);
	}
}

void GritRecordingCodeGenerator::write_await(const Address &p_target, const Address &p_operand) {
	bytecode.write_await(p_target, p_operand);
	if (is_recording) {
		builder.write_await(p_target, p_operand);
	}
}

void GritRecordingCodeGenerator::write_if(const Address &p_condition) {
	bytecode.write_if(p_condition);
	if (is_recording) {
		builder.write_if(p_condition);
	}
}

void GritRecordingCodeGenerator::write_else() {
	bytecode.write_else();
	if (is_recording) {
		builder.write_else();
	}
}

void GritRecordingCodeGenerator::write_endif() {
	bytecode.write_endif();
	if (is_recording) {
		builder.write_endif();
	}
}

void GritRecordingCodeGenerator::write_jump_if_shared(const Address &p_value) {
	bytecode.write_jump_if_shared(p_value);
	if (is_recording) {
		builder.write_jump_if_shared(p_value);
	}
}

void GritRecordingCodeGenerator::write_end_jump_if_shared() {
	bytecode.write_end_jump_if_shared();
	if (is_recording) {
		builder.write_end_jump_if_shared();
	}
}

void GritRecordingCodeGenerator::start_for(const GDScriptDataType &p_iterator_type, const GDScriptDataType &p_list_type, bool p_is_range) {
	bytecode.start_for(p_iterator_type, p_list_type, p_is_range);
	if (is_recording) {
		builder.start_for(p_iterator_type, p_list_type, p_is_range);
	}
}

void GritRecordingCodeGenerator::write_for_list_assignment(const Address &p_list) {
	bytecode.write_for_list_assignment(p_list);
	if (is_recording) {
		builder.write_for_list_assignment(p_list);
	}
}

void GritRecordingCodeGenerator::write_for_range_assignment(const Address &p_from, const Address &p_to, const Address &p_step) {
	bytecode.write_for_range_assignment(p_from, p_to, p_step);
	if (is_recording) {
		builder.write_for_range_assignment(p_from, p_to, p_step);
	}
}

void GritRecordingCodeGenerator::write_for(const Address &p_variable, bool p_use_conversion, bool p_is_range) {
	bytecode.write_for(p_variable, p_use_conversion, p_is_range);
	if (is_recording) {
		builder.write_for(p_variable, p_use_conversion, p_is_range);
	}
}

void GritRecordingCodeGenerator::write_endfor(bool p_is_range) {
	bytecode.write_endfor(p_is_range);
	if (is_recording) {
		builder.write_endfor();
	}
}

void GritRecordingCodeGenerator::start_while_condition() {
	bytecode.start_while_condition();
	if (is_recording) {
		builder.start_while_condition();
	}
}

void GritRecordingCodeGenerator::write_while(const Address &p_condition) {
	bytecode.write_while(p_condition);
	if (is_recording) {
		builder.write_while(p_condition);
	}
}

void GritRecordingCodeGenerator::write_endwhile() {
	bytecode.write_endwhile();
	if (is_recording) {
		builder.write_endwhile();
	}
}

void GritRecordingCodeGenerator::write_break() {
	bytecode.write_break();
	if (is_recording) {
		builder.write_break();
	}
}

void GritRecordingCodeGenerator::write_continue() {
	bytecode.write_continue();
	if (is_recording) {
		builder.write_continue();
	}
}

void GritRecordingCodeGenerator::write_breakpoint() {
	bytecode.write_breakpoint();
	reject("breakpoint");
}

void GritRecordingCodeGenerator::write_newline(int p_line) {
	nesting.mark_newline();
	bytecode.write_newline(p_line);
	if (is_recording) {
		builder.set_line(p_line);
	}
}

void GritRecordingCodeGenerator::write_return(const Address &p_return_value, bool p_use_conversion) {
	bytecode.write_return(p_return_value, p_use_conversion);
	if (is_recording) {
		builder.write_return(p_return_value, p_use_conversion);
	}
}

void GritRecordingCodeGenerator::write_assert(const Address &p_test, const Address &p_message) {
	bytecode.write_assert(p_test, p_message);
	nesting.mark_assert();
	if (is_recording) {
		builder.write_assert(p_test, p_message);
	}
}
