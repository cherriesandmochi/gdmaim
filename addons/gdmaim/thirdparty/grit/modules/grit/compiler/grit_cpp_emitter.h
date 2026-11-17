#pragma once

#include "grit_ir.h"

#include "core/string/string_builder.h"
#include "core/templates/hash_map.h"

class GritCppEmitter {
	struct Scope {
		LocalVector<String> prelude;
		LocalVector<String> postlude;
	};

	struct Loop {
		bool is_for_each = false;
		bool is_continue_used = false;
		String continue_label;
	};

	struct HoistedState {
		String name;
		Variant::Type storage = Variant::NIL;
		bool is_saved = true;
	};

	struct StatementState {
		String first;
		String second;
		String third;
	};

	struct DirectTarget {
		String symbol;
		const GritFunction *function = nullptr;
	};

	const GritFunction *function = nullptr;
	const HashMap<String, DirectTarget> *direct_targets = nullptr;
	StringBuilder body;
	int indentation = 0;
	int symbol_count = 0;
	int current_line = -1;
	LocalVector<Loop> loops;
	LocalVector<String> statics;
	HashMap<String, String> static_symbols;
	LocalVector<String> constant_initializers;
	HashMap<int, int> constant_slots;
	HashMap<String, int> script_slots;
	bool uses_class = false;
	bool uses_members = false;
	LocalVector<bool> variables_read;
	bool is_coroutine = false;
	LocalVector<HoistedState> hoisted;
	HashMap<const GritStatement *, StatementState> statement_states;
	LocalVector<String> resume_cases;

	void write_line(const String &p_line);
	void open_scope(const String &p_header);
	void close_scope(const String &p_footer = "}");
	void write_scoped(const Scope &p_scope, const LocalVector<String> &p_core);
	static void nest_block(const Scope &p_scope, const LocalVector<String> &p_core, LocalVector<String> &r_core);
	String next_symbol(const String &p_prefix);
	String add_static(const String &p_type, const String &p_prefix, const String &p_initializer);
	int line_of(const GritStatement &p_statement) const;
	String error_return() const;
	String checked(const String &p_call) const;

	String variable_name(int p_variable) const;
	static String variable_name_in(const GritFunction &p_function, int p_variable);
	Variant::Type storage_of(const GritOperand &p_operand) const;
	bool is_native(const GritOperand &p_operand) const;
	const GritType &type_of(int p_type) const;
	Variant::Type static_type_of(const GritOperand &p_operand) const;
	Variant::Type value_type_of(const GritOperand &p_operand) const;
	StringName native_class_of(const GritOperand &p_operand) const;
	bool object_value_assignable(const GritOperand &p_value, const StringName &p_class) const;
	String native_expression(const GritOperand &p_operand) const;
	String converted(const GritOperand &p_operand, Variant::Type p_storage) const;
	bool is_lvalue(const GritOperand &p_operand) const;
	String lvalue(const GritOperand &p_operand) const;
	String truth_value(const GritOperand &p_operand, Scope &r_scope);
	String variant_value(const GritOperand &p_operand, Scope &r_scope);
	String variant_base(const GritOperand &p_operand, Scope &r_scope);
	String variant_target(const GritOperand &p_target, Scope &r_scope);
	String pointer_input(const GritOperand &p_operand, Variant::Type p_type, Scope &r_scope);
	String pointer_base(const GritOperand &p_operand, Variant::Type p_type, Scope &r_scope);
	String pointer_output(const GritOperand &p_target, Variant::Type p_type, Scope &r_scope);
	String arguments_array(const GritStatement &p_statement, int p_first, Scope &r_scope);
	String pointer_arguments(const GritStatement &p_statement, int p_first, const LocalVector<Variant::Type> &p_types, Scope &r_scope);
	String constant_slot(int p_constant);
	String script_value(const GritType &p_type);
	String string_name(const StringName &p_name);
	void guard_members(const GritStatement &p_statement, Scope &r_scope);
	void assign_value(const GritOperand &p_target, const String &p_expression, Variant::Type p_storage, Scope &r_scope, LocalVector<String> &r_core);
	void assign_typed(const GritOperand &p_target, const GritOperand &p_source, const GritType &p_type, int p_line, Scope &r_scope, LocalVector<String> &r_core);
	void clear_to_nil(const GritOperand &p_target, Scope &r_scope, LocalVector<String> &r_core);

	bool native_unary(const GritStatement &p_statement, String &r_expression, Variant::Type &r_storage);
	bool native_binary(const GritStatement &p_statement, String &r_expression, Variant::Type &r_storage);
	bool write_pointer_operator(const GritStatement &p_statement, Scope &r_scope, LocalVector<String> &r_core);
	bool write_pointer_call(const GritStatement &p_statement, Scope &r_scope, LocalVector<String> &r_core);
	bool write_inline_access(const GritStatement &p_statement, Scope &r_scope, LocalVector<String> &r_core);
	bool write_dictionary_access(const GritStatement &p_statement, Scope &r_scope, LocalVector<String> &r_core);
	bool write_property_access(const GritStatement &p_statement, Scope &r_scope, LocalVector<String> &r_core);
	bool write_script_member_access(const GritStatement &p_statement, Scope &r_scope, LocalVector<String> &r_core);
	bool as_builtin_call(const GritStatement &p_statement, GritStatement &r_builtin) const;
	String index_expression(const GritOperand &p_operand, Scope &r_scope);

	void collect_reads();
	String hoist(const String &p_prefix, Variant::Type p_storage, bool p_is_saved);
	void collect_hoisted(int p_block);
	int slot_count() const;
	void save_slots(const String &p_slots, LocalVector<String> &r_lines) const;
	void restore_slots(const String &p_slots, LocalVector<String> &r_lines) const;
	void write_await(const GritStatement &p_statement);
	void write_block(int p_block);
	void write_statement(const GritStatement &p_statement);
	void write_operator(const GritStatement &p_statement);
	void write_access(const GritStatement &p_statement);
	void write_generic_access(const GritStatement &p_statement, Scope &r_scope, LocalVector<String> &r_core);
	void write_call(const GritStatement &p_statement);
	bool write_direct_call(const GritStatement &p_statement);
	bool write_type_of(const GritStatement &p_statement);
	void write_generic_call(const GritStatement &p_statement, Scope &r_scope, LocalVector<String> &r_core);
	void write_control(const GritStatement &p_statement);
	bool write_typed_for_each(const GritStatement &p_statement);
	void write_return(const GritStatement &p_statement);

	static bool is_numeric(Variant::Type p_type);
	static bool is_dynamic_numeric_operator(Variant::Operator p_operator);
	static String packed_element_type(Variant::Type p_type);
	static Variant::Type packed_element_storage(Variant::Type p_type);
	static String cpp_type(Variant::Type p_storage);
	static String default_value(Variant::Type p_storage);
	static String variant_type_name(Variant::Type p_type);
	static String float_literal(double p_value);
	static String int_literal(int64_t p_value);
	static String convert_expression(const String &p_expression, Variant::Type p_from, Variant::Type p_to);
	static String take_expression(const String &p_scratch, Variant::Type p_storage);
	static String sanitize_identifier(const String &p_name);
	static bool is_direct_callable(const GritFunction &p_function);
	static String entry_declaration(const String &p_symbol);
	static String body_declaration(const GritFunction &p_function, const String &p_symbol);

	String emit_function(const GritFunction &p_function, const String &p_symbol, String &r_constants_symbol);

public:
	static String string_literal(const String &p_text);
	static bool can_emit_constant(const Variant &p_value);
	static String constant_initializer(const Variant &p_value);
	static String registration_symbol(const String &p_script_path);
	static String emit_script_file(const String &p_script_path, const LocalVector<GritFunction> &p_functions, bool p_obfuscate);
	static String emit_registry_file(const String &p_fingerprint, const Vector<String> &p_script_paths, bool p_obfuscate);
};
