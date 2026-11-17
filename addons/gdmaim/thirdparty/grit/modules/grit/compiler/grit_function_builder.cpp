#include "grit_function_builder.h"

#include "grit_cpp_emitter.h"

#include "grit/runtime/grit_signature.h"

#include "modules/gdscript/gdscript.h"

#include "core/object/method_bind.h"

#include "grit/editor/grit_piper.h"

Variant::Type GritFunctionBuilder::storage_for_type(const GDScriptDataType &p_type) {
	if (p_type.kind != GDScriptDataType::BUILTIN || !grit_is_unboxed_type(p_type.builtin_type)) {
		return Variant::NIL;
	}
	return p_type.builtin_type;
}

bool GritFunctionBuilder::has_builtin_type(const Address &p_address) {
	return p_address.type.kind == GDScriptDataType::BUILTIN;
}

bool GritFunctionBuilder::is_builtin_type(const Address &p_address, Variant::Type p_type) {
	return p_address.type.kind == GDScriptDataType::BUILTIN && p_address.type.builtin_type == p_type && p_type != Variant::NIL;
}

int GritFunctionBuilder::add_type(const GDScriptDataType &p_type) {
	return add_grit_type(GritType::from(p_type));
}

int GritFunctionBuilder::add_grit_type(const GritType &p_type) {
	if (!p_type.is_referencable()) {
		reject("type from a script without path");
	}
	for (uint32_t i = 0; i < function.types.size(); i++) {
		if (function.types[i] == p_type) {
			return i;
		}
	}
	function.types.push_back(p_type);
	return function.types.size() - 1;
}

int GritFunctionBuilder::add_block() {
	function.blocks.push_back(GritBlock());
	return function.blocks.size() - 1;
}

int GritFunctionBuilder::add_variable(GritVariable::Role p_role, const GDScriptDataType &p_type, const String &p_name) {
	GritVariable variable;
	variable.role = p_role;
	variable.storage = storage_for_type(p_type);
	variable.type = add_type(p_type);
	variable.name = p_name;
	function.variables.push_back(variable);
	return function.variables.size() - 1;
}

int GritFunctionBuilder::add_own_constant(const Variant &p_value) {
	for (uint32_t i = 0; i < constant_values.size(); i++) {
		if (constant_values[i].get_type() == p_value.get_type() && constant_values[i].identity_compare(p_value)) {
			return i;
		}
	}
	const Variant::Type type = p_value.get_type();
	const bool primitive = type == Variant::NIL || type == Variant::BOOL || type == Variant::INT || type == Variant::FLOAT;
	constant_values.push_back(p_value);
	function.constants.push_back(primitive ? p_value : Variant());
	function.constant_types.push_back(type);
	function.constant_initializers.push_back(primitive ? String() : GritCppEmitter::constant_initializer(p_value));
	return function.constants.size() - 1;
}

GritOperand GritFunctionBuilder::constant_operand(const Variant &p_value) {
	GritOperand operand;
	operand.kind = GritOperand::Kind::CONSTANT;
	operand.index = add_own_constant(p_value);
	GDScriptDataType type;
	if (p_value.get_type() != Variant::NIL) {
		type.kind = GDScriptDataType::BUILTIN;
		type.builtin_type = p_value.get_type();
	}
	operand.type = add_type(type);
	return operand;
}

int GritFunctionBuilder::append(const GritStatement &p_statement) {
	LocalVector<GritStatement> &statements = function.blocks[current_block].statements;
	statements.push_back(p_statement);
	return statements.size() - 1;
}

GritStatement &GritFunctionBuilder::statement_at(int p_block, int p_index) {
	return function.blocks[p_block].statements[p_index];
}

GritStatement GritFunctionBuilder::make_statement(GritOpcode p_opcode) const {
	GritStatement statement;
	statement.opcode = p_opcode;
	statement.line = current_line;
	return statement;
}

void GritFunctionBuilder::push_construct(Construct::Kind p_kind, int p_statement_index) {
	Construct construct;
	construct.kind = p_kind;
	construct.parent_block = current_block;
	construct.statement_index = p_statement_index;
	constructs.push_back(construct);
}

GritFunctionBuilder::Construct *GritFunctionBuilder::top_construct(Construct::Kind p_kind) {
	if (constructs.is_empty() || constructs[constructs.size() - 1].kind != p_kind) {
		reject("unbalanced control flow");
		return nullptr;
	}
	return &constructs[constructs.size() - 1];
}

bool GritFunctionBuilder::pop_construct(Construct::Kind p_kind, Construct &r_construct) {
	const Construct *construct = top_construct(p_kind);
	if (!construct) {
		return false;
	}
	r_construct = *construct;
	constructs.remove_at(constructs.size() - 1);
	current_block = r_construct.parent_block;
	return true;
}

bool GritFunctionBuilder::contains_opcode(int p_block, GritOpcode p_opcode) const {
	for (const GritStatement &statement : function.blocks[p_block].statements) {
		if (statement.opcode == p_opcode) {
			return true;
		}
		for (int block : statement.blocks) {
			if (block >= 0 && contains_opcode(block, p_opcode)) {
				return true;
			}
		}
	}
	return false;
}

int GritFunctionBuilder::open_block_statement(GritOpcode p_opcode, const GritOperand &p_condition, bool p_with_else) {
	GritStatement statement = make_statement(p_opcode);
	statement.operands.push_back(p_condition);
	statement.blocks[0] = add_block();
	if (p_with_else) {
		statement.blocks[1] = add_block();
	}
	return append(statement);
}

bool GritFunctionBuilder::resolve(const Address &p_address, GritOperand &r_operand) {
	r_operand = GritOperand();
	r_operand.type = add_type(p_address.type);
	switch (p_address.mode) {
		case Address::FUNCTION_PARAMETER:
		case Address::LOCAL_VARIABLE: {
			const int *variable = stack_variables.getptr(p_address.address);
			if (!variable) {
				reject("unknown stack address");
				return false;
			}
			r_operand.kind = GritOperand::Kind::VARIABLE;
			r_operand.index = *variable;
			return true;
		}
		case Address::TEMPORARY: {
			const TemporarySlot *slot = temporary_slots.getptr(p_address.address);
			if (!slot) {
				reject("unknown temporary");
				return false;
			}
			r_operand.kind = GritOperand::Kind::VARIABLE;
			r_operand.index = slot->variable;
			return true;
		}
		case Address::CONSTANT: {
			const int *constant = constant_indices.getptr(p_address.address);
			if (!constant) {
				reject("unknown constant");
				return false;
			}
			const Variant::Type constant_type = function.constant_types[*constant];
			const bool primitive = constant_type == Variant::NIL || constant_type == Variant::BOOL || constant_type == Variant::INT || constant_type == Variant::FLOAT;
			if (!primitive && function.constant_initializers[*constant].is_empty()) {
				reject(vformat("constant of type %s", Variant::get_type_name(constant_type)));
				return false;
			}
			r_operand.kind = GritOperand::Kind::CONSTANT;
			r_operand.index = *constant;
			return true;
		}
		case Address::NIL:
			r_operand.kind = GritOperand::Kind::NIL;
			return true;
		case Address::SELF:
			r_operand.kind = GritOperand::Kind::SELF;
			return true;
		case Address::CLASS:
			r_operand.kind = GritOperand::Kind::CLASS;
			return true;
		case Address::MEMBER:
			r_operand.kind = GritOperand::Kind::MEMBER;
			r_operand.index = p_address.address;
			return true;
	}
	reject("unknown address mode");
	return false;
}

bool GritFunctionBuilder::resolve_all(const Vector<Address> &p_addresses, LocalVector<GritOperand> &r_operands) {
	for (const Address &address : p_addresses) {
		GritOperand operand;
		if (!resolve(address, operand)) {
			return false;
		}
		r_operands.push_back(operand);
	}
	return true;
}

GritOperand GritFunctionBuilder::variable_operand(int p_variable) const {
	GritOperand operand;
	operand.kind = GritOperand::Kind::VARIABLE;
	operand.index = p_variable;
	operand.type = function.variables[p_variable].type;
	return operand;
}

void GritFunctionBuilder::append_assign(const GritOperand &p_target, const GritOperand &p_source, bool p_typed, const GDScriptDataType &p_type) {
	GritStatement statement = make_statement(p_typed ? GritOpcode::ASSIGN_TYPED : GritOpcode::ASSIGN);
	statement.target = p_target;
	statement.operands.push_back(p_source);
	if (p_typed) {
		statement.type = add_type(p_type);
	}
	append(statement);
}

void GritFunctionBuilder::append_call(GritOpcode p_opcode, const Address &p_target, const Address *p_base, const Vector<Address> &p_arguments, const StringName &p_name, bool p_validated) {
	GritStatement statement = make_statement(p_opcode);
	if (!resolve(p_target, statement.target)) {
		return;
	}
	if (p_base) {
		GritOperand base;
		if (!resolve(*p_base, base)) {
			return;
		}
		statement.operands.push_back(base);
	}
	if (!resolve_all(p_arguments, statement.operands)) {
		return;
	}
	statement.name = p_name;
	statement.validated = p_validated;
	append(statement);
}

void GritFunctionBuilder::begin(const String &p_script_path, const StringName &p_name, const GDScriptDataType &p_return_type) {
	function = GritFunction();
	constant_values.clear();
	stack_variables.clear();
	temporary_slots.clear();
	constant_indices.clear();
	used_temporaries.clear();
	temporaries_pending_clear.clear();
	constructs.clear();
	current_line = 0;
	statement_start_block = -1;
	statement_start_index = 0;
	body_started = false;
	default_argument_count = 0;
	lambda_count = 0;

	function.script_path = GritPiper::get_path(p_script_path);
	function.name = p_name;
	add_type(GDScriptDataType());
	function.return_type = add_type(p_return_type);
	current_block = add_block();
}

GritFunction GritFunctionBuilder::finish(int p_initial_line, const String &p_signature) {
	if (!constructs.is_empty()) {
		reject("unbalanced control flow");
	}
	function.initial_line = p_initial_line;
	function.signature = p_signature;
	constant_values.clear();
	return function;
}

void GritFunctionBuilder::reject(const String &p_reason) {
	if (function.unsupported_reason.is_empty()) {
		function.unsupported_reason = current_line > 0 ? vformat("%s (line %d)", p_reason, current_line) : p_reason;
	}
}

void GritFunctionBuilder::add_parameter(uint32_t p_address, const StringName &p_name, bool p_is_optional, const GDScriptDataType &p_type) {
	if (is_rejected()) {
		return;
	}
	stack_variables.insert(p_address, add_variable(GritVariable::Role::PARAMETER, p_type, p_name));
	function.parameter_count++;
	if (p_is_optional) {
		function.optional_parameter_count++;
	}
}

void GritFunctionBuilder::add_local(uint32_t p_address, const StringName &p_name, const GDScriptDataType &p_type) {
	if (is_rejected()) {
		return;
	}
	if (!body_started) {
		if (function.rest_variable >= 0) {
			reject("multiple rest parameters");
			return;
		}
		function.rest_variable = add_variable(GritVariable::Role::LOCAL, p_type, p_name);
		stack_variables.insert(p_address, function.rest_variable);
		return;
	}
	stack_variables.insert(p_address, add_variable(GritVariable::Role::LOCAL, p_type, p_name));
}

void GritFunctionBuilder::add_constant(uint32_t p_index, const Variant &p_value) {
	if (is_rejected() || constant_indices.has(p_index)) {
		return;
	}
	constant_indices.insert(p_index, add_own_constant(p_value));
}

void GritFunctionBuilder::add_temporary(uint32_t p_slot, const GDScriptDataType &p_type) {
	if (is_rejected()) {
		return;
	}
	TemporarySlot *slot = temporary_slots.getptr(p_slot);
	if (!slot) {
		TemporarySlot new_slot;
		new_slot.variable = add_variable(GritVariable::Role::TEMPORARY, p_type, String());
		new_slot.can_contain_object = p_type.can_contain_object();
		temporary_slots.insert(p_slot, new_slot);
	} else if (function.variables[slot->variable].storage != storage_for_type(p_type)) {
		reject("temporary slot reused with a different storage");
		return;
	}
	used_temporaries.push_back(p_slot);
}

void GritFunctionBuilder::pop_temporary() {
	if (is_rejected()) {
		return;
	}
	if (used_temporaries.is_empty()) {
		reject("temporary stack underflow");
		return;
	}
	const uint32_t slot = used_temporaries[used_temporaries.size() - 1];
	used_temporaries.remove_at(used_temporaries.size() - 1);
	if (temporary_slots[slot].can_contain_object && !temporaries_pending_clear.has(slot)) {
		temporaries_pending_clear.push_back(slot);
	}
}

void GritFunctionBuilder::clear_temporaries() {
	if (is_rejected()) {
		return;
	}
	for (const uint32_t slot : temporaries_pending_clear) {
		if (!temporary_slots[slot].can_contain_object) {
			continue;
		}
		GritStatement statement = make_statement(GritOpcode::CLEAR);
		statement.target = variable_operand(temporary_slots[slot].variable);
		statement.type = 0;
		append(statement);
	}
	temporaries_pending_clear.clear();
}

void GritFunctionBuilder::clear_address(const Address &p_address) {
	if (is_rejected()) {
		return;
	}
	GritStatement statement = make_statement(GritOpcode::CLEAR);
	if (!resolve(p_address, statement.target)) {
		return;
	}
	statement.type = add_type(p_address.type);
	append(statement);
}

void GritFunctionBuilder::start_parameters() {
	if (is_rejected()) {
		return;
	}
	default_argument_count = 0;
	open_default_argument();
}

void GritFunctionBuilder::open_default_argument() {
	current_block = 0;
	if (default_argument_count >= function.optional_parameter_count) {
		return;
	}
	GritStatement statement = make_statement(GritOpcode::DEFAULT_ARGUMENT);
	statement.index = function.parameter_count - function.optional_parameter_count + default_argument_count;
	statement.blocks[0] = add_block();
	append(statement);
	current_block = statement.blocks[0];
}

void GritFunctionBuilder::end_parameters() {
	current_block = 0;
}

void GritFunctionBuilder::start_block() {
	body_started = true;
}

void GritFunctionBuilder::set_line(int p_line) {
	current_line = p_line;
	statement_start_block = current_block;
	statement_start_index = current_block >= 0 ? function.blocks[current_block].statements.size() : 0;
}

void GritFunctionBuilder::write_unary_operator(const Address &p_target, Variant::Operator p_operator, const Address &p_operand) {
	if (is_rejected()) {
		return;
	}
	GritStatement statement = make_statement(GritOpcode::UNARY);
	GritOperand operand;
	if (!resolve(p_target, statement.target) || !resolve(p_operand, operand)) {
		return;
	}
	statement.operands.push_back(operand);
	statement.op = p_operator;
	statement.validated = has_builtin_type(p_operand) && Variant::get_validated_operator_evaluator(p_operator, p_operand.type.builtin_type, Variant::NIL) != nullptr;
	append(statement);
}

void GritFunctionBuilder::write_binary_operator(const Address &p_target, Variant::Operator p_operator, const Address &p_left, const Address &p_right) {
	if (is_rejected()) {
		return;
	}
	GritStatement statement = make_statement(GritOpcode::BINARY);
	GritOperand left;
	GritOperand right;
	if (!resolve(p_target, statement.target) || !resolve(p_left, left) || !resolve(p_right, right)) {
		return;
	}
	statement.operands.push_back(left);
	statement.operands.push_back(right);
	statement.op = p_operator;

	bool validated = has_builtin_type(p_left) && has_builtin_type(p_right);
	if (validated && (p_operator == Variant::OP_DIVIDE || p_operator == Variant::OP_MODULE)) {
		switch (p_left.type.builtin_type) {
			case Variant::INT:
				validated = p_right.type.builtin_type != Variant::INT && p_operator == Variant::OP_DIVIDE;
				break;
			case Variant::VECTOR2I:
			case Variant::VECTOR3I:
			case Variant::VECTOR4I:
				validated = p_right.type.builtin_type != Variant::INT && p_right.type.builtin_type != p_left.type.builtin_type;
				break;
			default:
				break;
		}
	}
	statement.validated = validated && Variant::get_validated_operator_evaluator(p_operator, p_left.type.builtin_type, p_right.type.builtin_type) != nullptr;
	append(statement);
}

void GritFunctionBuilder::write_type_test(const Address &p_target, const Address &p_source, const GDScriptDataType &p_type) {
	if (is_rejected()) {
		return;
	}
	GritStatement statement = make_statement(GritOpcode::TYPE_TEST);
	GritOperand source;
	if (!resolve(p_target, statement.target) || !resolve(p_source, source)) {
		return;
	}
	statement.operands.push_back(source);
	statement.type = add_type(p_type);
	append(statement);
}

void GritFunctionBuilder::write_and_left_operand(const Address &p_left) {
	if (is_rejected()) {
		return;
	}
	GritOperand left;
	if (!resolve(p_left, left)) {
		return;
	}
	const int index = open_block_statement(GritOpcode::IF, left, true);
	push_construct(Construct::Kind::AND, index);
	current_block = statement_at(constructs[constructs.size() - 1].parent_block, index).blocks[0];
}

void GritFunctionBuilder::write_and_right_operand(const Address &p_right) {
	if (is_rejected()) {
		return;
	}
	Construct *construct = top_construct(Construct::Kind::AND);
	if (construct) {
		resolve(p_right, construct->operand);
	}
}

void GritFunctionBuilder::write_end_and(const Address &p_target) {
	if (is_rejected()) {
		return;
	}
	const Construct *construct = top_construct(Construct::Kind::AND);
	GritOperand target;
	if (!construct || !resolve(p_target, target)) {
		return;
	}
	GritStatement test = make_statement(GritOpcode::TEST);
	test.target = target;
	test.operands.push_back(construct->operand);
	append(test);

	Construct finished;
	if (!pop_construct(Construct::Kind::AND, finished)) {
		return;
	}
	current_block = statement_at(finished.parent_block, finished.statement_index).blocks[1];
	append_assign(target, constant_operand(false), false, GDScriptDataType());
	current_block = finished.parent_block;
}

void GritFunctionBuilder::write_or_left_operand(const Address &p_left) {
	if (is_rejected()) {
		return;
	}
	GritOperand left;
	if (!resolve(p_left, left)) {
		return;
	}
	const int index = open_block_statement(GritOpcode::IF, left, true);
	push_construct(Construct::Kind::OR, index);
	current_block = statement_at(constructs[constructs.size() - 1].parent_block, index).blocks[1];
}

void GritFunctionBuilder::write_or_right_operand(const Address &p_right) {
	if (is_rejected()) {
		return;
	}
	Construct *construct = top_construct(Construct::Kind::OR);
	if (construct) {
		resolve(p_right, construct->operand);
	}
}

void GritFunctionBuilder::write_end_or(const Address &p_target) {
	if (is_rejected()) {
		return;
	}
	const Construct *construct = top_construct(Construct::Kind::OR);
	GritOperand target;
	if (!construct || !resolve(p_target, target)) {
		return;
	}
	GritStatement test = make_statement(GritOpcode::TEST);
	test.target = target;
	test.operands.push_back(construct->operand);
	append(test);

	Construct finished;
	if (!pop_construct(Construct::Kind::OR, finished)) {
		return;
	}
	current_block = statement_at(finished.parent_block, finished.statement_index).blocks[0];
	append_assign(target, constant_operand(true), false, GDScriptDataType());
	current_block = finished.parent_block;
}

void GritFunctionBuilder::write_start_ternary(const Address &p_target) {
	if (is_rejected()) {
		return;
	}
	GritOperand target;
	if (!resolve(p_target, target)) {
		return;
	}
	push_construct(Construct::Kind::TERNARY, -1);
	constructs[constructs.size() - 1].target = target;
}

void GritFunctionBuilder::write_ternary_condition(const Address &p_condition) {
	if (is_rejected()) {
		return;
	}
	GritOperand condition;
	if (!top_construct(Construct::Kind::TERNARY) || !resolve(p_condition, condition)) {
		return;
	}
	const int index = open_block_statement(GritOpcode::IF, condition, true);
	Construct *construct = top_construct(Construct::Kind::TERNARY);
	construct->parent_block = current_block;
	construct->statement_index = index;
	current_block = statement_at(current_block, index).blocks[0];
}

void GritFunctionBuilder::write_ternary_true_expr(const Address &p_expression) {
	if (is_rejected()) {
		return;
	}
	const Construct *construct = top_construct(Construct::Kind::TERNARY);
	GritOperand expression;
	if (!construct || construct->statement_index < 0 || !resolve(p_expression, expression)) {
		return;
	}
	append_assign(construct->target, expression, false, GDScriptDataType());
	current_block = statement_at(construct->parent_block, construct->statement_index).blocks[1];
}

void GritFunctionBuilder::write_ternary_false_expr(const Address &p_expression) {
	if (is_rejected()) {
		return;
	}
	const Construct *construct = top_construct(Construct::Kind::TERNARY);
	GritOperand expression;
	if (!construct || !resolve(p_expression, expression)) {
		return;
	}
	append_assign(construct->target, expression, false, GDScriptDataType());
}

void GritFunctionBuilder::write_end_ternary() {
	if (is_rejected()) {
		return;
	}
	Construct construct;
	pop_construct(Construct::Kind::TERNARY, construct);
}

void GritFunctionBuilder::write_set(const Address &p_base, const Address &p_index, const Address &p_source) {
	if (is_rejected()) {
		return;
	}
	GritStatement statement = make_statement(GritOpcode::SET_KEYED);
	GritOperand base;
	GritOperand index;
	GritOperand source;
	if (!resolve(p_base, base) || !resolve(p_index, index) || !resolve(p_source, source)) {
		return;
	}
	statement.operands.push_back(base);
	statement.operands.push_back(index);
	statement.operands.push_back(source);
	if (has_builtin_type(p_base)) {
		const Variant::Type base_type = p_base.type.builtin_type;
		if (is_builtin_type(p_index, Variant::INT) && Variant::get_member_validated_indexed_setter(base_type) && is_builtin_type(p_source, Variant::get_indexed_element_type(base_type))) {
			statement.index = 1;
		} else if (Variant::get_member_validated_keyed_setter(base_type)) {
			statement.index = 2;
		}
	}
	append(statement);
}

void GritFunctionBuilder::write_get(const Address &p_target, const Address &p_index, const Address &p_source) {
	if (is_rejected()) {
		return;
	}
	GritStatement statement = make_statement(GritOpcode::GET_KEYED);
	GritOperand source;
	GritOperand index;
	if (!resolve(p_target, statement.target) || !resolve(p_source, source) || !resolve(p_index, index)) {
		return;
	}
	statement.operands.push_back(source);
	statement.operands.push_back(index);
	if (has_builtin_type(p_source)) {
		const Variant::Type source_type = p_source.type.builtin_type;
		if (is_builtin_type(p_index, Variant::INT) && Variant::get_member_validated_indexed_getter(source_type)) {
			statement.index = 1;
		} else if (Variant::get_member_validated_keyed_getter(source_type)) {
			statement.index = 2;
		}
	}
	append(statement);
}

void GritFunctionBuilder::write_set_named(const Address &p_base, const StringName &p_name, const Address &p_source) {
	if (is_rejected()) {
		return;
	}
	GritStatement statement = make_statement(GritOpcode::SET_NAMED);
	GritOperand base;
	GritOperand source;
	if (!resolve(p_base, base) || !resolve(p_source, source)) {
		return;
	}
	statement.operands.push_back(base);
	statement.operands.push_back(source);
	statement.name = p_name;
	statement.validated = has_builtin_type(p_base) && Variant::get_member_validated_setter(p_base.type.builtin_type, p_name) &&
			is_builtin_type(p_source, Variant::get_member_type(p_base.type.builtin_type, p_name));
	append(statement);
}

void GritFunctionBuilder::write_get_named(const Address &p_target, const StringName &p_name, const Address &p_source) {
	if (is_rejected()) {
		return;
	}
	GritStatement statement = make_statement(GritOpcode::GET_NAMED);
	GritOperand source;
	if (!resolve(p_target, statement.target) || !resolve(p_source, source)) {
		return;
	}
	statement.operands.push_back(source);
	statement.name = p_name;
	statement.validated = has_builtin_type(p_source) && Variant::get_member_validated_getter(p_source.type.builtin_type, p_name);
	append(statement);
}

void GritFunctionBuilder::write_set_member(const Address &p_value, const StringName &p_name) {
	if (is_rejected()) {
		return;
	}
	GritStatement statement = make_statement(GritOpcode::SET_PROPERTY);
	GritOperand value;
	if (!resolve(p_value, value)) {
		return;
	}
	statement.operands.push_back(value);
	statement.name = p_name;
	append(statement);
}

void GritFunctionBuilder::write_get_member(const Address &p_target, const StringName &p_name) {
	if (is_rejected()) {
		return;
	}
	GritStatement statement = make_statement(GritOpcode::GET_PROPERTY);
	if (!resolve(p_target, statement.target)) {
		return;
	}
	statement.name = p_name;
	append(statement);
}

void GritFunctionBuilder::write_set_static_variable(const Address &p_value, const Address &p_class, int p_index) {
	if (is_rejected()) {
		return;
	}
	GritStatement statement = make_statement(GritOpcode::SET_STATIC);
	GritOperand value;
	GritOperand class_operand;
	if (!resolve(p_value, value) || !resolve(p_class, class_operand)) {
		return;
	}
	statement.operands.push_back(value);
	statement.operands.push_back(class_operand);
	statement.index = p_index;
	append(statement);
}

void GritFunctionBuilder::write_get_static_variable(const Address &p_target, const Address &p_class, int p_index) {
	if (is_rejected()) {
		return;
	}
	GritStatement statement = make_statement(GritOpcode::GET_STATIC);
	GritOperand class_operand;
	if (!resolve(p_target, statement.target) || !resolve(p_class, class_operand)) {
		return;
	}
	statement.operands.push_back(class_operand);
	statement.index = p_index;
	append(statement);
}

void GritFunctionBuilder::write_assign(const Address &p_target, const Address &p_source) {
	if (is_rejected()) {
		return;
	}
	GritOperand target;
	GritOperand source;
	if (!resolve(p_target, target) || !resolve(p_source, source)) {
		return;
	}
	const GDScriptDataType &type = p_target.type;
	const bool typed_container = type.kind == GDScriptDataType::BUILTIN &&
			((type.builtin_type == Variant::ARRAY && type.has_container_element_type(0)) || (type.builtin_type == Variant::DICTIONARY && type.has_container_element_types()));
	const bool builtin_conversion = type.kind == GDScriptDataType::BUILTIN && p_source.type.kind == GDScriptDataType::BUILTIN && type.builtin_type != p_source.type.builtin_type;
	append_assign(target, source, typed_container || builtin_conversion, type);
}

void GritFunctionBuilder::write_assign_with_conversion(const Address &p_target, const Address &p_source) {
	if (is_rejected()) {
		return;
	}
	GritOperand target;
	GritOperand source;
	if (!resolve(p_target, target) || !resolve(p_source, source)) {
		return;
	}
	append_assign(target, source, p_target.type.kind != GDScriptDataType::VARIANT, p_target.type);
}

void GritFunctionBuilder::write_assign_constant(const Address &p_target, const Variant &p_value) {
	if (is_rejected()) {
		return;
	}
	GritOperand target;
	if (!resolve(p_target, target)) {
		return;
	}
	append_assign(target, constant_operand(p_value), false, GDScriptDataType());
}

void GritFunctionBuilder::write_assign_default_parameter(const Address &p_target, const Address &p_source, bool p_use_conversion) {
	if (is_rejected()) {
		return;
	}
	if (p_use_conversion) {
		write_assign_with_conversion(p_target, p_source);
	} else {
		write_assign(p_target, p_source);
	}
	default_argument_count++;
	open_default_argument();
}

void GritFunctionBuilder::write_store_global(const Address &p_target, int p_global_index) {
	if (is_rejected()) {
		return;
	}
	for (const KeyValue<StringName, int> &global : GDScriptLanguage::get_singleton()->get_global_map()) {
		if (global.value == p_global_index) {
			write_store_named_global(p_target, global.key);
			return;
		}
	}
	reject("unknown global");
}

void GritFunctionBuilder::write_store_named_global(const Address &p_target, const StringName &p_global) {
	if (is_rejected()) {
		return;
	}
	GritStatement statement = make_statement(GritOpcode::GET_GLOBAL);
	if (!resolve(p_target, statement.target)) {
		return;
	}
	statement.name = p_global;
	append(statement);
}

void GritFunctionBuilder::write_cast(const Address &p_target, const Address &p_source, const GDScriptDataType &p_type) {
	if (is_rejected()) {
		return;
	}
	GritStatement statement = make_statement(GritOpcode::CAST);
	GritOperand source;
	if (!resolve(p_target, statement.target) || !resolve(p_source, source)) {
		return;
	}
	statement.operands.push_back(source);
	statement.type = add_type(p_type);
	append(statement);
}

void GritFunctionBuilder::write_call(const Address &p_target, const Address &p_base, const StringName &p_function_name, const Vector<Address> &p_arguments) {
	if (is_rejected()) {
		return;
	}
	append_call(GritOpcode::CALL, p_target, &p_base, p_arguments, p_function_name, false);
}

void GritFunctionBuilder::write_call_async(const Address &p_target, const Address &p_base, const StringName &p_function_name, const Vector<Address> &p_arguments) {
	if (is_rejected()) {
		return;
	}
	append_call(GritOpcode::CALL_ASYNC, p_target, &p_base, p_arguments, p_function_name, false);
}

void GritFunctionBuilder::write_lambda(const Address &p_target, const Vector<Address> &p_captures, bool p_use_self) {
	if (is_rejected()) {
		return;
	}
	GritStatement statement = make_statement(GritOpcode::CREATE_LAMBDA);
	if (!resolve(p_target, statement.target) || !resolve_all(p_captures, statement.operands)) {
		return;
	}
	statement.index = lambda_count++;
	statement.validated = p_use_self;
	append(statement);
}

void GritFunctionBuilder::write_await(const Address &p_target, const Address &p_operand) {
	if (is_rejected()) {
		return;
	}
	GritStatement statement = make_statement(GritOpcode::AWAIT);
	GritOperand operand;
	if (!resolve(p_target, statement.target) || !resolve(p_operand, operand)) {
		return;
	}
	statement.operands.push_back(operand);
	statement.index = function.await_count++;
	append(statement);
}

void GritFunctionBuilder::write_super_call(const Address &p_target, const StringName &p_function_name, const Vector<Address> &p_arguments) {
	if (is_rejected()) {
		return;
	}
	append_call(GritOpcode::CALL_SUPER, p_target, nullptr, p_arguments, p_function_name, false);
}

void GritFunctionBuilder::write_call_utility(const Address &p_target, const StringName &p_function, const Vector<Address> &p_arguments) {
	if (is_rejected()) {
		return;
	}
	bool validated = true;
	if (Variant::is_utility_function_vararg(p_function)) {
		validated = false;
	} else if (p_arguments.size() == Variant::get_utility_function_argument_count(p_function)) {
		for (int i = 0; i < p_arguments.size(); i++) {
			if (!is_builtin_type(p_arguments[i], Variant::get_utility_function_argument_type(p_function, i))) {
				validated = false;
				break;
			}
		}
	}
	append_call(GritOpcode::CALL_UTILITY, p_target, nullptr, p_arguments, p_function, validated);
}

void GritFunctionBuilder::write_call_gdscript_utility(const Address &p_target, const StringName &p_function, const Vector<Address> &p_arguments) {
	if (is_rejected()) {
		return;
	}
	append_call(GritOpcode::CALL_GDSCRIPT_UTILITY, p_target, nullptr, p_arguments, p_function, false);
}

void GritFunctionBuilder::write_call_builtin_type(const Address &p_target, const Address *p_base, Variant::Type p_type, const StringName &p_method, const Vector<Address> &p_arguments) {
	if (is_rejected()) {
		return;
	}
	bool validated = false;
	if (!Variant::is_builtin_method_vararg(p_type, p_method) && p_arguments.size() == Variant::get_builtin_method_argument_count(p_type, p_method)) {
		validated = true;
		for (int i = 0; i < p_arguments.size(); i++) {
			if (!is_builtin_type(p_arguments[i], Variant::get_builtin_method_argument_type(p_type, p_method, i))) {
				validated = false;
				break;
			}
		}
	}
	if (!validated && p_base) {
		write_call(p_target, *p_base, p_method, p_arguments);
		return;
	}
	const int statement_count = function.blocks[current_block].statements.size();
	append_call(p_base ? GritOpcode::CALL_BUILTIN : GritOpcode::CALL_BUILTIN_STATIC, p_target, p_base, p_arguments, p_method, validated);
	if (!is_rejected() && int(function.blocks[current_block].statements.size()) > statement_count) {
		function.blocks[current_block].statements[statement_count].builtin_type = p_type;
	}
}

void GritFunctionBuilder::write_call_native_static(const Address &p_target, const StringName &p_class, const StringName &p_method, const Vector<Address> &p_arguments, bool p_validated) {
	if (is_rejected()) {
		return;
	}
	const int statement_count = function.blocks[current_block].statements.size();
	append_call(GritOpcode::CALL_NATIVE_STATIC, p_target, nullptr, p_arguments, p_method, p_validated);
	if (!is_rejected() && int(function.blocks[current_block].statements.size()) > statement_count) {
		function.blocks[current_block].statements[statement_count].class_name = p_class;
	}
}

void GritFunctionBuilder::write_call_method_bind(const Address &p_target, const Address &p_base, const MethodBind *p_method, const Vector<Address> &p_arguments, bool p_validated) {
	if (is_rejected()) {
		return;
	}
	const int statement_count = function.blocks[current_block].statements.size();
	append_call(GritOpcode::CALL_METHOD_BIND, p_target, &p_base, p_arguments, p_method->get_name(), p_validated);
	if (!is_rejected() && int(function.blocks[current_block].statements.size()) > statement_count) {
		function.blocks[current_block].statements[statement_count].class_name = p_method->get_instance_class();
	}
}

void GritFunctionBuilder::write_construct(const Address &p_target, Variant::Type p_type, const Vector<Address> &p_arguments) {
	if (is_rejected()) {
		return;
	}
	GritStatement statement = make_statement(GritOpcode::CONSTRUCT);
	if (!resolve(p_target, statement.target) || !resolve_all(p_arguments, statement.operands)) {
		return;
	}
	statement.builtin_type = p_type;

	bool all_have_type = true;
	for (const Address &argument : p_arguments) {
		if (!has_builtin_type(argument)) {
			all_have_type = false;
			break;
		}
	}
	if (all_have_type) {
		for (int i = 0; i < Variant::get_constructor_count(p_type); i++) {
			if (Variant::get_constructor_argument_count(p_type, i) != p_arguments.size()) {
				continue;
			}
			bool types_correct = true;
			for (int j = 0; j < p_arguments.size(); j++) {
				if (p_arguments[j].type.builtin_type != Variant::get_constructor_argument_type(p_type, i, j)) {
					types_correct = false;
					break;
				}
			}
			if (types_correct) {
				statement.index = i;
				statement.validated = true;
				break;
			}
		}
	}
	append(statement);
}

void GritFunctionBuilder::write_construct_array(const Address &p_target, const GDScriptDataType *p_element_type, const Vector<Address> &p_arguments) {
	if (is_rejected()) {
		return;
	}
	GritStatement statement = make_statement(GritOpcode::CONSTRUCT_ARRAY);
	if (!resolve(p_target, statement.target) || !resolve_all(p_arguments, statement.operands)) {
		return;
	}
	if (p_element_type) {
		statement.validated = true;
		statement.type = add_type(*p_element_type);
	}
	append(statement);
}

void GritFunctionBuilder::write_construct_dictionary(const Address &p_target, const GDScriptDataType *p_key_type, const GDScriptDataType *p_value_type, const Vector<Address> &p_arguments) {
	if (is_rejected()) {
		return;
	}
	GritStatement statement = make_statement(GritOpcode::CONSTRUCT_DICTIONARY);
	if (!resolve(p_target, statement.target) || !resolve_all(p_arguments, statement.operands)) {
		return;
	}
	if (p_key_type && p_value_type) {
		statement.validated = true;
		statement.type = add_type(*p_key_type);
		statement.secondary_type = add_type(*p_value_type);
	}
	append(statement);
}

void GritFunctionBuilder::write_if(const Address &p_condition) {
	if (is_rejected()) {
		return;
	}
	GritOperand condition;
	if (!resolve(p_condition, condition)) {
		return;
	}
	const int index = open_block_statement(GritOpcode::IF, condition, false);
	push_construct(Construct::Kind::IF, index);
	current_block = statement_at(current_block, index).blocks[0];
}

void GritFunctionBuilder::write_else() {
	if (is_rejected()) {
		return;
	}
	const Construct *construct = top_construct(Construct::Kind::IF);
	if (!construct) {
		return;
	}
	const int else_block = add_block();
	statement_at(construct->parent_block, construct->statement_index).blocks[1] = else_block;
	current_block = else_block;
}

void GritFunctionBuilder::write_endif() {
	if (is_rejected()) {
		return;
	}
	Construct construct;
	pop_construct(Construct::Kind::IF, construct);
}

void GritFunctionBuilder::write_jump_if_shared(const Address &p_value) {
	if (is_rejected()) {
		return;
	}
	GritOperand value;
	if (!resolve(p_value, value)) {
		return;
	}
	const int index = open_block_statement(GritOpcode::IF_NOT_SHARED, value, false);
	push_construct(Construct::Kind::SHARED, index);
	current_block = statement_at(current_block, index).blocks[0];
}

void GritFunctionBuilder::write_end_jump_if_shared() {
	if (is_rejected()) {
		return;
	}
	Construct construct;
	pop_construct(Construct::Kind::SHARED, construct);
}

void GritFunctionBuilder::start_while_condition() {
	if (is_rejected()) {
		return;
	}
	GritStatement statement = make_statement(GritOpcode::WHILE);
	statement.blocks[0] = add_block();
	const int index = append(statement);
	push_construct(Construct::Kind::WHILE, index);
	current_block = statement_at(current_block, index).blocks[0];
}

void GritFunctionBuilder::write_while(const Address &p_condition) {
	if (is_rejected()) {
		return;
	}
	const Construct *construct = top_construct(Construct::Kind::WHILE);
	GritOperand condition;
	if (!construct || !resolve(p_condition, condition)) {
		return;
	}
	const int body_block = add_block();
	GritStatement &statement = statement_at(construct->parent_block, construct->statement_index);
	statement.operands.push_back(condition);
	statement.blocks[1] = body_block;
	current_block = body_block;
}

void GritFunctionBuilder::write_endwhile() {
	if (is_rejected()) {
		return;
	}
	Construct construct;
	pop_construct(Construct::Kind::WHILE, construct);
}

void GritFunctionBuilder::start_for(const GDScriptDataType &p_iterator_type, const GDScriptDataType &p_list_type, bool p_is_range) {
	if (is_rejected()) {
		return;
	}
	push_construct(Construct::Kind::FOR, -1);
	Construct &construct = constructs[constructs.size() - 1];
	construct.is_range = p_is_range;
	if (p_is_range) {
		GDScriptDataType int_type;
		int_type.kind = GDScriptDataType::BUILTIN;
		int_type.builtin_type = Variant::INT;
		construct.range_variables[0] = add_variable(GritVariable::Role::LOCAL, int_type, "range_from");
		construct.range_variables[1] = add_variable(GritVariable::Role::LOCAL, int_type, "range_to");
		construct.range_variables[2] = add_variable(GritVariable::Role::LOCAL, int_type, "range_step");
	} else {
		construct.container_variable = add_variable(GritVariable::Role::LOCAL, p_list_type, "container");
		construct.range_variables[0] = add_variable(GritVariable::Role::LOCAL, GDScriptDataType(), "counter");
	}
}

void GritFunctionBuilder::write_for_list_assignment(const Address &p_list) {
	if (is_rejected()) {
		return;
	}
	const Construct *construct = top_construct(Construct::Kind::FOR);
	GritOperand list;
	if (!construct || construct->is_range || !resolve(p_list, list)) {
		return;
	}
	append_assign(variable_operand(construct->container_variable), list, false, GDScriptDataType());
}

void GritFunctionBuilder::write_for_range_assignment(const Address &p_from, const Address &p_to, const Address &p_step) {
	if (is_rejected()) {
		return;
	}
	const Construct *construct = top_construct(Construct::Kind::FOR);
	if (!construct || !construct->is_range) {
		return;
	}
	GDScriptDataType int_type;
	int_type.kind = GDScriptDataType::BUILTIN;
	int_type.builtin_type = Variant::INT;
	const Address *arguments[3] = { &p_from, &p_to, &p_step };
	for (int i = 0; i < 3; i++) {
		GritOperand operand;
		if (!resolve(*arguments[i], operand)) {
			return;
		}
		append_assign(variable_operand(construct->range_variables[i]), operand, !(int_type == arguments[i]->type), int_type);
	}
}

void GritFunctionBuilder::write_for(const Address &p_variable, bool p_use_conversion, bool p_is_range) {
	if (is_rejected()) {
		return;
	}
	Construct *construct = top_construct(Construct::Kind::FOR);
	GritOperand variable;
	if (!construct || construct->is_range != p_is_range || !resolve(p_variable, variable)) {
		return;
	}

	GritOperand iterator = variable;
	if (p_use_conversion) {
		iterator = variable_operand(add_variable(GritVariable::Role::LOCAL, GDScriptDataType(), "iterator"));
	}

	GritStatement statement = make_statement(p_is_range ? GritOpcode::FOR_RANGE : GritOpcode::FOR_EACH);
	statement.target = iterator;
	if (p_is_range) {
		for (int i = 0; i < 3; i++) {
			statement.operands.push_back(variable_operand(construct->range_variables[i]));
		}
	} else {
		statement.operands.push_back(variable_operand(construct->container_variable));
		statement.operands.push_back(variable_operand(construct->range_variables[0]));
	}
	statement.blocks[0] = add_block();
	construct->statement_index = append(statement);
	current_block = statement.blocks[0];

	if (p_use_conversion) {
		append_assign(variable, iterator, p_variable.type.kind != GDScriptDataType::VARIANT, p_variable.type);
		if (p_variable.type.can_contain_object()) {
			GritStatement clear = make_statement(GritOpcode::CLEAR);
			clear.target = iterator;
			append(clear);
		}
	}
}

void GritFunctionBuilder::write_endfor() {
	if (is_rejected()) {
		return;
	}
	Construct construct;
	pop_construct(Construct::Kind::FOR, construct);
}

void GritFunctionBuilder::write_break() {
	if (is_rejected()) {
		return;
	}
	append(make_statement(GritOpcode::BREAK));
}

void GritFunctionBuilder::write_continue() {
	if (is_rejected()) {
		return;
	}
	append(make_statement(GritOpcode::CONTINUE));
}

void GritFunctionBuilder::write_return(const Address &p_value, bool p_use_conversion) {
	if (is_rejected()) {
		return;
	}
	GritStatement statement = make_statement(GritOpcode::RETURN);
	GritOperand value;
	if (!resolve(p_value, value)) {
		return;
	}
	statement.operands.push_back(value);
	statement.validated = p_use_conversion;
	append(statement);
}

void GritFunctionBuilder::write_assert(const Address &p_test, const Address &p_message) {
	if (is_rejected()) {
		return;
	}
	GritOperand test;
	GritOperand message;
	if (!resolve(p_test, test) || !resolve(p_message, message)) {
		return;
	}
	if (statement_start_block != current_block) {
		reject("assert spanning blocks");
		return;
	}

	const int assert_block = add_block();
	LocalVector<GritStatement> &statements = function.blocks[current_block].statements;
	for (uint32_t i = statement_start_index; i < statements.size(); i++) {
		function.blocks[assert_block].statements.push_back(statements[i]);
	}
	statements.resize(statement_start_index);
	if (contains_opcode(assert_block, GritOpcode::CREATE_LAMBDA)) {
		reject("lambda in assert");
		return;
	}
	if (contains_opcode(assert_block, GritOpcode::AWAIT)) {
		reject("await in assert");
		return;
	}

	GritStatement statement = make_statement(GritOpcode::ASSERT);
	statement.operands.push_back(test);
	statement.operands.push_back(message);
	statement.validated = p_message.mode != Address::NIL;
	statement.blocks[0] = assert_block;
	append(statement);
}
