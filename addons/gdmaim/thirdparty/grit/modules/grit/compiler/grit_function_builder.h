#pragma once

#include "grit_ir.h"

#include "modules/gdscript/gdscript_codegen.h"

#include "core/templates/hash_map.h"

class GritFunctionBuilder {
	using Address = GDScriptCodeGenerator::Address;

	struct Construct {
		enum class Kind {
			IF,
			AND,
			OR,
			TERNARY,
			WHILE,
			FOR,
			SHARED,
		};

		Kind kind = Kind::IF;
		int parent_block = -1;
		int statement_index = -1;
		GritOperand target;
		GritOperand operand;
		bool is_range = false;
		int range_variables[3] = { -1, -1, -1 };
		int container_variable = -1;
	};

	struct TemporarySlot {
		int variable = -1;
		bool can_contain_object = false;
	};

	GritFunction function;
	LocalVector<Variant> constant_values;
	HashMap<uint32_t, int> stack_variables;
	HashMap<uint32_t, TemporarySlot> temporary_slots;
	HashMap<uint32_t, int> constant_indices;
	LocalVector<uint32_t> used_temporaries;
	LocalVector<uint32_t> temporaries_pending_clear;
	LocalVector<Construct> constructs;
	int current_block = -1;
	int current_line = 0;
	int statement_start_block = -1;
	int statement_start_index = 0;
	bool body_started = false;
	int default_argument_count = 0;
	int lambda_count = 0;

	static Variant::Type storage_for_type(const GDScriptDataType &p_type);
	static bool has_builtin_type(const Address &p_address);
	static bool is_builtin_type(const Address &p_address, Variant::Type p_type);

	int add_type(const GDScriptDataType &p_type);
	int add_grit_type(const GritType &p_type);
	int add_block();
	int add_variable(GritVariable::Role p_role, const GDScriptDataType &p_type, const String &p_name);
	int add_own_constant(const Variant &p_value);
	GritOperand constant_operand(const Variant &p_value);
	int append(const GritStatement &p_statement);
	GritStatement &statement_at(int p_block, int p_index);
	GritStatement make_statement(GritOpcode p_opcode) const;
	void push_construct(Construct::Kind p_kind, int p_statement_index);
	Construct *top_construct(Construct::Kind p_kind);
	bool pop_construct(Construct::Kind p_kind, Construct &r_construct);
	int open_block_statement(GritOpcode p_opcode, const GritOperand &p_condition, bool p_with_else);
	bool contains_opcode(int p_block, GritOpcode p_opcode) const;

	bool resolve(const Address &p_address, GritOperand &r_operand);
	bool resolve_all(const Vector<Address> &p_addresses, LocalVector<GritOperand> &r_operands);
	GritOperand variable_operand(int p_variable) const;
	void append_assign(const GritOperand &p_target, const GritOperand &p_source, bool p_typed, const GDScriptDataType &p_type);
	void append_call(GritOpcode p_opcode, const Address &p_target, const Address *p_base, const Vector<Address> &p_arguments, const StringName &p_name, bool p_validated);
	void open_default_argument();

public:
	void begin(const String &p_script_path, const StringName &p_name, const GDScriptDataType &p_return_type);
	GritFunction finish(int p_initial_line, const String &p_signature);

	void reject(const String &p_reason);
	bool is_rejected() const { return !function.unsupported_reason.is_empty(); }

	void add_parameter(uint32_t p_address, const StringName &p_name, bool p_is_optional, const GDScriptDataType &p_type);
	void add_local(uint32_t p_address, const StringName &p_name, const GDScriptDataType &p_type);
	void add_constant(uint32_t p_index, const Variant &p_value);
	void add_temporary(uint32_t p_slot, const GDScriptDataType &p_type);
	void pop_temporary();
	void clear_temporaries();
	void clear_address(const Address &p_address);
	void start_parameters();
	void end_parameters();
	void start_block();
	void set_line(int p_line);

	void write_unary_operator(const Address &p_target, Variant::Operator p_operator, const Address &p_operand);
	void write_binary_operator(const Address &p_target, Variant::Operator p_operator, const Address &p_left, const Address &p_right);
	void write_type_test(const Address &p_target, const Address &p_source, const GDScriptDataType &p_type);
	void write_and_left_operand(const Address &p_left);
	void write_and_right_operand(const Address &p_right);
	void write_end_and(const Address &p_target);
	void write_or_left_operand(const Address &p_left);
	void write_or_right_operand(const Address &p_right);
	void write_end_or(const Address &p_target);
	void write_start_ternary(const Address &p_target);
	void write_ternary_condition(const Address &p_condition);
	void write_ternary_true_expr(const Address &p_expression);
	void write_ternary_false_expr(const Address &p_expression);
	void write_end_ternary();
	void write_set(const Address &p_base, const Address &p_index, const Address &p_source);
	void write_get(const Address &p_target, const Address &p_index, const Address &p_source);
	void write_set_named(const Address &p_base, const StringName &p_name, const Address &p_source);
	void write_get_named(const Address &p_target, const StringName &p_name, const Address &p_source);
	void write_set_member(const Address &p_value, const StringName &p_name);
	void write_get_member(const Address &p_target, const StringName &p_name);
	void write_set_static_variable(const Address &p_value, const Address &p_class, int p_index);
	void write_get_static_variable(const Address &p_target, const Address &p_class, int p_index);
	void write_assign(const Address &p_target, const Address &p_source);
	void write_assign_with_conversion(const Address &p_target, const Address &p_source);
	void write_assign_constant(const Address &p_target, const Variant &p_value);
	void write_assign_default_parameter(const Address &p_target, const Address &p_source, bool p_use_conversion);
	void write_store_global(const Address &p_target, int p_global_index);
	void write_store_named_global(const Address &p_target, const StringName &p_global);
	void write_cast(const Address &p_target, const Address &p_source, const GDScriptDataType &p_type);
	void write_call(const Address &p_target, const Address &p_base, const StringName &p_function_name, const Vector<Address> &p_arguments);
	void write_super_call(const Address &p_target, const StringName &p_function_name, const Vector<Address> &p_arguments);
	void write_call_async(const Address &p_target, const Address &p_base, const StringName &p_function_name, const Vector<Address> &p_arguments);
	void write_lambda(const Address &p_target, const Vector<Address> &p_captures, bool p_use_self);
	void write_await(const Address &p_target, const Address &p_operand);
	void write_call_utility(const Address &p_target, const StringName &p_function, const Vector<Address> &p_arguments);
	void write_call_gdscript_utility(const Address &p_target, const StringName &p_function, const Vector<Address> &p_arguments);
	void write_call_builtin_type(const Address &p_target, const Address *p_base, Variant::Type p_type, const StringName &p_method, const Vector<Address> &p_arguments);
	void write_call_native_static(const Address &p_target, const StringName &p_class, const StringName &p_method, const Vector<Address> &p_arguments, bool p_validated);
	void write_call_method_bind(const Address &p_target, const Address &p_base, const MethodBind *p_method, const Vector<Address> &p_arguments, bool p_validated);
	void write_construct(const Address &p_target, Variant::Type p_type, const Vector<Address> &p_arguments);
	void write_construct_array(const Address &p_target, const GDScriptDataType *p_element_type, const Vector<Address> &p_arguments);
	void write_construct_dictionary(const Address &p_target, const GDScriptDataType *p_key_type, const GDScriptDataType *p_value_type, const Vector<Address> &p_arguments);
	void write_if(const Address &p_condition);
	void write_else();
	void write_endif();
	void write_jump_if_shared(const Address &p_value);
	void write_end_jump_if_shared();
	void start_while_condition();
	void write_while(const Address &p_condition);
	void write_endwhile();
	void start_for(const GDScriptDataType &p_iterator_type, const GDScriptDataType &p_list_type, bool p_is_range);
	void write_for_list_assignment(const Address &p_list);
	void write_for_range_assignment(const Address &p_from, const Address &p_to, const Address &p_step);
	void write_for(const Address &p_variable, bool p_use_conversion, bool p_is_range);
	void write_endfor();
	void write_break();
	void write_continue();
	void write_return(const Address &p_value, bool p_use_conversion);
	void write_assert(const Address &p_test, const Address &p_message);
};
