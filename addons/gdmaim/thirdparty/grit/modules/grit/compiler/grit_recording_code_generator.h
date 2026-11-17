#pragma once

#include "grit_function_builder.h"

#include "grit/runtime/grit_function_nesting.h"
#include "grit/runtime/grit_signature.h"

#include "modules/gdscript/gdscript_byte_codegen.h"

class GritScriptRecording {
	static thread_local GritScriptRecording *current;

	GritScriptRecording *previous = nullptr;

public:
	const GDScript *script = nullptr;
	LocalVector<GritFunction> functions;

	static GritScriptRecording *get_current() { return current; }

	bool should_record(const GDScript *p_script) const;

	explicit GritScriptRecording(const GDScript *p_script);
	~GritScriptRecording();
};

class GritRecordingCodeGenerator : public GDScriptCodeGenerator {
	GritFunctionNesting<GritFunction> nesting;
	GDScriptByteCodeGenerator bytecode;
	GritFunctionBuilder builder;
	GritFunctionSignature signature;
	GritScriptRecording *recording = nullptr;
	bool is_recording = false;
	int initial_line = 0;
	String class_path;
	StringName native_base;

	void reject(const String &p_reason);

public:
	virtual uint32_t add_parameter(const StringName &p_name, bool p_is_optional, const GDScriptDataType &p_type) override;
	virtual uint32_t add_local(const StringName &p_name, const GDScriptDataType &p_type) override;
	virtual uint32_t add_local_constant(const StringName &p_name, const Variant &p_constant) override;
	virtual uint32_t add_or_get_constant(const Variant &p_constant) override;
	virtual uint32_t add_or_get_name(const StringName &p_name) override;
	virtual uint32_t add_temporary(const GDScriptDataType &p_type) override;
	virtual void pop_temporary() override;
	virtual void clear_temporaries() override;
	virtual void clear_address(const Address &p_address) override;
	virtual bool is_local_dirty(const Address &p_address) const override;

	virtual void start_parameters() override;
	virtual void end_parameters() override;

	virtual void start_block() override;
	virtual void end_block() override;

	virtual void write_start(GDScript *p_script, const StringName &p_function_name, bool p_static, Variant p_rpc_config, const GDScriptDataType &p_return_type) override;
	virtual GDScriptFunction *write_end() override;

#ifdef DEBUG_ENABLED
	virtual void set_signature(const String &p_signature) override;
#endif
	virtual void set_initial_line(int p_line) override;

	virtual void write_type_adjust(const Address &p_target, Variant::Type p_new_type) override;
	virtual void write_unary_operator(const Address &p_target, Variant::Operator p_operator, const Address &p_left_operand) override;
	virtual void write_binary_operator(const Address &p_target, Variant::Operator p_operator, const Address &p_left_operand, const Address &p_right_operand) override;
	virtual void write_type_test(const Address &p_target, const Address &p_source, const GDScriptDataType &p_type) override;
	virtual void write_and_left_operand(const Address &p_left_operand) override;
	virtual void write_and_right_operand(const Address &p_right_operand) override;
	virtual void write_end_and(const Address &p_target) override;
	virtual void write_or_left_operand(const Address &p_left_operand) override;
	virtual void write_or_right_operand(const Address &p_right_operand) override;
	virtual void write_end_or(const Address &p_target) override;
	virtual void write_start_ternary(const Address &p_target) override;
	virtual void write_ternary_condition(const Address &p_condition) override;
	virtual void write_ternary_true_expr(const Address &p_expr) override;
	virtual void write_ternary_false_expr(const Address &p_expr) override;
	virtual void write_end_ternary() override;
	virtual void write_set(const Address &p_target, const Address &p_index, const Address &p_source) override;
	virtual void write_get(const Address &p_target, const Address &p_index, const Address &p_source) override;
	virtual void write_set_named(const Address &p_target, const StringName &p_name, const Address &p_source) override;
	virtual void write_get_named(const Address &p_target, const StringName &p_name, const Address &p_source) override;
	virtual void write_set_member(const Address &p_value, const StringName &p_name) override;
	virtual void write_get_member(const Address &p_target, const StringName &p_name) override;
	virtual void write_set_static_variable(const Address &p_value, const Address &p_class, int p_index) override;
	virtual void write_get_static_variable(const Address &p_target, const Address &p_class, int p_index) override;
	virtual void write_assign(const Address &p_target, const Address &p_source) override;
	virtual void write_assign_with_conversion(const Address &p_target, const Address &p_source) override;
	virtual void write_assign_null(const Address &p_target) override;
	virtual void write_assign_true(const Address &p_target) override;
	virtual void write_assign_false(const Address &p_target) override;
	virtual void write_assign_default_parameter(const Address &p_dst, const Address &p_src, bool p_use_conversion) override;
	virtual void write_store_global(const Address &p_dst, int p_global_index) override;
	virtual void write_store_named_global(const Address &p_dst, const StringName &p_global) override;
	virtual void write_cast(const Address &p_target, const Address &p_source, const GDScriptDataType &p_type) override;
	virtual void write_call(const Address &p_target, const Address &p_base, const StringName &p_function_name, const Vector<Address> &p_arguments) override;
	virtual void write_super_call(const Address &p_target, const StringName &p_function_name, const Vector<Address> &p_arguments) override;
	virtual void write_call_async(const Address &p_target, const Address &p_base, const StringName &p_function_name, const Vector<Address> &p_arguments) override;
	virtual void write_call_utility(const Address &p_target, const StringName &p_function, const Vector<Address> &p_arguments) override;
	virtual void write_call_gdscript_utility(const Address &p_target, const StringName &p_function, const Vector<Address> &p_arguments) override;
	virtual void write_call_builtin_type(const Address &p_target, const Address &p_base, Variant::Type p_type, const StringName &p_method, const Vector<Address> &p_arguments) override;
	virtual void write_call_builtin_type_static(const Address &p_target, Variant::Type p_type, const StringName &p_method, const Vector<Address> &p_arguments) override;
	virtual void write_call_native_static(const Address &p_target, const StringName &p_class, const StringName &p_method, const Vector<Address> &p_arguments) override;
	virtual void write_call_native_static_validated(const Address &p_target, MethodBind *p_method, const Vector<Address> &p_arguments) override;
	virtual void write_call_method_bind(const Address &p_target, const Address &p_base, MethodBind *p_method, const Vector<Address> &p_arguments) override;
	virtual void write_call_method_bind_validated(const Address &p_target, const Address &p_base, MethodBind *p_method, const Vector<Address> &p_arguments) override;
	virtual void write_call_self(const Address &p_target, const StringName &p_function_name, const Vector<Address> &p_arguments) override;
	virtual void write_call_self_async(const Address &p_target, const StringName &p_function_name, const Vector<Address> &p_arguments) override;
	virtual void write_call_script_function(const Address &p_target, const Address &p_base, const StringName &p_function_name, const Vector<Address> &p_arguments) override;
	virtual void write_lambda(const Address &p_target, GDScriptFunction *p_function, const Vector<Address> &p_captures, bool p_use_self) override;
	virtual void write_construct(const Address &p_target, Variant::Type p_type, const Vector<Address> &p_arguments) override;
	virtual void write_construct_array(const Address &p_target, const Vector<Address> &p_arguments) override;
	virtual void write_construct_typed_array(const Address &p_target, const GDScriptDataType &p_element_type, const Vector<Address> &p_arguments) override;
	virtual void write_construct_dictionary(const Address &p_target, const Vector<Address> &p_arguments) override;
	virtual void write_construct_typed_dictionary(const Address &p_target, const GDScriptDataType &p_key_type, const GDScriptDataType &p_value_type, const Vector<Address> &p_arguments) override;
	virtual void write_await(const Address &p_target, const Address &p_operand) override;
	virtual void write_if(const Address &p_condition) override;
	virtual void write_else() override;
	virtual void write_endif() override;
	virtual void write_jump_if_shared(const Address &p_value) override;
	virtual void write_end_jump_if_shared() override;
	virtual void start_for(const GDScriptDataType &p_iterator_type, const GDScriptDataType &p_list_type, bool p_is_range) override;
	virtual void write_for_list_assignment(const Address &p_list) override;
	virtual void write_for_range_assignment(const Address &p_from, const Address &p_to, const Address &p_step) override;
	virtual void write_for(const Address &p_variable, bool p_use_conversion, bool p_is_range) override;
	virtual void write_endfor(bool p_is_range) override;
	virtual void start_while_condition() override;
	virtual void write_while(const Address &p_condition) override;
	virtual void write_endwhile() override;
	virtual void write_break() override;
	virtual void write_continue() override;
	virtual void write_breakpoint() override;
	virtual void write_newline(int p_line) override;
	virtual void write_return(const Address &p_return_value, bool p_use_conversion) override;
	virtual void write_assert(const Address &p_test, const Address &p_message) override;

};
