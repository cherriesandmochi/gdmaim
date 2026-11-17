#include "grit_cpp_emitter.h"

#include "grit_inline_catalog.h"

#include "grit/runtime/grit_registry.h"
#include "grit/runtime/grit_runtime.h"

#include "core/object/class_db.h"
#include "core/object/method_bind.h"

#include "grit/editor/grit_piper.h"

#include <cstdio>

static bool is_trivial_type(Variant::Type p_type) {
	switch (p_type) {
		case Variant::BOOL:
		case Variant::INT:
		case Variant::FLOAT:
		case Variant::VECTOR2:
		case Variant::VECTOR2I:
		case Variant::RECT2:
		case Variant::RECT2I:
		case Variant::VECTOR3:
		case Variant::VECTOR3I:
		case Variant::TRANSFORM2D:
		case Variant::VECTOR4:
		case Variant::VECTOR4I:
		case Variant::PLANE:
		case Variant::QUATERNION:
		case Variant::AABB:
		case Variant::BASIS:
		case Variant::TRANSFORM3D:
		case Variant::PROJECTION:
		case Variant::COLOR:
		case Variant::RID:
			return true;
		default:
			return false;
	}
}

void GritCppEmitter::write_line(const String &p_line) {
	for (int i = 0; i < indentation; i++) {
		body.append("\t");
	}
	body.append(p_line);
	body.append("\n");
}

void GritCppEmitter::open_scope(const String &p_header) {
	write_line(p_header.is_empty() ? String("{") : p_header + " {");
	indentation++;
}

void GritCppEmitter::close_scope(const String &p_footer) {
	indentation--;
	write_line(p_footer);
}

void GritCppEmitter::write_scoped(const Scope &p_scope, const LocalVector<String> &p_core) {
	const bool wrap = is_coroutine || !p_scope.prelude.is_empty() || !p_scope.postlude.is_empty();
	if (wrap) {
		open_scope(String());
	}
	for (const String &line : p_scope.prelude) {
		write_line(line);
	}
	for (const String &line : p_core) {
		write_line(line);
	}
	for (const String &line : p_scope.postlude) {
		write_line(line);
	}
	if (wrap) {
		close_scope();
	}
}

void GritCppEmitter::nest_block(const Scope &p_scope, const LocalVector<String> &p_core, LocalVector<String> &r_core) {
	r_core.push_back("\t{");
	for (const String &statement : p_scope.prelude) {
		r_core.push_back("\t\t" + statement);
	}
	for (const String &statement : p_core) {
		r_core.push_back("\t\t" + statement);
	}
	for (const String &statement : p_scope.postlude) {
		r_core.push_back("\t\t" + statement);
	}
	r_core.push_back("\t}");
}

String GritCppEmitter::next_symbol(const String &p_prefix) {
	return p_prefix + "_" + itos(symbol_count++);
}

String GritCppEmitter::add_static(const String &p_type, const String &p_prefix, const String &p_initializer) {
	const String key = p_type + "=" + p_initializer;
	const String *existing = static_symbols.getptr(key);
	if (existing) {
		return *existing;
	}
	const String symbol = next_symbol(p_prefix);
	statics.push_back("static " + p_type + " " + symbol + " = " + p_initializer + ";");
	static_symbols.insert(key, symbol);
	return symbol;
}

int GritCppEmitter::line_of(const GritStatement &p_statement) const {
	return p_statement.line > 0 ? p_statement.line : function->initial_line;
}

String GritCppEmitter::error_return() const {
	return "return GritRuntime::error_return(frame);";
}

String GritCppEmitter::checked(const String &p_call) const {
	return "if (unlikely(!" + p_call + ")) { " + error_return() + " }";
}

String GritCppEmitter::variable_name(int p_variable) const {
	return variable_name_in(*function, p_variable);
}

String GritCppEmitter::variable_name_in(const GritFunction &p_function, int p_variable) {
	const GritVariable &variable = p_function.variables[p_variable];
	String name;
	switch (variable.role) {
		case GritVariable::Role::PARAMETER:
			name = "parameter_";
			break;
		case GritVariable::Role::LOCAL:
			name = "local_";
			break;
		case GritVariable::Role::TEMPORARY:
			name = "temporary_";
			break;
	}
	name += itos(p_variable);
	const String suffix = sanitize_identifier(variable.name);
	if (!suffix.is_empty()) {
		name += "_" + suffix;
	}
	return name;
}

Variant::Type GritCppEmitter::storage_of(const GritOperand &p_operand) const {
	switch (p_operand.kind) {
		case GritOperand::Kind::VARIABLE:
			return function->variables[p_operand.index].storage;
		case GritOperand::Kind::CONSTANT: {
			const Variant::Type type = function->constant_types[p_operand.index];
			return is_numeric(type) ? type : Variant::NIL;
		}
		default:
			return Variant::NIL;
	}
}

bool GritCppEmitter::is_native(const GritOperand &p_operand) const {
	return storage_of(p_operand) != Variant::NIL;
}

const GritType &GritCppEmitter::type_of(int p_type) const {
	return function->types[p_type];
}

Variant::Type GritCppEmitter::static_type_of(const GritOperand &p_operand) const {
	const GritType &type = type_of(p_operand.type);
	return type.kind == GritType::Kind::BUILTIN ? type.builtin_type : Variant::NIL;
}

Variant::Type GritCppEmitter::value_type_of(const GritOperand &p_operand) const {
	if (p_operand.kind == GritOperand::Kind::CONSTANT) {
		return function->constant_types[p_operand.index];
	}
	const Variant::Type storage = storage_of(p_operand);
	return storage != Variant::NIL ? storage : static_type_of(p_operand);
}

StringName GritCppEmitter::native_class_of(const GritOperand &p_operand) const {
	switch (p_operand.kind) {
		case GritOperand::Kind::SELF:
			return function->native_base;
		case GritOperand::Kind::VARIABLE:
		case GritOperand::Kind::MEMBER: {
			const GritType &type = type_of(p_operand.type);
			if (type.kind == GritType::Kind::NATIVE || type.kind == GritType::Kind::SCRIPT) {
				return type.native_type;
			}
		} break;
		default:
			break;
	}
	return StringName();
}

String GritCppEmitter::native_expression(const GritOperand &p_operand) const {
	if (p_operand.kind == GritOperand::Kind::VARIABLE) {
		return variable_name(p_operand.index);
	}
	const Variant &value = function->constants[p_operand.index];
	switch (value.get_type()) {
		case Variant::BOOL:
			return bool(value) ? "true" : "false";
		case Variant::INT:
			return int_literal(value);
		default:
			return float_literal(value);
	}
}

String GritCppEmitter::converted(const GritOperand &p_operand, Variant::Type p_storage) const {
	return convert_expression(native_expression(p_operand), storage_of(p_operand), p_storage);
}

bool GritCppEmitter::is_lvalue(const GritOperand &p_operand) const {
	return p_operand.kind == GritOperand::Kind::VARIABLE || p_operand.kind == GritOperand::Kind::MEMBER;
}

String GritCppEmitter::lvalue(const GritOperand &p_operand) const {
	if (p_operand.kind == GritOperand::Kind::MEMBER) {
		return "members[" + itos(p_operand.index) + "]";
	}
	return variable_name(p_operand.index);
}

String GritCppEmitter::truth_value(const GritOperand &p_operand, Scope &r_scope) {
	switch (storage_of(p_operand)) {
		case Variant::NIL:
			return variant_value(p_operand, r_scope) + ".booleanize()";
		case Variant::BOOL:
			return native_expression(p_operand);
		case Variant::INT:
			return "(" + native_expression(p_operand) + " != 0)";
		case Variant::FLOAT:
			return "(" + native_expression(p_operand) + " != 0.0)";
		default:
			return "GritRuntime::truth(" + native_expression(p_operand) + ")";
	}
}

String GritCppEmitter::variant_value(const GritOperand &p_operand, Scope &r_scope) {
	switch (p_operand.kind) {
		case GritOperand::Kind::VARIABLE:
			if (!is_native(p_operand)) {
				return variable_name(p_operand.index);
			}
			break;
		case GritOperand::Kind::CONSTANT:
			if (!is_native(p_operand) && function->constant_types[p_operand.index] != Variant::NIL) {
				return constant_slot(p_operand.index);
			}
			break;
		case GritOperand::Kind::SELF:
			return "self_variant";
		case GritOperand::Kind::CLASS:
			return "class_variant";
		case GritOperand::Kind::MEMBER:
			return lvalue(p_operand);
		case GritOperand::Kind::NIL:
			break;
	}
	const String boxed = next_symbol("boxed");
	if (is_native(p_operand)) {
		r_scope.prelude.push_back("const Variant " + boxed + "(" + native_expression(p_operand) + ");");
	} else {
		r_scope.prelude.push_back("const Variant " + boxed + ";");
	}
	return boxed;
}

String GritCppEmitter::variant_base(const GritOperand &p_operand, Scope &r_scope) {
	switch (p_operand.kind) {
		case GritOperand::Kind::VARIABLE:
			if (!is_native(p_operand)) {
				return variable_name(p_operand.index);
			}
			break;
		case GritOperand::Kind::SELF:
		case GritOperand::Kind::CLASS:
		case GritOperand::Kind::MEMBER:
			return variant_value(p_operand, r_scope);
		default:
			break;
	}
	const String base = next_symbol("base");
	if (is_native(p_operand)) {
		r_scope.prelude.push_back("Variant " + base + "(" + native_expression(p_operand) + ");");
		if (p_operand.kind == GritOperand::Kind::VARIABLE) {
			r_scope.postlude.push_back(variable_name(p_operand.index) + " = " + take_expression(base, storage_of(p_operand)) + ";");
		}
	} else if (p_operand.kind == GritOperand::Kind::CONSTANT) {
		r_scope.prelude.push_back("Variant " + base + " = " + variant_value(p_operand, r_scope) + ";");
	} else {
		r_scope.prelude.push_back("Variant " + base + ";");
	}
	return base;
}

String GritCppEmitter::variant_target(const GritOperand &p_target, Scope &r_scope) {
	if (p_target.kind == GritOperand::Kind::VARIABLE && !is_native(p_target)) {
		return variable_name(p_target.index);
	}
	if (p_target.kind == GritOperand::Kind::MEMBER) {
		return lvalue(p_target);
	}
	const String result = next_symbol("result");
	r_scope.prelude.push_back("Variant " + result + ";");
	if (p_target.kind == GritOperand::Kind::VARIABLE) {
		r_scope.postlude.push_back(variable_name(p_target.index) + " = " + take_expression(result, storage_of(p_target)) + ";");
	}
	return result;
}

String GritCppEmitter::take_expression(const String &p_scratch, Variant::Type p_storage) {
	return "GritRuntime::unbox_take<" + cpp_type(p_storage) + ">(" + p_scratch + ", " + variant_type_name(p_storage) + ")";
}

String GritCppEmitter::pointer_input(const GritOperand &p_operand, Variant::Type p_type, Scope &r_scope) {
	if (p_type == Variant::NIL) {
		return "&" + variant_value(p_operand, r_scope);
	}
	const Variant::Type storage = storage_of(p_operand);
	if (storage == p_type && p_operand.kind == GritOperand::Kind::VARIABLE) {
		return "&" + variable_name(p_operand.index);
	}
	const String input = next_symbol("input");
	if (storage != Variant::NIL) {
		r_scope.prelude.push_back("const " + cpp_type(p_type) + " " + input + " = " + converted(p_operand, p_type) + ";");
		return "&" + input;
	}
	const String value = variant_value(p_operand, r_scope);
	if (p_operand.kind != GritOperand::Kind::NIL && static_type_of(p_operand) == p_type) {
		return "GritRuntime::internal_pointer<" + cpp_type(p_type) + ">(" + value + ")";
	}
	r_scope.prelude.push_back("const " + cpp_type(p_type) + " " + input + " = " + convert_expression(value, Variant::NIL, p_type) + ";");
	return "&" + input;
}

String GritCppEmitter::pointer_base(const GritOperand &p_operand, Variant::Type p_type, Scope &r_scope) {
	const Variant::Type storage = storage_of(p_operand);
	if (storage == p_type && p_operand.kind == GritOperand::Kind::VARIABLE) {
		return "&" + variable_name(p_operand.index);
	}
	if (is_lvalue(p_operand) && storage == Variant::NIL && static_type_of(p_operand) == p_type) {
		return "GritRuntime::internal_pointer<" + cpp_type(p_type) + ">(" + lvalue(p_operand) + ")";
	}
	const String base = next_symbol("base");
	const String value = storage != Variant::NIL ? converted(p_operand, p_type) : convert_expression(variant_value(p_operand, r_scope), Variant::NIL, p_type);
	r_scope.prelude.push_back(cpp_type(p_type) + " " + base + " = " + value + ";");
	if (is_lvalue(p_operand)) {
		r_scope.postlude.push_back(lvalue(p_operand) + " = " + convert_expression(base, p_type, storage) + ";");
	}
	return "&" + base;
}

String GritCppEmitter::pointer_output(const GritOperand &p_target, Variant::Type p_type, Scope &r_scope) {
	if (p_type == Variant::NIL) {
		return "&" + variant_target(p_target, r_scope);
	}
	const Variant::Type storage = storage_of(p_target);
	if (p_target.kind == GritOperand::Kind::VARIABLE && storage == p_type) {
		return "&" + variable_name(p_target.index);
	}
	if (is_lvalue(p_target) && storage == Variant::NIL && static_type_of(p_target) == p_type) {
		r_scope.prelude.push_back("GritRuntime::prepare(" + lvalue(p_target) + ", " + variant_type_name(p_type) + ");");
		return "GritRuntime::internal_pointer<" + cpp_type(p_type) + ">(" + lvalue(p_target) + ")";
	}
	const String result = next_symbol("result");
	r_scope.prelude.push_back(cpp_type(p_type) + " " + result + "{};");
	if (is_lvalue(p_target)) {
		r_scope.postlude.push_back(lvalue(p_target) + " = " + convert_expression(result, p_type, storage) + ";");
	}
	return "&" + result;
}

String GritCppEmitter::arguments_array(const GritStatement &p_statement, int p_first, Scope &r_scope) {
	if (int(p_statement.operands.size()) <= p_first) {
		return "nullptr";
	}
	String values;
	for (uint32_t i = p_first; i < p_statement.operands.size(); i++) {
		if (!values.is_empty()) {
			values += ", ";
		}
		values += "&" + variant_value(p_statement.operands[i], r_scope);
	}
	const String arguments = next_symbol("arguments");
	r_scope.prelude.push_back("const Variant *" + arguments + "[] = { " + values + " };");
	return arguments;
}

String GritCppEmitter::pointer_arguments(const GritStatement &p_statement, int p_first, const LocalVector<Variant::Type> &p_types, Scope &r_scope) {
	if (int(p_statement.operands.size()) <= p_first) {
		return "nullptr";
	}
	String values;
	for (uint32_t i = p_first; i < p_statement.operands.size(); i++) {
		if (!values.is_empty()) {
			values += ", ";
		}
		values += pointer_input(p_statement.operands[i], p_types[i - p_first], r_scope);
	}
	const String arguments = next_symbol("arguments");
	r_scope.prelude.push_back("const void *" + arguments + "[] = { " + values + " };");
	return arguments;
}

String GritCppEmitter::constant_slot(int p_constant) {
	const int *slot = constant_slots.getptr(p_constant);
	if (slot) {
		return "constants[" + itos(*slot) + "]";
	}
	const int new_slot = constant_initializers.size();
	constant_initializers.push_back(function->constant_initializers[p_constant]);
	constant_slots.insert(p_constant, new_slot);
	return "constants[" + itos(new_slot) + "]";
}

String GritCppEmitter::script_value(const GritType &p_type) {
	if (p_type.script.is_empty()) {
		return "GritRuntime::no_script()";
	}
	const int *slot = script_slots.getptr(p_type.script);
	if (slot) {
		return "constants[" + itos(*slot) + "]";
	}
	const int new_slot = constant_initializers.size();
	constant_initializers.push_back("GritRuntime::load_script(p_script, " + string_literal(p_type.script) + ")");
	script_slots.insert(p_type.script, new_slot);
	return "constants[" + itos(new_slot) + "]";
}

String GritCppEmitter::string_name(const StringName &p_name) {
	if (p_name == StringName()) {
		return "StringName()";
	}
	return add_static("const StringName", "name", "StringName(" + string_literal(p_name) + ", true)");
}

void GritCppEmitter::guard_members(const GritStatement &p_statement, Scope &r_scope) {
	bool uses = p_statement.target.kind == GritOperand::Kind::MEMBER;
	for (const GritOperand &operand : p_statement.operands) {
		uses = uses || operand.kind == GritOperand::Kind::MEMBER;
	}
	if (uses) {
		r_scope.prelude.push_back("if (unlikely(!members)) { GritRuntime::member_access_error(frame, " + itos(line_of(p_statement)) + "); " + error_return() + " }");
	}
}

void GritCppEmitter::assign_value(const GritOperand &p_target, const String &p_expression, Variant::Type p_storage, Scope &r_scope, LocalVector<String> &r_core) {
	if (p_target.kind == GritOperand::Kind::NIL) {
		r_core.push_back("static_cast<void>(" + p_expression + ");");
		return;
	}
	if (p_target.kind == GritOperand::Kind::VARIABLE && is_native(p_target)) {
		r_core.push_back(variable_name(p_target.index) + " = " + convert_expression(p_expression, p_storage, storage_of(p_target)) + ";");
		return;
	}
	r_core.push_back(variant_target(p_target, r_scope) + " = " + convert_expression(p_expression, p_storage, Variant::NIL) + ";");
}

void GritCppEmitter::assign_typed(const GritOperand &p_target, const GritOperand &p_source, const GritType &p_type, int p_line, Scope &r_scope, LocalVector<String> &r_core) {
	const String line = itos(p_line);
	switch (p_type.kind) {
		case GritType::Kind::BUILTIN: {
			if (p_type.builtin_type == Variant::ARRAY && p_type.has_elements()) {
				const GritType &element = p_type.elements[0];
				r_core.push_back(checked("GritRuntime::assign_typed_array(" + variant_target(p_target, r_scope) + ", " + variant_value(p_source, r_scope) + ", " + variant_type_name(element.builtin_type) + ", " + string_name(element.native_type) + ", " + script_value(element) + ", frame, " + line + ")"));
				return;
			}
			if (p_type.builtin_type == Variant::DICTIONARY && p_type.has_elements()) {
				const GritType key = p_type.element(0);
				const GritType value = p_type.element(1);
				r_core.push_back(checked("GritRuntime::assign_typed_dictionary(" + variant_target(p_target, r_scope) + ", " + variant_value(p_source, r_scope) + ", " + variant_type_name(key.builtin_type) + ", " + string_name(key.native_type) + ", " + script_value(key) + ", " + variant_type_name(value.builtin_type) + ", " + string_name(value.native_type) + ", " + script_value(value) + ", frame, " + line + ")"));
				return;
			}
			const Variant::Type source_storage = storage_of(p_source);
			if (source_storage != Variant::NIL && (source_storage == p_type.builtin_type || (is_numeric(source_storage) && is_numeric(p_type.builtin_type)))) {
				assign_value(p_target, converted(p_source, p_type.builtin_type), p_type.builtin_type, r_scope, r_core);
				return;
			}
			r_core.push_back(checked("GritRuntime::assign_typed_builtin(" + variant_target(p_target, r_scope) + ", " + variant_value(p_source, r_scope) + ", " + variant_type_name(p_type.builtin_type) + ", frame, " + line + ")"));
			return;
		}
		case GritType::Kind::NATIVE:
			r_core.push_back(checked("GritRuntime::assign_typed_native(" + variant_target(p_target, r_scope) + ", " + variant_value(p_source, r_scope) + ", " + string_name(p_type.native_type) + ", frame, " + line + ")"));
			return;
		case GritType::Kind::SCRIPT:
			r_core.push_back(checked("GritRuntime::assign_typed_script(" + variant_target(p_target, r_scope) + ", " + variant_value(p_source, r_scope) + ", " + script_value(p_type) + ", frame, " + line + ")"));
			return;
		case GritType::Kind::VARIANT:
			break;
	}
	if (is_native(p_source)) {
		assign_value(p_target, native_expression(p_source), storage_of(p_source), r_scope, r_core);
	} else if (p_target.kind != GritOperand::Kind::NIL) {
		assign_value(p_target, variant_value(p_source, r_scope), Variant::NIL, r_scope, r_core);
	}
}

void GritCppEmitter::clear_to_nil(const GritOperand &p_target, Scope &r_scope, LocalVector<String> &r_core) {
	if (p_target.kind == GritOperand::Kind::NIL) {
		return;
	}
	if (p_target.kind == GritOperand::Kind::VARIABLE && is_native(p_target)) {
		r_core.push_back(variable_name(p_target.index) + " = " + default_value(storage_of(p_target)) + ";");
		return;
	}
	r_core.push_back(variant_target(p_target, r_scope) + " = Variant();");
}

bool GritCppEmitter::native_unary(const GritStatement &p_statement, String &r_expression, Variant::Type &r_storage) {
	const GritOperand &operand = p_statement.operands[0];
	const Variant::Type storage = storage_of(operand);
	if (!is_numeric(storage)) {
		return false;
	}
	const bool number = storage == Variant::INT || storage == Variant::FLOAT;
	const String expression = native_expression(operand);
	switch (p_statement.op) {
		case Variant::OP_NEGATE:
			r_storage = storage;
			r_expression = storage == Variant::INT ? "GritRuntime::negate_int(" + expression + ")" : "(-" + expression + ")";
			return number;
		case Variant::OP_POSITIVE:
			r_storage = storage;
			r_expression = expression;
			return number;
		case Variant::OP_NOT: {
			Scope unused;
			r_storage = Variant::BOOL;
			r_expression = "(!" + truth_value(operand, unused) + ")";
			return true;
		}
		case Variant::OP_BIT_NEGATE:
			r_storage = Variant::INT;
			r_expression = "(~" + expression + ")";
			return storage == Variant::INT;
		default:
			return false;
	}
}

bool GritCppEmitter::native_binary(const GritStatement &p_statement, String &r_expression, Variant::Type &r_storage) {
	const GritOperand &left = p_statement.operands[0];
	const GritOperand &right = p_statement.operands[1];
	const Variant::Type left_storage = storage_of(left);
	const Variant::Type right_storage = storage_of(right);
	if (!is_numeric(left_storage) || !is_numeric(right_storage)) {
		return false;
	}
	const bool both_numeric = left_storage != Variant::BOOL && right_storage != Variant::BOOL;
	const bool both_int = left_storage == Variant::INT && right_storage == Variant::INT;

	const auto arithmetic = [&](const String &p_function, const String &p_operator) {
		if (both_int) {
			r_storage = Variant::INT;
			r_expression = "GritRuntime::" + p_function + "(" + converted(left, Variant::INT) + ", " + converted(right, Variant::INT) + ")";
		} else {
			r_storage = Variant::FLOAT;
			r_expression = "(" + converted(left, Variant::FLOAT) + " " + p_operator + " " + converted(right, Variant::FLOAT) + ")";
		}
		return both_numeric;
	};
	const auto comparison = [&](const String &p_operator, bool p_allow_bool) {
		r_storage = Variant::BOOL;
		r_expression = "(" + native_expression(left) + " " + p_operator + " " + native_expression(right) + ")";
		return both_numeric || (p_allow_bool && left_storage == Variant::BOOL && right_storage == Variant::BOOL);
	};
	const auto integer = [&](const String &p_expression) {
		r_storage = Variant::INT;
		r_expression = p_expression;
		return both_int;
	};
	const String left_int = converted(left, Variant::INT);
	const String right_int = converted(right, Variant::INT);

	switch (p_statement.op) {
		case Variant::OP_ADD:
			return arithmetic("add_int", "+");
		case Variant::OP_SUBTRACT:
			return arithmetic("subtract_int", "-");
		case Variant::OP_MULTIPLY:
			return arithmetic("multiply_int", "*");
		case Variant::OP_DIVIDE:
			return arithmetic("divide_int", "/");
		case Variant::OP_MODULE:
			return integer("GritRuntime::modulo_int(" + left_int + ", " + right_int + ")");
		case Variant::OP_EQUAL:
			return comparison("==", true);
		case Variant::OP_NOT_EQUAL:
			return comparison("!=", true);
		case Variant::OP_LESS:
			return comparison("<", false);
		case Variant::OP_LESS_EQUAL:
			return comparison("<=", false);
		case Variant::OP_GREATER:
			return comparison(">", false);
		case Variant::OP_GREATER_EQUAL:
			return comparison(">=", false);
		case Variant::OP_BIT_AND:
			return integer("(" + left_int + " & " + right_int + ")");
		case Variant::OP_BIT_OR:
			return integer("(" + left_int + " | " + right_int + ")");
		case Variant::OP_BIT_XOR:
			return integer("(" + left_int + " ^ " + right_int + ")");
		case Variant::OP_SHIFT_LEFT:
			return integer("GritRuntime::shift_left_int(" + left_int + ", " + right_int + ")");
		case Variant::OP_SHIFT_RIGHT:
			return integer("GritRuntime::shift_right_int(" + left_int + ", " + right_int + ")");
		default:
			return false;
	}
}

bool GritCppEmitter::write_pointer_operator(const GritStatement &p_statement, Scope &r_scope, LocalVector<String> &r_core) {
	if (!p_statement.validated) {
		return false;
	}
	const bool is_unary = p_statement.opcode == GritOpcode::UNARY;
	const Variant::Type left_type = static_type_of(p_statement.operands[0]);
	const Variant::Type right_type = is_unary ? Variant::NIL : static_type_of(p_statement.operands[1]);
	if (left_type == Variant::NIL || !grit_is_pointer_type(left_type)) {
		return false;
	}
	if (!is_unary && (right_type == Variant::NIL || !grit_is_pointer_type(right_type))) {
		return false;
	}
	const Variant::Type result_type = Variant::get_operator_return_type(p_statement.op, left_type, right_type);
	const Variant::PTROperatorEvaluator pointer_evaluator = Variant::get_ptr_operator_evaluator(p_statement.op, left_type, right_type);
	if (result_type == Variant::NIL || !grit_is_pointer_type(result_type) || !pointer_evaluator) {
		return false;
	}
	const String inline_evaluator = GritInlineCatalog::operator_evaluator(pointer_evaluator);
	String evaluator;
	if (inline_evaluator.is_empty()) {
		evaluator = add_static("Variant::PTROperatorEvaluator const", "evaluator", "Variant::get_ptr_operator_evaluator(static_cast<Variant::Operator>(" + itos(p_statement.op) + "), " + variant_type_name(left_type) + ", " + variant_type_name(right_type) + ")");
	} else {
		evaluator = inline_evaluator + "::ptr_evaluate";
	}
	const String left = pointer_input(p_statement.operands[0], left_type, r_scope);
	const String right = is_unary ? String("nullptr") : pointer_input(p_statement.operands[1], right_type, r_scope);
	const String result = pointer_output(p_statement.target, result_type, r_scope);
	r_core.push_back(evaluator + "(" + left + ", " + right + ", " + result + ");");
	return true;
}

bool GritCppEmitter::write_pointer_call(const GritStatement &p_statement, Scope &r_scope, LocalVector<String> &r_core) {
	if (!p_statement.validated) {
		return false;
	}
	const String line = itos(line_of(p_statement));
	const GritOperand &target = p_statement.target;
	const StringName &name = p_statement.name;
	LocalVector<Variant::Type> types;

	switch (p_statement.opcode) {
		case GritOpcode::GET_NAMED: {
			const Variant::Type base_type = static_type_of(p_statement.operands[0]);
			if (base_type == Variant::NIL || !grit_is_pointer_type(base_type)) {
				return false;
			}
			const Variant::Type member_type = Variant::get_member_type(base_type, name);
			const Variant::PTRGetter pointer_getter = Variant::get_member_ptr_getter(base_type, name);
			if (member_type == Variant::NIL || !grit_is_pointer_type(member_type) || !pointer_getter) {
				return false;
			}
			const String inline_getter = GritInlineCatalog::member_accessor(pointer_getter, nullptr);
			const String getter = inline_getter.is_empty() ? add_static("Variant::PTRGetter const", "getter", "Variant::get_member_ptr_getter(" + variant_type_name(base_type) + ", " + string_name(name) + ")") : inline_getter + "::ptr_get";
			const String base = pointer_input(p_statement.operands[0], base_type, r_scope);
			r_core.push_back(getter + "(" + base + ", " + pointer_output(target, member_type, r_scope) + ");");
			return true;
		}
		case GritOpcode::SET_NAMED: {
			const Variant::Type base_type = static_type_of(p_statement.operands[0]);
			if (base_type == Variant::NIL || !grit_is_pointer_type(base_type)) {
				return false;
			}
			const Variant::Type member_type = Variant::get_member_type(base_type, name);
			const Variant::PTRSetter pointer_setter = Variant::get_member_ptr_setter(base_type, name);
			if (member_type == Variant::NIL || !grit_is_pointer_type(member_type) || !pointer_setter) {
				return false;
			}
			const String inline_setter = GritInlineCatalog::member_accessor(nullptr, pointer_setter);
			const String setter = inline_setter.is_empty() ? add_static("Variant::PTRSetter const", "setter", "Variant::get_member_ptr_setter(" + variant_type_name(base_type) + ", " + string_name(name) + ")") : inline_setter + "::ptr_set";
			const String value = pointer_input(p_statement.operands[1], member_type, r_scope);
			r_core.push_back(setter + "(" + pointer_base(p_statement.operands[0], base_type, r_scope) + ", " + value + ");");
			return true;
		}
		case GritOpcode::CONSTRUCT: {
			const Variant::Type type = p_statement.builtin_type;
			if (!is_trivial_type(type) || Variant::get_constructor_argument_count(type, p_statement.index) != int(p_statement.operands.size())) {
				return false;
			}
			for (uint32_t i = 0; i < p_statement.operands.size(); i++) {
				types.push_back(Variant::get_constructor_argument_type(type, p_statement.index, i));
				if (!grit_is_pointer_type(types[i])) {
					return false;
				}
			}
			const Variant::PTRConstructor pointer_constructor = Variant::get_ptr_constructor(type, p_statement.index);
			if (!pointer_constructor) {
				return false;
			}
			const String inline_constructor = GritInlineCatalog::constructor(pointer_constructor);
			const String constructor = inline_constructor.is_empty() ? add_static("Variant::PTRConstructor const", "constructor", "Variant::get_ptr_constructor(" + variant_type_name(type) + ", " + itos(p_statement.index) + ")") : inline_constructor + "::ptr_construct";
			const String arguments = pointer_arguments(p_statement, 0, types, r_scope);
			r_core.push_back(constructor + "(" + pointer_output(target, type, r_scope) + ", " + arguments + ");");
			return true;
		}
		case GritOpcode::CALL_UTILITY: {
			if (Variant::is_utility_function_vararg(name) || Variant::get_utility_function_argument_count(name) != int(p_statement.operands.size())) {
				return false;
			}
			for (uint32_t i = 0; i < p_statement.operands.size(); i++) {
				types.push_back(Variant::get_utility_function_argument_type(name, i));
				if (!grit_is_pointer_type(types[i])) {
					return false;
				}
			}
			const bool has_return = Variant::has_utility_function_return_value(name);
			const Variant::Type return_type = has_return ? Variant::get_utility_function_return_type(name) : Variant::NIL;
			if (!grit_is_pointer_type(return_type) || !Variant::get_ptr_utility_function(name)) {
				return false;
			}
			const String utility = add_static("Variant::PTRUtilityFunction const", "utility", "Variant::get_ptr_utility_function(" + string_name(name) + ")");
			const String arguments = pointer_arguments(p_statement, 0, types, r_scope);
			const String result = has_return ? pointer_output(target, return_type, r_scope) : String("nullptr");
			r_core.push_back(utility + "(" + result + ", " + arguments + ", " + itos(p_statement.operands.size()) + ");");
			if (!has_return) {
				clear_to_nil(target, r_scope, r_core);
			}
			return true;
		}
		case GritOpcode::CALL_BUILTIN:
		case GritOpcode::CALL_BUILTIN_STATIC: {
			const bool is_static = p_statement.opcode == GritOpcode::CALL_BUILTIN_STATIC;
			const int first = is_static ? 0 : 1;
			const int argument_count = int(p_statement.operands.size()) - first;
			const Variant::Type type = p_statement.builtin_type;
			if (type == Variant::NIL || !grit_is_pointer_type(type)) {
				return false;
			}
			if (Variant::is_builtin_method_vararg(type, name)) {
				return false;
			}
			const int parameter_count = Variant::get_builtin_method_argument_count(type, name);
			const Vector<Variant> defaults = Variant::get_builtin_method_default_arguments(type, name);
			const int first_default = parameter_count - defaults.size();
			if (argument_count > parameter_count || argument_count < first_default) {
				return false;
			}
			for (int i = 0; i < parameter_count; i++) {
				types.push_back(Variant::get_builtin_method_argument_type(type, name, i));
				if (!grit_is_pointer_type(types[i])) {
					return false;
				}
				if (i >= argument_count && types[i] != Variant::NIL && defaults[i - first_default].get_type() != types[i]) {
					return false;
				}
			}
			const bool has_return = Variant::has_builtin_method_return_value(type, name);
			const Variant::Type return_type = has_return ? Variant::get_builtin_method_return_type(type, name) : Variant::NIL;
			if (!grit_is_pointer_type(return_type) || !Variant::get_ptr_builtin_method(type, name)) {
				return false;
			}
			const String method = add_static("Variant::PTRBuiltInMethod const", "builtin_method", "Variant::get_ptr_builtin_method(" + variant_type_name(type) + ", " + string_name(name) + ")");
			String base = "nullptr";
			if (!is_static) {
				const GritOperand &operand = p_statement.operands[0];
				const bool readonly_constant = operand.kind == GritOperand::Kind::CONSTANT && !is_native(operand) && static_type_of(operand) == type;
				if (readonly_constant && Variant::is_builtin_method_const(type, name)) {
					base = "const_cast<" + cpp_type(type) + " *>(GritRuntime::internal_pointer<" + cpp_type(type) + ">(" + variant_value(operand, r_scope) + "))";
				} else {
					base = pointer_base(operand, type, r_scope);
				}
			}
			String arguments = "nullptr";
			if (parameter_count > 0) {
				String values;
				for (int i = 0; i < parameter_count; i++) {
					String value;
					if (i < argument_count) {
						value = pointer_input(p_statement.operands[first + i], types[i], r_scope);
					} else {
						const String default_values = add_static("const Vector<Variant>", "defaults", "Variant::get_builtin_method_default_arguments(" + variant_type_name(type) + ", " + string_name(name) + ")");
						const String default_value = default_values + "[" + itos(i - first_default) + "]";
						value = types[i] == Variant::NIL ? "&" + default_value : "GritRuntime::internal_pointer<" + cpp_type(types[i]) + ">(" + default_value + ")";
					}
					values += (i > 0 ? ", " : "") + value;
				}
				arguments = next_symbol("arguments");
				r_scope.prelude.push_back("const void *" + arguments + "[] = { " + values + " };");
			}
			const String result = has_return ? pointer_output(target, return_type, r_scope) : String("nullptr");
			r_core.push_back(method + "(" + base + ", " + arguments + ", " + result + ", " + itos(parameter_count) + ");");
			if (!has_return) {
				clear_to_nil(target, r_scope, r_core);
			}
			return true;
		}
		case GritOpcode::CALL_METHOD_BIND:
		case GritOpcode::CALL_NATIVE_STATIC: {
			const bool is_static = p_statement.opcode == GritOpcode::CALL_NATIVE_STATIC;
			const int first = is_static ? 0 : 1;
			const MethodBind *method = ClassDB::get_method(p_statement.class_name, name);
			if (!method || method->is_vararg() || method->get_argument_count() != int(p_statement.operands.size()) - first) {
				return false;
			}
			for (int i = 0; i < method->get_argument_count(); i++) {
				types.push_back(method->get_argument_type(i));
				if (!grit_is_pointer_type(types[i])) {
					return false;
				}
			}
			const bool has_return = method->has_return();
			const Variant::Type return_type = has_return ? method->get_return_info().type : Variant::NIL;
			if (!grit_is_pointer_type(return_type)) {
				return false;
			}
			const String method_symbol = add_static("MethodBind *const", "method", "ClassDB::get_method(" + string_name(p_statement.class_name) + ", " + string_name(name) + ")");
			r_scope.prelude.push_back("if (unlikely(!" + method_symbol + ")) { GritRuntime::report_error(frame, " + line + ", " + string_literal(vformat("Grit: method \"%s.%s\" is not available.", p_statement.class_name, name)) + "); " + error_return() + " }");
			const String base = is_static ? String() : variant_value(p_statement.operands[0], r_scope);
			const String arguments = pointer_arguments(p_statement, first, types, r_scope);
			const String result = has_return ? pointer_output(target, return_type, r_scope) : String("nullptr");
			if (is_static) {
				r_core.push_back(method_symbol + "->ptrcall(nullptr, " + arguments + ", " + result + ");");
			} else {
				r_core.push_back(checked("GritRuntime::call_method_bind_pointer(" + method_symbol + ", " + base + ", " + arguments + ", " + result + ", frame, " + line + ")"));
			}
			if (!has_return) {
				clear_to_nil(target, r_scope, r_core);
			}
			return true;
		}
		default:
			return false;
	}
}

bool GritCppEmitter::as_builtin_call(const GritStatement &p_statement, GritStatement &r_builtin) const {
	if (p_statement.opcode != GritOpcode::CALL) {
		return false;
	}
	const Variant::Type type = static_type_of(p_statement.operands[0]);
	const StringName &name = p_statement.name;
	if (type == Variant::NIL || !grit_is_pointer_type(type) || !Variant::has_builtin_method(type, name)) {
		return false;
	}
	if (Variant::is_builtin_method_vararg(type, name) || Variant::is_builtin_method_static(type, name)) {
		return false;
	}
	if (p_statement.target.kind != GritOperand::Kind::NIL && !Variant::has_builtin_method_return_value(type, name)) {
		return false;
	}
	const int argument_count = int(p_statement.operands.size()) - 1;
	const int parameter_count = Variant::get_builtin_method_argument_count(type, name);
	for (int i = 0; i < MIN(argument_count, parameter_count); i++) {
		const Variant::Type parameter = Variant::get_builtin_method_argument_type(type, name, i);
		if (parameter != Variant::NIL && static_type_of(p_statement.operands[i + 1]) != parameter) {
			return false;
		}
	}
	r_builtin = p_statement;
	r_builtin.opcode = GritOpcode::CALL_BUILTIN;
	r_builtin.builtin_type = type;
	r_builtin.validated = true;
	return true;
}

bool GritCppEmitter::write_dictionary_access(const GritStatement &p_statement, Scope &r_scope, LocalVector<String> &r_core) {
	const GritOperand &base = p_statement.operands[0];
	const String line = itos(line_of(p_statement));
	if (p_statement.opcode == GritOpcode::GET_KEYED) {
		const GritOperand &target = p_statement.target;
		if (target.kind == base.kind && target.index == base.index) {
			return false;
		}
		const String base_value = variant_value(base, r_scope);
		const String key = variant_value(p_statement.operands[1], r_scope);
		const String entry = next_symbol("entry");
		r_core.push_back("const Variant *" + entry + " = GritRuntime::dictionary_entry(*GritRuntime::internal_pointer<Dictionary>(" + base_value + "), " + key + ");");
		LocalVector<String> assignment;
		assign_value(target, "*" + entry, Variant::NIL, r_scope, assignment);
		r_core.push_back("if (likely(" + entry + ")) {");
		for (const String &statement : assignment) {
			r_core.push_back("\t" + statement);
		}
		r_core.push_back("} else if (unlikely(!GritRuntime::missing_key(" + base_value + ", " + key + ", frame, " + line + "))) {");
		r_core.push_back("\t" + error_return());
		r_core.push_back("}");
		return true;
	}
	if (!is_lvalue(base)) {
		return false;
	}
	const String key = variant_value(p_statement.operands[1], r_scope);
	const String value = variant_value(p_statement.operands[2], r_scope);
	r_core.push_back("if (unlikely(!GritRuntime::internal_pointer<Dictionary>(" + lvalue(base) + ")->set(" + key + ", " + value + ")) && unlikely(!GritRuntime::keyed_set_failed(" + lvalue(base) + ", " + key + ", " + value + ", frame, " + line + "))) {");
	r_core.push_back("\t" + error_return());
	r_core.push_back("}");
	return true;
}

String GritCppEmitter::index_expression(const GritOperand &p_operand, Scope &r_scope) {
	if (is_native(p_operand)) {
		return converted(p_operand, Variant::INT);
	}
	return "*VariantInternal::get_int(&" + variant_value(p_operand, r_scope) + ")";
}

bool GritCppEmitter::write_inline_access(const GritStatement &p_statement, Scope &r_scope, LocalVector<String> &r_core) {
	if (p_statement.opcode != GritOpcode::GET_KEYED && p_statement.opcode != GritOpcode::SET_KEYED) {
		return false;
	}
	const GritOperand &base = p_statement.operands[0];
	if (p_statement.index == 2 && static_type_of(base) == Variant::DICTIONARY && !is_native(base) && base.kind != GritOperand::Kind::NIL) {
		return write_dictionary_access(p_statement, r_scope, r_core);
	}
	const GritOperand &key = p_statement.operands[1];
	const Variant::Type base_type = static_type_of(base);
	const Variant::Type key_storage = storage_of(key);
	const bool integer_key = key_storage == Variant::INT || (key_storage == Variant::NIL && key.kind != GritOperand::Kind::NIL && static_type_of(key) == Variant::INT);
	const bool is_array = base_type == Variant::ARRAY;
	const String element_type = packed_element_type(base_type);
	if (is_native(base) || base.kind == GritOperand::Kind::NIL || !integer_key || (!is_array && element_type.is_empty())) {
		return false;
	}
	const String line = itos(line_of(p_statement));
	const String container_type = cpp_type(base_type);
	const Variant::Type element_storage = Variant::get_indexed_element_type(base_type);

	if (p_statement.opcode == GritOpcode::GET_KEYED) {
		const GritOperand &target = p_statement.target;
		if (p_statement.index != 1 || (target.kind == base.kind && target.index == base.index)) {
			return false;
		}
		const String base_value = variant_value(base, r_scope);
		const String index = index_expression(key, r_scope);
		const String element = next_symbol("element");
		const String container = "*GritRuntime::internal_pointer<" + container_type + ">(" + base_value + ")";
		if (is_array) {
			r_core.push_back("const Variant *" + element + " = GritRuntime::array_element(" + container + ", " + index + ");");
		} else {
			r_core.push_back("const " + element_type + " *" + element + " = GritRuntime::packed_element(" + container + ", " + index + ");");
		}
		String value = "*" + element;
		if (element_storage == Variant::INT || element_storage == Variant::FLOAT) {
			value = "static_cast<" + cpp_type(element_storage) + ">(" + value + ")";
		}
		LocalVector<String> assignment;
		assign_value(target, value, element_storage, r_scope, assignment);
		r_core.push_back("if (likely(" + element + ")) {");
		for (const String &statement : assignment) {
			r_core.push_back("\t" + statement);
		}
		r_core.push_back("} else if (unlikely(!GritRuntime::out_of_bounds_get(" + base_value + ", " + index + ", frame, " + line + "))) {");
		r_core.push_back("\t" + error_return());
		r_core.push_back("}");
		return true;
	}

	if (!is_lvalue(base)) {
		return false;
	}
	const GritOperand &value = p_statement.operands[2];
	const String container = "*GritRuntime::internal_pointer<" + container_type + ">(" + lvalue(base) + ")";
	if (p_statement.index == 1 && !is_array) {
		const String element_value = is_native(value) ? converted(value, element_storage) : convert_expression(variant_value(value, r_scope), Variant::NIL, element_storage);
		const String index = index_expression(key, r_scope);
		const String slot = next_symbol("slot");
		r_core.push_back(element_type + " *" + slot + " = GritRuntime::packed_slot(" + container + ", " + index + ");");
		r_core.push_back("if (likely(" + slot + ")) {");
		r_core.push_back("\t*" + slot + " = static_cast<" + element_type + ">(" + element_value + ");");
		r_core.push_back("} else if (unlikely(!GritRuntime::out_of_bounds_set(" + lvalue(base) + ", " + index + ", frame, " + line + "))) {");
		r_core.push_back("\t" + error_return());
		r_core.push_back("}");
		return true;
	}
	if (p_statement.index != 1 && p_statement.index != 2 && is_array) {
		const String element_value = variant_value(value, r_scope);
		const String index = index_expression(key, r_scope);
		r_core.push_back("if (unlikely(!GritRuntime::array_set(" + container + ", " + index + ", " + element_value + "))) {");
		String key_value;
		if (is_native(key)) {
			key_value = next_symbol("key");
			r_core.push_back("\tconst Variant " + key_value + "(" + index + ");");
		} else {
			key_value = variant_value(key, r_scope);
		}
		r_core.push_back("\t" + checked("GritRuntime::set_keyed(" + lvalue(base) + ", " + key_value + ", " + element_value + ", frame, " + line + ")"));
		r_core.push_back("}");
		return true;
	}
	return false;
}

void GritCppEmitter::collect_reads() {
	variables_read.resize(function->variables.size());
	for (bool &read : variables_read) {
		read = false;
	}
	uses_class = false;
	uses_members = false;
	for (const GritBlock &block : function->blocks) {
		for (const GritStatement &statement : block.statements) {
			uses_members = uses_members || statement.target.kind == GritOperand::Kind::MEMBER;
			for (const GritOperand &operand : statement.operands) {
				if (operand.kind == GritOperand::Kind::VARIABLE) {
					variables_read[operand.index] = true;
				}
				uses_class = uses_class || operand.kind == GritOperand::Kind::CLASS;
				uses_members = uses_members || operand.kind == GritOperand::Kind::MEMBER;
			}
		}
	}
}

String GritCppEmitter::hoist(const String &p_prefix, Variant::Type p_storage, bool p_is_saved) {
	HoistedState state;
	state.name = next_symbol(p_prefix);
	state.storage = p_storage;
	state.is_saved = p_is_saved;
	hoisted.push_back(state);
	return state.name;
}

void GritCppEmitter::collect_hoisted(int p_block) {
	for (const GritStatement &statement : function->blocks[p_block].statements) {
		StatementState state;
		switch (statement.opcode) {
			case GritOpcode::IF:
			case GritOpcode::IF_NOT_SHARED:
				state.first = hoist("condition", Variant::BOOL, false);
				statement_states.insert(&statement, state);
				break;
			case GritOpcode::FOR_RANGE:
				state.first = hoist("range_counter", Variant::INT, true);
				statement_states.insert(&statement, state);
				break;
			case GritOpcode::FOR_EACH:
				state.first = hoist("iterating", Variant::BOOL, true);
				if (is_native(statement.target)) {
					state.second = hoist("iterator", Variant::NIL, true);
				}
				if (is_native(statement.operands[0])) {
					state.third = hoist("container", Variant::NIL, true);
				}
				statement_states.insert(&statement, state);
				break;
			default:
				break;
		}
		for (int block : statement.blocks) {
			if (block >= 0) {
				collect_hoisted(block);
			}
		}
	}
}

int GritCppEmitter::slot_count() const {
	int count = function->variables.size();
	for (const HoistedState &state : hoisted) {
		count += state.is_saved ? 1 : 0;
	}
	return count;
}

void GritCppEmitter::save_slots(const String &p_slots, LocalVector<String> &r_lines) const {
	int slot = 0;
	const auto save = [&](const String &p_name, Variant::Type p_storage) {
		const String value = p_storage == Variant::NIL ? "std::move(" + p_name + ")" : "Variant(" + p_name + ")";
		r_lines.push_back(p_slots + "[" + itos(slot++) + "] = " + value + ";");
	};
	for (uint32_t i = 0; i < function->variables.size(); i++) {
		save(variable_name(i), function->variables[i].storage);
	}
	for (const HoistedState &state : hoisted) {
		if (state.is_saved) {
			save(state.name, state.storage);
		}
	}
}

void GritCppEmitter::restore_slots(const String &p_slots, LocalVector<String> &r_lines) const {
	int slot = 0;
	const auto restore = [&](const String &p_name, Variant::Type p_storage) {
		const String slot_value = p_slots + "[" + itos(slot++) + "]";
		const String value = p_storage == Variant::NIL ? "std::move(" + slot_value + ")" : take_expression(slot_value, p_storage);
		r_lines.push_back(p_name + " = " + value + ";");
	};
	for (uint32_t i = 0; i < function->variables.size(); i++) {
		restore(variable_name(i), function->variables[i].storage);
	}
	for (const HoistedState &state : hoisted) {
		if (state.is_saved) {
			restore(state.name, state.storage);
		}
	}
}

void GritCppEmitter::write_await(const GritStatement &p_statement) {
	Scope scope;
	LocalVector<String> core;
	guard_members(p_statement, scope);
	const String line = itos(line_of(p_statement));
	const String resume = itos(p_statement.index);
	const String signal = next_symbol("signal");
	const String awaited = next_symbol("awaited");
	const String action = next_symbol("await_action");
	const String slots = next_symbol("slots");
	const String state = next_symbol("state");
	const String operand = variant_value(p_statement.operands[0], scope);
	scope.prelude.push_back("Signal " + signal + ";");
	scope.prelude.push_back("Variant " + awaited + ";");
	core.push_back("const GritRuntime::AwaitAction " + action + " = GritRuntime::await_operand(" + operand + ", " + signal + ", " + awaited + ", frame, " + line + ");");
	core.push_back("if (unlikely(" + action + " == GritRuntime::AwaitAction::ERROR)) { " + error_return() + " }");
	core.push_back("if (" + action + " == GritRuntime::AwaitAction::SUSPEND) {");
	core.push_back("	Variant *" + slots + " = nullptr;");
	core.push_back("	const Ref<GDScriptFunctionState> " + state + " = GritRuntime::await_state(frame, p_state, " + resume + ", " + itos(slot_count()) + ", " + line + ", " + slots + ");");
	LocalVector<String> saves;
	save_slots(slots, saves);
	for (const String &save : saves) {
		core.push_back("	" + save);
	}
	core.push_back("	return GritRuntime::await_suspend(" + state + ", " + signal + ", r_awaited, frame, " + line + ");");
	core.push_back("}");
	assign_value(p_statement.target, awaited, Variant::NIL, scope, core);
	write_scoped(scope, core);
	write_line("await_resume_" + resume + ":;");

	Scope resume_scope;
	LocalVector<String> resume_core;
	if (p_statement.target.kind == GritOperand::Kind::MEMBER) {
		resume_scope.prelude.push_back("if (unlikely(!members)) { GritRuntime::member_access_error(frame, " + line + "); " + error_return() + " }");
	}
	assign_value(p_statement.target, "p_state->result", Variant::NIL, resume_scope, resume_core);
	resume_cases.push_back("case " + resume + ": {");
	for (const String &prelude : resume_scope.prelude) {
		resume_cases.push_back("	" + prelude);
	}
	for (const String &statement : resume_core) {
		resume_cases.push_back("	" + statement);
	}
	for (const String &postlude : resume_scope.postlude) {
		resume_cases.push_back("	" + postlude);
	}
	resume_cases.push_back("}");
	resume_cases.push_back("	goto await_resume_" + resume + ";");
}

void GritCppEmitter::write_block(int p_block) {
	current_line = -1;
	for (const GritStatement &statement : function->blocks[p_block].statements) {
		write_statement(statement);
	}
}

void GritCppEmitter::write_statement(const GritStatement &p_statement) {
	if (p_statement.line > 0 && p_statement.line != current_line) {
		write_line("GritRuntime::line_reached(call_scope, " + itos(p_statement.line) + ");");
		current_line = p_statement.line;
	}
	switch (p_statement.opcode) {
		case GritOpcode::ASSIGN:
		case GritOpcode::ASSIGN_TYPED:
		case GritOpcode::TEST:
		case GritOpcode::CLEAR:
		case GritOpcode::UNARY:
		case GritOpcode::BINARY:
		case GritOpcode::TYPE_TEST:
		case GritOpcode::CAST:
			write_operator(p_statement);
			break;
		case GritOpcode::GET_KEYED:
		case GritOpcode::SET_KEYED:
		case GritOpcode::GET_NAMED:
		case GritOpcode::SET_NAMED:
		case GritOpcode::GET_PROPERTY:
		case GritOpcode::SET_PROPERTY:
		case GritOpcode::GET_STATIC:
		case GritOpcode::SET_STATIC:
		case GritOpcode::GET_GLOBAL:
		case GritOpcode::CONSTRUCT:
		case GritOpcode::CONSTRUCT_ARRAY:
		case GritOpcode::CONSTRUCT_DICTIONARY:
			write_access(p_statement);
			break;
		case GritOpcode::CALL:
		case GritOpcode::CALL_ASYNC:
		case GritOpcode::CALL_SUPER:
		case GritOpcode::CALL_UTILITY:
		case GritOpcode::CALL_GDSCRIPT_UTILITY:
		case GritOpcode::CALL_BUILTIN:
		case GritOpcode::CALL_BUILTIN_STATIC:
		case GritOpcode::CALL_NATIVE_STATIC:
		case GritOpcode::CALL_METHOD_BIND:
		case GritOpcode::CREATE_LAMBDA:
			write_call(p_statement);
			break;
		case GritOpcode::AWAIT:
			write_await(p_statement);
			current_line = -1;
			break;
		case GritOpcode::RETURN:
			write_return(p_statement);
			break;
		default:
			write_control(p_statement);
			current_line = -1;
			break;
	}
}

void GritCppEmitter::write_operator(const GritStatement &p_statement) {
	Scope scope;
	LocalVector<String> core;
	guard_members(p_statement, scope);
	const GritOperand &target = p_statement.target;
	const String line = itos(line_of(p_statement));

	switch (p_statement.opcode) {
		case GritOpcode::ASSIGN: {
			const GritOperand &source = p_statement.operands[0];
			if (is_lvalue(source) && source.kind == target.kind && source.index == target.index) {
				break;
			}
			if (is_native(source)) {
				assign_value(target, native_expression(source), storage_of(source), scope, core);
			} else if (target.kind != GritOperand::Kind::NIL) {
				assign_value(target, variant_value(source, scope), Variant::NIL, scope, core);
			}
		} break;
		case GritOpcode::ASSIGN_TYPED:
			assign_typed(target, p_statement.operands[0], type_of(p_statement.type), line_of(p_statement), scope, core);
			break;
		case GritOpcode::TEST:
			assign_value(target, truth_value(p_statement.operands[0], scope), Variant::BOOL, scope, core);
			break;
		case GritOpcode::CLEAR: {
			const GritType &type = type_of(p_statement.type);
			if (type.kind != GritType::Kind::BUILTIN || type.builtin_type == Variant::NIL || type.builtin_type == Variant::OBJECT) {
				clear_to_nil(target, scope, core);
			} else if (type.builtin_type == Variant::ARRAY && type.has_elements()) {
				const GritType &element = type.elements[0];
				core.push_back(variant_target(target, scope) + " = GritRuntime::typed_array(" + variant_type_name(element.builtin_type) + ", " + string_name(element.native_type) + ", " + script_value(element) + ");");
			} else if (type.builtin_type == Variant::DICTIONARY && type.has_elements()) {
				const GritType key = type.element(0);
				const GritType value = type.element(1);
				core.push_back(variant_target(target, scope) + " = GritRuntime::typed_dictionary(" + variant_type_name(key.builtin_type) + ", " + string_name(key.native_type) + ", " + script_value(key) + ", " + variant_type_name(value.builtin_type) + ", " + string_name(value.native_type) + ", " + script_value(value) + ");");
			} else if (grit_is_unboxed_type(type.builtin_type)) {
				assign_value(target, default_value(type.builtin_type), type.builtin_type, scope, core);
			} else {
				const String value = variant_target(target, scope);
				core.push_back(value + " = Variant();");
				core.push_back("VariantInternal::initialize(&" + value + ", " + variant_type_name(type.builtin_type) + ");");
			}
		} break;
		case GritOpcode::UNARY:
		case GritOpcode::BINARY: {
			String expression;
			Variant::Type storage = Variant::NIL;
			const bool is_unary = p_statement.opcode == GritOpcode::UNARY;
			if (is_unary ? native_unary(p_statement, expression, storage) : native_binary(p_statement, expression, storage)) {
				const bool checked_division = !is_unary && storage == Variant::INT && (p_statement.op == Variant::OP_DIVIDE || p_statement.op == Variant::OP_MODULE);
				if (checked_division) {
					const GritOperand &divisor = p_statement.operands[1];
					const bool nonzero_constant = divisor.kind == GritOperand::Kind::CONSTANT && int64_t(function->constants[divisor.index]) != 0;
					if (!nonzero_constant) {
						const String message = p_statement.op == Variant::OP_DIVIDE ? "Division by zero error in operator '/'." : "Modulo by zero error in operator '%'.";
						core.push_back("if (unlikely(" + converted(divisor, Variant::INT) + " == 0)) { GritRuntime::report_error(frame, " + line + ", " + string_literal(message) + "); " + error_return() + " }");
					}
				}
				assign_value(target, expression, storage, scope, core);
				break;
			}
			if (write_pointer_operator(p_statement, scope, core)) {
				break;
			}
			const String left = variant_value(p_statement.operands[0], scope);
			const String right = is_unary ? variant_value(GritOperand(), scope) : variant_value(p_statement.operands[1], scope);
			const String result = variant_target(target, scope);
			if (p_statement.validated) {
				const Variant::Type left_type = static_type_of(p_statement.operands[0]);
				const Variant::Type right_type = is_unary ? Variant::NIL : static_type_of(p_statement.operands[1]);
				const String evaluator = add_static("Variant::ValidatedOperatorEvaluator const", "evaluator", "Variant::get_validated_operator_evaluator(static_cast<Variant::Operator>(" + itos(p_statement.op) + "), " + variant_type_name(left_type) + ", " + variant_type_name(right_type) + ")");
				core.push_back("GritRuntime::prepare(" + result + ", " + variant_type_name(Variant::get_operator_return_type(p_statement.op, left_type, right_type)) + ");");
				core.push_back(evaluator + "(&" + left + ", &" + right + ", &" + result + ");");
			} else if (!is_unary && is_dynamic_numeric_operator(p_statement.op)) {
				core.push_back(checked("GritRuntime::evaluate_dynamic<static_cast<Variant::Operator>(" + itos(p_statement.op) + ")>(" + left + ", " + right + ", " + result + ", frame, " + line + ")"));
			} else {
				core.push_back(checked("GritRuntime::evaluate(static_cast<Variant::Operator>(" + itos(p_statement.op) + "), " + left + ", " + right + ", " + result + ", frame, " + line + ")"));
			}
		} break;
		case GritOpcode::TYPE_TEST: {
			const GritType &type = type_of(p_statement.type);
			const String value = variant_value(p_statement.operands[0], scope);
			switch (type.kind) {
				case GritType::Kind::BUILTIN:
					if (type.builtin_type == Variant::ARRAY && type.has_elements()) {
						const GritType &element = type.elements[0];
						assign_value(target, "GritRuntime::is_typed_array(" + value + ", " + variant_type_name(element.builtin_type) + ", " + string_name(element.native_type) + ", " + script_value(element) + ")", Variant::BOOL, scope, core);
					} else if (type.builtin_type == Variant::DICTIONARY && type.has_elements()) {
						const GritType key = type.element(0);
						const GritType element = type.element(1);
						assign_value(target, "GritRuntime::is_typed_dictionary(" + value + ", " + variant_type_name(key.builtin_type) + ", " + string_name(key.native_type) + ", " + script_value(key) + ", " + variant_type_name(element.builtin_type) + ", " + string_name(element.native_type) + ", " + script_value(element) + ")", Variant::BOOL, scope, core);
					} else {
						assign_value(target, "(" + value + ".get_type() == " + variant_type_name(type.builtin_type) + ")", Variant::BOOL, scope, core);
					}
					break;
				case GritType::Kind::NATIVE:
				case GritType::Kind::SCRIPT: {
					const String result = next_symbol("is_type");
					scope.prelude.push_back("bool " + result + " = false;");
					if (type.kind == GritType::Kind::NATIVE) {
						core.push_back(checked("GritRuntime::type_test_native(" + value + ", " + string_name(type.native_type) + ", " + result + ", frame, " + line + ")"));
					} else {
						core.push_back(checked("GritRuntime::type_test_script(" + value + ", " + script_value(type) + ", " + result + ", frame, " + line + ")"));
					}
					assign_value(target, result, Variant::BOOL, scope, core);
				} break;
				case GritType::Kind::VARIANT:
					assign_value(target, "false", Variant::BOOL, scope, core);
					break;
			}
		} break;
		case GritOpcode::CAST: {
			const GritType &type = type_of(p_statement.type);
			const String value = variant_value(p_statement.operands[0], scope);
			switch (type.kind) {
				case GritType::Kind::BUILTIN:
					core.push_back(checked("GritRuntime::cast_to_builtin(" + value + ", " + variant_type_name(type.builtin_type) + ", " + variant_target(target, scope) + ", frame, " + line + ")"));
					break;
				case GritType::Kind::NATIVE:
					core.push_back(checked("GritRuntime::cast_to_native(" + value + ", " + string_name(type.native_type) + ", " + variant_target(target, scope) + ", frame, " + line + ")"));
					break;
				case GritType::Kind::SCRIPT:
					core.push_back(checked("GritRuntime::cast_to_script(" + value + ", " + script_value(type) + ", " + variant_target(target, scope) + ", frame, " + line + ")"));
					break;
				case GritType::Kind::VARIANT:
					break;
			}
		} break;
		default:
			break;
	}
	write_scoped(scope, core);
}

void GritCppEmitter::write_access(const GritStatement &p_statement) {
	Scope scope;
	LocalVector<String> core;
	guard_members(p_statement, scope);
	if (!write_inline_access(p_statement, scope, core) && !write_pointer_call(p_statement, scope, core) && !write_property_access(p_statement, scope, core) && !write_script_member_access(p_statement, scope, core)) {
		write_generic_access(p_statement, scope, core);
	}
	write_scoped(scope, core);
}

bool GritCppEmitter::object_value_assignable(const GritOperand &p_value, const StringName &p_class) const {
	if (p_value.kind == GritOperand::Kind::CONSTANT && function->constants[p_value.index].get_type() == Variant::NIL) {
		return true;
	}
	const GritType &type = type_of(p_value.type);
	if (type.kind != GritType::Kind::NATIVE && type.kind != GritType::Kind::SCRIPT) {
		return false;
	}
	return type.native_type != StringName() && ClassDB::is_parent_class(type.native_type, p_class);
}

bool GritCppEmitter::write_property_access(const GritStatement &p_statement, Scope &r_scope, LocalVector<String> &r_core) {
	bool is_get = false;
	bool is_named = false;
	switch (p_statement.opcode) {
		case GritOpcode::GET_NAMED:
			is_get = true;
			is_named = true;
			break;
		case GritOpcode::SET_NAMED:
			is_named = true;
			break;
		case GritOpcode::GET_PROPERTY:
			is_get = true;
			break;
		case GritOpcode::SET_PROPERTY:
			break;
		default:
			return false;
	}
	const StringName native_class = is_named ? native_class_of(p_statement.operands[0]) : function->native_base;
	if (native_class == StringName() || !ClassDB::class_exists(native_class)) {
		return false;
	}
	GritRuntime::PropertyAccessors accessors;
	GritRuntime::resolve_property_accessors(native_class, p_statement.name, accessors);
	const GritRuntime::PropertyAccessor &accessor_info = is_get ? accessors.getter : accessors.setter;
	const Variant::Type value_type = accessor_info.value_type;
	if (value_type == Variant::VARIANT_MAX) {
		return false;
	}
	const bool is_object = value_type == Variant::OBJECT;
	const GritOperand &target = p_statement.target;
	if (is_get) {
		const Variant::Type target_storage = storage_of(target);
		if (target.kind == GritOperand::Kind::NIL || (target_storage != Variant::NIL && target_storage != value_type)) {
			return false;
		}
		if (is_named) {
			const GritOperand &base = p_statement.operands[0];
			if (is_lvalue(base) && base.kind == target.kind && base.index == target.index) {
				return false;
			}
		}
	} else {
		const GritOperand &value = p_statement.operands[is_named ? 1 : 0];
		if (is_object) {
			if (!object_value_assignable(value, accessor_info.object_class)) {
				return false;
			}
		} else if (value_type != Variant::NIL && value_type_of(value) != value_type) {
			return false;
		}
	}

	const String cache = next_symbol("property_cache");
	statics.push_back("static GritRuntime::PropertyCache " + cache + ";");
	const String object = next_symbol("property_object");
	const String accessor = next_symbol("property");
	const String arguments = "(" + object + ", " + cache + ", " + string_name(p_statement.name) + ", " + variant_type_name(value_type) + ")";
	if (is_named) {
		const String base = variant_value(p_statement.operands[0], r_scope);
		r_scope.prelude.push_back("Object *const " + object + " = GritRuntime::object_of(" + base + ");");
		r_scope.prelude.push_back("const GritRuntime::PropertyAccessor *const " + accessor + " = GritRuntime::" + String(is_get ? "named_getter" : "named_setter") + arguments + ";");
	} else {
		r_scope.prelude.push_back("Object *const " + object + " = frame.instance ? GritRuntimeAccess::get_owner(frame.instance) : nullptr;");
		r_scope.prelude.push_back("const GritRuntime::PropertyAccessor *const " + accessor + " = " + object + " ? GritRuntime::" + String(is_get ? "member_getter" : "member_setter") + arguments + " : nullptr;");
	}

	Scope fast_scope;
	LocalVector<String> fast_core;
	if (is_get && is_object) {
		assign_value(target, "GritRuntime::read_object_property(*" + accessor + ", " + object + ")", Variant::NIL, fast_scope, fast_core);
	} else if (is_get) {
		fast_core.push_back("GritRuntime::read_property(*" + accessor + ", " + object + ", " + pointer_output(target, value_type, fast_scope) + ");");
	} else if (is_object) {
		if (is_named) {
			fast_core.push_back("GritRuntime::mark_edited(" + object + ");");
		}
		const String value_object = next_symbol("property_value");
		fast_scope.prelude.push_back("Object *" + value_object + " = GritRuntime::object_of(" + variant_value(p_statement.operands[is_named ? 1 : 0], fast_scope) + ");");
		fast_core.push_back("GritRuntime::write_property(*" + accessor + ", " + object + ", &" + value_object + ");");
	} else {
		if (is_named) {
			fast_core.push_back("GritRuntime::mark_edited(" + object + ");");
		}
		const String value = pointer_input(p_statement.operands[is_named ? 1 : 0], value_type, fast_scope);
		fast_core.push_back("GritRuntime::write_property(*" + accessor + ", " + object + ", " + value + ");");
	}
	Scope generic_scope;
	LocalVector<String> generic_core;
	write_generic_access(p_statement, generic_scope, generic_core);

	r_core.push_back("if (likely(" + accessor + " != nullptr)) {");
	nest_block(fast_scope, fast_core, r_core);
	r_core.push_back("} else {");
	nest_block(generic_scope, generic_core, r_core);
	r_core.push_back("}");
	return true;
}

bool GritCppEmitter::write_script_member_access(const GritStatement &p_statement, Scope &r_scope, LocalVector<String> &r_core) {
	const bool is_get = p_statement.opcode == GritOpcode::GET_NAMED;
	if ((!is_get && p_statement.opcode != GritOpcode::SET_NAMED) || p_statement.validated) {
		return false;
	}
	const GritOperand &base = p_statement.operands[0];
	if (base.kind != GritOperand::Kind::VARIABLE && base.kind != GritOperand::Kind::MEMBER) {
		return false;
	}
	const GritType &base_type = type_of(base.type);
	if (base_type.kind == GritType::Kind::BUILTIN) {
		return false;
	}
	if (base_type.kind != GritType::Kind::VARIANT) {
		const StringName &native_class = base_type.native_type;
		const StringName &name = p_statement.name;
		if (native_class == StringName() || !ClassDB::class_exists(native_class) || ClassDB::has_property(native_class, name) || ClassDB::has_method(native_class, name) || ClassDB::has_signal(native_class, name) || ClassDB::has_integer_constant(native_class, name)) {
			return false;
		}
	}
	const GritOperand &target = p_statement.target;
	if (is_get && is_lvalue(base) && base.kind == target.kind && base.index == target.index) {
		return false;
	}

	const String member = next_symbol("script_member");
	const String base_value = variant_value(base, r_scope);
	Scope fast_scope;
	LocalVector<String> fast_core;
	if (is_get) {
		r_scope.prelude.push_back("const Variant *const " + member + " = GritRuntime::readable_script_member(" + base_value + ", " + string_name(p_statement.name) + ");");
		assign_value(target, "*" + member, Variant::NIL, fast_scope, fast_core);
	} else {
		const String value = variant_value(p_statement.operands[1], r_scope);
		r_scope.prelude.push_back("Variant *const " + member + " = GritRuntime::writable_script_member(" + base_value + ", " + string_name(p_statement.name) + ", " + value + ");");
		fast_core.push_back("*" + member + " = " + value + ";");
	}
	Scope generic_scope;
	LocalVector<String> generic_core;
	write_generic_access(p_statement, generic_scope, generic_core);

	r_core.push_back("if (likely(" + member + " != nullptr)) {");
	nest_block(fast_scope, fast_core, r_core);
	r_core.push_back("} else {");
	nest_block(generic_scope, generic_core, r_core);
	r_core.push_back("}");
	return true;
}

void GritCppEmitter::write_generic_access(const GritStatement &p_statement, Scope &r_scope, LocalVector<String> &r_core) {
	const String line = itos(line_of(p_statement));
	const GritOperand &target = p_statement.target;

	switch (p_statement.opcode) {
		case GritOpcode::GET_KEYED: {
			const String source = variant_value(p_statement.operands[0], r_scope);
			const String key = variant_value(p_statement.operands[1], r_scope);
			const String result = variant_target(target, r_scope);
			const Variant::Type source_type = static_type_of(p_statement.operands[0]);
			if (p_statement.index == 1) {
				const String getter = add_static("Variant::ValidatedIndexedGetter const", "indexed_getter", "Variant::get_member_validated_indexed_getter(" + variant_type_name(source_type) + ")");
				r_core.push_back(checked("GritRuntime::get_indexed_validated(" + getter + ", " + source + ", " + key + ", " + result + ", frame, " + line + ")"));
			} else if (p_statement.index == 2) {
				const String getter = add_static("Variant::ValidatedKeyedGetter const", "keyed_getter", "Variant::get_member_validated_keyed_getter(" + variant_type_name(source_type) + ")");
				r_core.push_back(checked("GritRuntime::get_keyed_validated(" + getter + ", " + source + ", " + key + ", " + result + ", frame, " + line + ")"));
			} else {
				r_core.push_back(checked("GritRuntime::get_keyed(" + source + ", " + key + ", " + result + ", frame, " + line + ")"));
			}
		} break;
		case GritOpcode::SET_KEYED: {
			const String base = variant_base(p_statement.operands[0], r_scope);
			const String key = variant_value(p_statement.operands[1], r_scope);
			const String value = variant_value(p_statement.operands[2], r_scope);
			const Variant::Type base_type = static_type_of(p_statement.operands[0]);
			if (p_statement.index == 1) {
				const String setter = add_static("Variant::ValidatedIndexedSetter const", "indexed_setter", "Variant::get_member_validated_indexed_setter(" + variant_type_name(base_type) + ")");
				r_core.push_back(checked("GritRuntime::set_indexed_validated(" + setter + ", " + base + ", " + key + ", " + value + ", frame, " + line + ")"));
			} else if (p_statement.index == 2) {
				const String setter = add_static("Variant::ValidatedKeyedSetter const", "keyed_setter", "Variant::get_member_validated_keyed_setter(" + variant_type_name(base_type) + ")");
				r_core.push_back(checked("GritRuntime::set_keyed_validated(" + setter + ", " + base + ", " + key + ", " + value + ", frame, " + line + ")"));
			} else {
				r_core.push_back(checked("GritRuntime::set_keyed(" + base + ", " + key + ", " + value + ", frame, " + line + ")"));
			}
		} break;
		case GritOpcode::GET_NAMED: {
			const String source = variant_value(p_statement.operands[0], r_scope);
			const String result = variant_target(target, r_scope);
			if (p_statement.validated) {
				const Variant::Type source_type = static_type_of(p_statement.operands[0]);
				const String getter = add_static("Variant::ValidatedGetter const", "getter", "Variant::get_member_validated_getter(" + variant_type_name(source_type) + ", " + string_name(p_statement.name) + ")");
				r_core.push_back("GritRuntime::prepare(" + result + ", " + variant_type_name(Variant::get_member_type(source_type, p_statement.name)) + ");");
				r_core.push_back(getter + "(&" + source + ", &" + result + ");");
			} else {
				r_core.push_back(checked("GritRuntime::get_named(" + source + ", " + string_name(p_statement.name) + ", " + result + ", frame, " + line + ")"));
			}
		} break;
		case GritOpcode::SET_NAMED: {
			const String base = variant_base(p_statement.operands[0], r_scope);
			const String value = variant_value(p_statement.operands[1], r_scope);
			if (p_statement.validated) {
				const String setter = add_static("Variant::ValidatedSetter const", "setter", "Variant::get_member_validated_setter(" + variant_type_name(static_type_of(p_statement.operands[0])) + ", " + string_name(p_statement.name) + ")");
				r_core.push_back(setter + "(&" + base + ", &" + value + ");");
			} else {
				r_core.push_back(checked("GritRuntime::set_named(" + base + ", " + string_name(p_statement.name) + ", " + value + ", frame, " + line + ")"));
			}
		} break;
		case GritOpcode::GET_PROPERTY:
			r_core.push_back(checked("GritRuntime::get_property(" + string_name(p_statement.name) + ", " + variant_target(target, r_scope) + ", frame, " + line + ")"));
			break;
		case GritOpcode::SET_PROPERTY:
			r_core.push_back(checked("GritRuntime::set_property(" + string_name(p_statement.name) + ", " + variant_value(p_statement.operands[0], r_scope) + ", frame, " + line + ")"));
			break;
		case GritOpcode::GET_STATIC:
		case GritOpcode::SET_STATIC: {
			const bool is_get = p_statement.opcode == GritOpcode::GET_STATIC;
			const String class_value = variant_value(p_statement.operands[is_get ? 0 : 1], r_scope);
			const String variable = next_symbol("static_variable");
			r_core.push_back("Variant *" + variable + " = GritRuntime::static_variable(" + class_value + ", " + itos(p_statement.index) + ");");
			r_core.push_back("if (unlikely(!" + variable + ")) { " + error_return() + " }");
			if (is_get) {
				assign_value(target, "*" + variable, Variant::NIL, r_scope, r_core);
			} else {
				r_core.push_back("*" + variable + " = " + variant_value(p_statement.operands[0], r_scope) + ";");
			}
		} break;
		case GritOpcode::GET_GLOBAL:
			r_core.push_back(checked("GritRuntime::get_global(" + string_name(p_statement.name) + ", " + variant_target(target, r_scope) + ", frame, " + line + ")"));
			break;
		case GritOpcode::CONSTRUCT: {
			const String arguments = arguments_array(p_statement, 0, r_scope);
			const String result = variant_target(target, r_scope);
			if (p_statement.validated) {
				const String constructor = add_static("Variant::ValidatedConstructor const", "constructor", "Variant::get_validated_constructor(" + variant_type_name(p_statement.builtin_type) + ", " + itos(p_statement.index) + ")");
				r_core.push_back("GritRuntime::prepare(" + result + ", " + variant_type_name(p_statement.builtin_type) + ");");
				r_core.push_back(constructor + "(&" + result + ", " + arguments + ");");
			} else {
				r_core.push_back(checked("GritRuntime::construct(" + variant_type_name(p_statement.builtin_type) + ", " + arguments + ", " + itos(p_statement.operands.size()) + ", " + result + ", frame, " + line + ")"));
			}
		} break;
		case GritOpcode::CONSTRUCT_ARRAY: {
			const String arguments = arguments_array(p_statement, 0, r_scope);
			const String count = itos(p_statement.operands.size());
			if (p_statement.validated) {
				const GritType &element = type_of(p_statement.type);
				r_core.push_back(variant_target(target, r_scope) + " = GritRuntime::typed_array_literal(" + arguments + ", " + count + ", " + variant_type_name(element.builtin_type) + ", " + string_name(element.native_type) + ", " + script_value(element) + ");");
			} else {
				r_core.push_back(variant_target(target, r_scope) + " = GritRuntime::array_literal(" + arguments + ", " + count + ");");
			}
		} break;
		case GritOpcode::CONSTRUCT_DICTIONARY: {
			const String arguments = arguments_array(p_statement, 0, r_scope);
			const String pairs = itos(p_statement.operands.size() / 2);
			if (p_statement.validated) {
				const GritType &key = type_of(p_statement.type);
				const GritType &value = type_of(p_statement.secondary_type);
				r_core.push_back(variant_target(target, r_scope) + " = GritRuntime::typed_dictionary_literal(" + arguments + ", " + pairs + ", " + variant_type_name(key.builtin_type) + ", " + string_name(key.native_type) + ", " + script_value(key) + ", " + variant_type_name(value.builtin_type) + ", " + string_name(value.native_type) + ", " + script_value(value) + ");");
			} else {
				r_core.push_back(variant_target(target, r_scope) + " = GritRuntime::dictionary_literal(" + arguments + ", " + pairs + ");");
			}
		} break;
		default:
			break;
	}
}

bool GritCppEmitter::write_direct_call(const GritStatement &p_statement) {
	if (p_statement.opcode != GritOpcode::CALL || !direct_targets) {
		return false;
	}
	const String name = p_statement.name;
	if (name == "_ready" || name == "free") {
		return false;
	}
	const GritOperand &base = p_statement.operands[0];
	String class_path;
	String instance;
	bool is_static = false;
	switch (base.kind) {
		case GritOperand::Kind::SELF:
			class_path = function->class_path;
			instance = "frame.instance";
			break;
		case GritOperand::Kind::CLASS:
			class_path = function->class_path;
			is_static = true;
			break;
		case GritOperand::Kind::VARIABLE:
		case GritOperand::Kind::MEMBER: {
			const GritType &type = type_of(base.type);
			if (type.kind != GritType::Kind::SCRIPT || is_native(base)) {
				return false;
			}
			class_path = type.script;
			instance = "GritRuntime::gdscript_instance_of(" + lvalue(base) + ")";
		} break;
		default:
			return false;
	}
	const DirectTarget *target = direct_targets->getptr(class_path + "::" + name);
	if (!target) {
		return false;
	}
	const GritFunction &callee = *target->function;
	const int argument_count = int(p_statement.operands.size()) - 1;
	if (argument_count < callee.parameter_count - callee.optional_parameter_count || argument_count > callee.parameter_count) {
		return false;
	}

	Scope scope;
	guard_members(p_statement, scope);
	String arguments;
	String condition;
	int parameter_index = 0;
	for (uint32_t i = 0; i < callee.variables.size(); i++) {
		const GritVariable &parameter = callee.variables[i];
		if (parameter.role != GritVariable::Role::PARAMETER) {
			continue;
		}
		String value = default_value(parameter.storage);
		if (parameter_index < argument_count) {
			const GritOperand &operand = p_statement.operands[parameter_index + 1];
			const GritType &parameter_type = callee.types[parameter.type];
			if (parameter_type.kind != GritType::Kind::VARIANT && !(parameter_type == type_of(operand.type))) {
				return false;
			}
			if (parameter_type.kind == GritType::Kind::NATIVE || parameter_type.kind == GritType::Kind::SCRIPT) {
				condition += " && !GritRuntime::is_freed_object(" + variant_value(operand, scope) + ")";
			}
			if (parameter.storage == Variant::NIL) {
				value = variant_value(operand, scope);
			} else if (is_native(operand)) {
				value = converted(operand, parameter.storage);
			} else {
				value = convert_expression(variant_value(operand, scope), Variant::NIL, parameter.storage);
			}
		}
		arguments += ", " + value;
		parameter_index++;
	}

	const String line = itos(line_of(p_statement));
	const String callee_symbol = next_symbol("callee");
	const String instance_symbol = next_symbol("callee_instance");
	LocalVector<String> core;
	if (is_static) {
		core.push_back("GDScriptInstance *const " + instance_symbol + " = nullptr;");
		core.push_back("GDScriptFunction *const " + callee_symbol + " = GritRuntime::find_static_function(" + variant_value(base, scope) + ", " + string_name(p_statement.name) + ");");
	} else {
		core.push_back("GDScriptInstance *const " + instance_symbol + " = " + instance + ";");
		core.push_back("GDScriptFunction *const " + callee_symbol + " = " + instance_symbol + " ? GritRuntime::find_instance_function(" + instance_symbol + ", " + string_name(p_statement.name) + ") : nullptr;");
	}
	core.push_back("if (likely(" + callee_symbol + " != nullptr && " + callee_symbol + "->get_native_call() == &" + target->symbol + condition + ")) {");
	const bool has_target = p_statement.target.kind != GritOperand::Kind::NIL;
	const String result = next_symbol("direct_result");
	const String call = target->symbol + "_body(" + callee_symbol + ", " + instance_symbol + ", " + itos(argument_count) + arguments + ")";
	if (has_target) {
		core.push_back("\tVariant " + result + ";");
	}
	core.push_back("\t{");
	core.push_back("\t\tGritRuntime::DirectCall direct_call(" + callee_symbol + ", " + instance_symbol + ", " + String(is_static ? "false" : "true") + ");");
	if (has_target) {
		core.push_back("\t\t" + result + " = direct_call.entered() ? " + call + " : GritRuntimeAccess::get_default_return(" + callee_symbol + ");");
	} else {
		core.push_back("\t\tif (likely(direct_call.entered())) {");
		core.push_back("\t\t\tstatic_cast<void>(" + call + ");");
		core.push_back("\t\t}");
	}
	core.push_back("\t}");
	if (has_target) {
		core.push_back("\tif (unlikely(!GritRuntime::check_call_result(" + result + ", frame, " + line + "))) { " + error_return() + " }");
		LocalVector<String> assignment;
		assign_value(p_statement.target, "std::move(" + result + ")", Variant::NIL, scope, assignment);
		for (const String &statement : assignment) {
			core.push_back("\t" + statement);
		}
	}
	core.push_back("} else {");
	Scope generic_scope;
	LocalVector<String> generic_core;
	write_generic_call(p_statement, generic_scope, generic_core);
	nest_block(generic_scope, generic_core, core);
	core.push_back("}");
	write_scoped(scope, core);
	return true;
}

void GritCppEmitter::write_generic_call(const GritStatement &p_statement, Scope &r_scope, LocalVector<String> &r_core) {
	const bool is_async = p_statement.opcode == GritOpcode::CALL_ASYNC;
	const GritOperand &target = p_statement.target;
	const String base = variant_base(p_statement.operands[0], r_scope);
	const String arguments = arguments_array(p_statement, 1, r_scope);
	const String result = target.kind == GritOperand::Kind::NIL && !is_async ? String("nullptr") : "&" + variant_target(target, r_scope);
	const String argument_count = itos(int(p_statement.operands.size()) - 1);
	r_core.push_back(checked("GritRuntime::call(" + base + ", " + string_name(p_statement.name) + ", " + arguments + ", " + argument_count + ", " + result + ", " + String(is_async ? "true" : "false") + ", frame, " + itos(line_of(p_statement)) + ")"));
}

bool GritCppEmitter::write_type_of(const GritStatement &p_statement) {
	if (p_statement.opcode != GritOpcode::CALL_UTILITY || p_statement.name != StringName("typeof") || p_statement.operands.size() != 1) {
		return false;
	}
	const GritOperand &value = p_statement.operands[0];
	Scope scope;
	LocalVector<String> core;
	guard_members(p_statement, scope);
	String type;
	if (is_native(value)) {
		type = int_literal(storage_of(value));
	} else if (value.kind == GritOperand::Kind::CONSTANT) {
		type = int_literal(function->constant_types[value.index]);
	} else {
		type = "static_cast<int64_t>(" + variant_value(value, scope) + ".get_type())";
	}
	assign_value(p_statement.target, type, Variant::INT, scope, core);
	write_scoped(scope, core);
	return true;
}

void GritCppEmitter::write_call(const GritStatement &p_statement) {
	if (write_direct_call(p_statement) || write_type_of(p_statement)) {
		return;
	}
	Scope scope;
	LocalVector<String> core;
	guard_members(p_statement, scope);
	const String line = itos(line_of(p_statement));
	const GritOperand &target = p_statement.target;
	const bool has_base = p_statement.opcode == GritOpcode::CALL || p_statement.opcode == GritOpcode::CALL_ASYNC || p_statement.opcode == GritOpcode::CALL_BUILTIN || p_statement.opcode == GritOpcode::CALL_METHOD_BIND;
	const int first_argument = has_base ? 1 : 0;
	const String argument_count = itos(int(p_statement.operands.size()) - first_argument);

	GritStatement builtin_call;
	if (write_pointer_call(p_statement, scope, core) || (as_builtin_call(p_statement, builtin_call) && write_pointer_call(builtin_call, scope, core))) {
		write_scoped(scope, core);
		return;
	}

	switch (p_statement.opcode) {
		case GritOpcode::CALL:
		case GritOpcode::CALL_ASYNC:
			write_generic_call(p_statement, scope, core);
			break;
		case GritOpcode::CREATE_LAMBDA: {
			const String captures = arguments_array(p_statement, 0, scope);
			const String use_self = p_statement.validated ? "true" : "false";
			core.push_back(checked("GritRuntime::create_lambda(" + itos(p_statement.index) + ", " + captures + ", " + itos(p_statement.operands.size()) + ", " + use_self + ", " + variant_target(target, scope) + ", frame, " + line + ")"));
		} break;
		case GritOpcode::CALL_SUPER: {
			const String arguments = arguments_array(p_statement, 0, scope);
			core.push_back(checked("GritRuntime::call_super(" + string_name(p_statement.name) + ", " + arguments + ", " + argument_count + ", " + variant_target(target, scope) + ", frame, " + line + ")"));
		} break;
		case GritOpcode::CALL_UTILITY: {
			const String arguments = arguments_array(p_statement, 0, scope);
			const String result = variant_target(target, scope);
			if (p_statement.validated) {
				const Variant::Type return_type = Variant::has_utility_function_return_value(p_statement.name) ? Variant::get_utility_function_return_type(p_statement.name) : Variant::NIL;
				const String utility = add_static("Variant::ValidatedUtilityFunction const", "utility", "Variant::get_validated_utility_function(" + string_name(p_statement.name) + ")");
				core.push_back("GritRuntime::prepare(" + result + ", " + variant_type_name(return_type) + ");");
				core.push_back(utility + "(&" + result + ", " + arguments + ", " + argument_count + ");");
			} else {
				core.push_back(checked("GritRuntime::call_utility(" + string_name(p_statement.name) + ", " + arguments + ", " + argument_count + ", " + result + ", frame, " + line + ")"));
			}
		} break;
		case GritOpcode::CALL_GDSCRIPT_UTILITY: {
			const String arguments = arguments_array(p_statement, 0, scope);
			const String name = string_name(p_statement.name);
			const String utility = add_static("GDScriptUtilityFunctions::FunctionPtr const", "gdscript_utility", "GDScriptUtilityFunctions::get_function(" + name + ")");
			core.push_back(checked("GritRuntime::call_gdscript_utility(" + utility + ", " + name + ", " + arguments + ", " + argument_count + ", " + variant_target(target, scope) + ", frame, " + line + ")"));
		} break;
		case GritOpcode::CALL_BUILTIN:
		case GritOpcode::CALL_BUILTIN_STATIC: {
			const bool is_static = p_statement.opcode == GritOpcode::CALL_BUILTIN_STATIC;
			String base;
			if (is_static) {
				base = next_symbol("static_base");
				scope.prelude.push_back("Variant " + base + ";");
			} else {
				base = variant_base(p_statement.operands[0], scope);
			}
			const String arguments = arguments_array(p_statement, first_argument, scope);
			const String result = variant_target(target, scope);
			const String type = variant_type_name(p_statement.builtin_type);
			if (p_statement.validated) {
				const String method = add_static("Variant::ValidatedBuiltInMethod const", "builtin_method", "Variant::get_validated_builtin_method(" + type + ", " + string_name(p_statement.name) + ")");
				core.push_back("GritRuntime::prepare(" + result + ", " + variant_type_name(Variant::get_builtin_method_return_type(p_statement.builtin_type, p_statement.name)) + ");");
				core.push_back(method + "(&" + base + ", " + arguments + ", " + argument_count + ", &" + result + ");");
			} else {
				core.push_back(checked("GritRuntime::call_builtin_static(" + type + ", " + string_name(p_statement.name) + ", " + arguments + ", " + argument_count + ", " + result + ", frame, " + line + ")"));
			}
		} break;
		case GritOpcode::CALL_NATIVE_STATIC:
		case GritOpcode::CALL_METHOD_BIND: {
			const String method = add_static("MethodBind *const", "method", "ClassDB::get_method(" + string_name(p_statement.class_name) + ", " + string_name(p_statement.name) + ")");
			scope.prelude.push_back("if (unlikely(!" + method + ")) { GritRuntime::report_error(frame, " + line + ", " + string_literal(vformat("Grit: method \"%s.%s\" is not available.", p_statement.class_name, p_statement.name)) + "); " + error_return() + " }");
			if (p_statement.opcode == GritOpcode::CALL_NATIVE_STATIC) {
				const String arguments = arguments_array(p_statement, 0, scope);
				const String result = variant_target(target, scope);
				if (p_statement.validated) {
					core.push_back("GritRuntime::call_native_static_validated(" + method + ", " + arguments + ", " + result + ");");
				} else {
					core.push_back(checked("GritRuntime::call_native_static(" + method + ", " + arguments + ", " + argument_count + ", " + result + ", frame, " + line + ")"));
				}
			} else {
				const String base = variant_value(p_statement.operands[0], scope);
				const String arguments = arguments_array(p_statement, 1, scope);
				if (p_statement.validated) {
					core.push_back(checked("GritRuntime::call_method_bind_validated(" + method + ", " + base + ", " + arguments + ", " + variant_target(target, scope) + ", frame, " + line + ")"));
				} else {
					const String result = target.kind == GritOperand::Kind::NIL ? String("nullptr") : "&" + variant_target(target, scope);
					core.push_back(checked("GritRuntime::call_method_bind(" + method + ", " + base + ", " + arguments + ", " + argument_count + ", " + result + ", frame, " + line + ")"));
				}
			}
		} break;
		default:
			break;
	}
	write_scoped(scope, core);
}

bool GritCppEmitter::write_typed_for_each(const GritStatement &p_statement) {
	const GritOperand &container_operand = p_statement.operands[0];
	const Variant::Type container_type = static_type_of(container_operand);
	const GritOperand &target = p_statement.target;
	Variant::Type element_storage = Variant::NIL;
	switch (container_type) {
		case Variant::INT:
			element_storage = Variant::INT;
			break;
		case Variant::ARRAY:
			element_storage = Variant::NIL;
			break;
		default:
			element_storage = packed_element_storage(container_type);
			if (element_storage == Variant::NIL) {
				return false;
			}
			break;
	}
	const Variant::Type target_storage = storage_of(target);
	if (target.kind != GritOperand::Kind::VARIABLE || (container_type != Variant::ARRAY && target_storage != Variant::NIL && target_storage != element_storage)) {
		return false;
	}

	open_scope(String());
	Scope scope;
	const String index = next_symbol("index");
	String size;
	String element;
	if (container_type == Variant::INT) {
		size = is_native(container_operand) ? converted(container_operand, Variant::INT) : "GritRuntime::iteration_count(" + variant_value(container_operand, scope) + ")";
		element = index;
	} else {
		const String container = variant_value(container_operand, scope);
		const String accessor = container_type == Variant::ARRAY ? String("Array") : "Vector<" + packed_element_type(container_type) + ">";
		size = "GritRuntime::iteration_size<" + accessor + ">(" + container + ", " + variant_type_name(container_type) + ")";
		element = "GritRuntime::iteration_element<" + accessor + ">(" + container + ", " + index + ")";
		if (element_storage == Variant::INT || element_storage == Variant::FLOAT) {
			element = "static_cast<" + cpp_type(element_storage) + ">(" + element + ")";
		}
	}
	for (const String &prelude : scope.prelude) {
		write_line(prelude);
	}
	open_scope("for (int64_t " + index + " = 0; " + index + " < " + size + "; " + index + "++)");
	Scope assignment_scope;
	LocalVector<String> assignment;
	assign_value(target, element, element_storage, assignment_scope, assignment);
	write_scoped(assignment_scope, assignment);
	loops.push_back(Loop());
	open_scope(String());
	write_block(p_statement.blocks[0]);
	close_scope();
	loops.remove_at(loops.size() - 1);
	close_scope();
	for (const String &postlude : scope.postlude) {
		write_line(postlude);
	}
	close_scope();
	return true;
}

void GritCppEmitter::write_control(const GritStatement &p_statement) {
	const String line = itos(line_of(p_statement));
	switch (p_statement.opcode) {
		case GritOpcode::IF:
		case GritOpcode::IF_NOT_SHARED: {
			Scope scope;
			guard_members(p_statement, scope);
			String condition;
			if (p_statement.opcode == GritOpcode::IF) {
				condition = truth_value(p_statement.operands[0], scope);
			} else {
				condition = "!" + variant_value(p_statement.operands[0], scope) + ".is_shared()";
			}
			if (is_coroutine) {
				const String hoisted_condition = statement_states[&p_statement].first;
				LocalVector<String> core;
				core.push_back(hoisted_condition + " = " + condition + ";");
				write_scoped(scope, core);
				condition = hoisted_condition;
				scope.prelude.clear();
			}
			const bool wrap = !scope.prelude.is_empty();
			if (wrap) {
				open_scope(String());
				for (const String &prelude : scope.prelude) {
					write_line(prelude);
				}
			}
			open_scope("if (" + condition + ")");
			write_block(p_statement.blocks[0]);
			if (p_statement.blocks[1] >= 0 && !function->blocks[p_statement.blocks[1]].statements.is_empty()) {
				indentation--;
				write_line("} else {");
				indentation++;
				write_block(p_statement.blocks[1]);
			}
			close_scope();
			if (wrap) {
				close_scope();
			}
		} break;
		case GritOpcode::WHILE: {
			open_scope("for (;;)");
			write_block(p_statement.blocks[0]);
			Scope scope;
			guard_members(p_statement, scope);
			const String condition = truth_value(p_statement.operands[0], scope);
			LocalVector<String> core;
			core.push_back("if (!" + condition + ") { break; }");
			write_scoped(scope, core);
			loops.push_back(Loop());
			write_block(p_statement.blocks[1]);
			loops.remove_at(loops.size() - 1);
			close_scope();
		} break;
		case GritOpcode::FOR_RANGE: {
			const String counter = is_coroutine ? statement_states[&p_statement].first : next_symbol("range_counter");
			const String from = converted(p_statement.operands[0], Variant::INT);
			const String to = converted(p_statement.operands[1], Variant::INT);
			const String step = converted(p_statement.operands[2], Variant::INT);
			const String declaration = is_coroutine ? String() : String("int64_t ");
			open_scope("for (" + declaration + counter + " = " + from + "; GritRuntime::range_continues(" + counter + ", " + to + ", " + step + "); " + counter + " = GritRuntime::add_int(" + counter + ", " + step + "))");
			Scope scope;
			LocalVector<String> core;
			assign_value(p_statement.target, counter, Variant::INT, scope, core);
			write_scoped(scope, core);
			loops.push_back(Loop());
			write_block(p_statement.blocks[0]);
			loops.remove_at(loops.size() - 1);
			close_scope();
		} break;
		case GritOpcode::FOR_EACH: {
			if (is_coroutine) {
				const StatementState &state = statement_states[&p_statement];
				const GritOperand &container_operand = p_statement.operands[0];
				const GritOperand &target = p_statement.target;
				const String label = next_symbol("loop_continue");
				open_scope(String());
				String container = variable_name(container_operand.index);
				if (!state.third.is_empty()) {
					write_line(state.third + " = Variant(" + container + ");");
					container = state.third;
				}
				const String counter = variable_name(p_statement.operands[1].index);
				const String iterator = state.second.is_empty() ? variable_name(target.index) : state.second;
				write_line(state.first + " = false;");
				write_line(checked("GritRuntime::iterate_begin(" + container + ", " + counter + ", " + iterator + ", " + state.first + ", frame, " + line + ")"));
				open_scope("while (" + state.first + ")");
				if (!state.second.is_empty()) {
					write_line(variable_name(target.index) + " = " + take_expression(state.second, storage_of(target)) + ";");
				}
				Loop loop;
				loop.is_for_each = true;
				loop.continue_label = label;
				loops.push_back(loop);
				open_scope(String());
				write_block(p_statement.blocks[0]);
				close_scope();
				if (loops[loops.size() - 1].is_continue_used) {
					write_line(label + ":");
				}
				loops.remove_at(loops.size() - 1);
				write_line(checked("GritRuntime::iterate_next(" + container + ", " + counter + ", " + iterator + ", " + state.first + ", frame, " + line + ")"));
				close_scope();
				if (!state.second.is_empty()) {
					write_line(state.second + " = Variant();");
				}
				close_scope();
				break;
			}
			if (write_typed_for_each(p_statement)) {
				break;
			}
			open_scope(String());
			Scope scope;
			const String container = variant_value(p_statement.operands[0], scope);
			const String counter = variant_base(p_statement.operands[1], scope);
			const String iterator = variant_target(p_statement.target, scope);
			const String iterating = next_symbol("iterating");
			const String label = next_symbol("loop_continue");
			for (const String &prelude : scope.prelude) {
				write_line(prelude);
			}
			write_line("bool " + iterating + " = false;");
			write_line(checked("GritRuntime::iterate_begin(" + container + ", " + counter + ", " + iterator + ", " + iterating + ", frame, " + line + ")"));
			open_scope("while (" + iterating + ")");
			for (const String &postlude : scope.postlude) {
				write_line(postlude);
			}
			Loop loop;
			loop.is_for_each = true;
			loop.continue_label = label;
			loops.push_back(loop);
			open_scope(String());
			write_block(p_statement.blocks[0]);
			close_scope();
			if (loops[loops.size() - 1].is_continue_used) {
				write_line(label + ":");
			}
			loops.remove_at(loops.size() - 1);
			write_line(checked("GritRuntime::iterate_next(" + container + ", " + counter + ", " + iterator + ", " + iterating + ", frame, " + line + ")"));
			close_scope();
			close_scope();
		} break;
		case GritOpcode::BREAK:
			write_line("break;");
			break;
		case GritOpcode::CONTINUE:
			if (!loops.is_empty() && loops[loops.size() - 1].is_for_each) {
				loops[loops.size() - 1].is_continue_used = true;
				write_line("goto " + loops[loops.size() - 1].continue_label + ";");
			} else {
				write_line("continue;");
			}
			break;
		case GritOpcode::ASSERT: {
			write_line("#ifdef DEBUG_ENABLED");
			open_scope(String());
			write_block(p_statement.blocks[0]);
			Scope scope;
			guard_members(p_statement, scope);
			const String condition = truth_value(p_statement.operands[0], scope);
			const String message = variant_value(p_statement.operands[1], scope);
			LocalVector<String> core;
			core.push_back(checked("GritRuntime::check_assert(" + condition + ", " + message + ", " + String(p_statement.validated ? "true" : "false") + ", frame, " + line + ")"));
			write_scoped(scope, core);
			close_scope();
			write_line("#endif");
		} break;
		case GritOpcode::DEFAULT_ARGUMENT:
			open_scope("if (p_argcount <= " + itos(p_statement.index) + ")");
			write_block(p_statement.blocks[0]);
			close_scope();
			break;
		default:
			break;
	}
}

void GritCppEmitter::write_return(const GritStatement &p_statement) {
	Scope scope;
	LocalVector<String> core;
	guard_members(p_statement, scope);
	const String line = itos(line_of(p_statement));
	const GritOperand &value = p_statement.operands[0];
	const GritType &return_type = type_of(function->return_type);
	const Variant::Type storage = storage_of(value);

	if (!p_statement.validated || return_type.kind == GritType::Kind::VARIANT) {
		core.push_back("return " + (storage != Variant::NIL ? convert_expression(native_expression(value), storage, Variant::NIL) : variant_value(value, scope)) + ";");
		write_scoped(scope, core);
		return;
	}

	if (return_type.kind == GritType::Kind::BUILTIN && storage != Variant::NIL) {
		const Variant::Type type = return_type.builtin_type;
		if (storage == type || (is_numeric(storage) && is_numeric(type))) {
			core.push_back("return Variant(" + converted(value, type) + ");");
			write_scoped(scope, core);
			return;
		}
	}

	const String returned = next_symbol("return_value");
	scope.prelude.push_back("Variant " + returned + ";");
	switch (return_type.kind) {
		case GritType::Kind::BUILTIN: {
			const Variant::Type type = return_type.builtin_type;
			if (type == Variant::ARRAY && return_type.has_elements()) {
				const GritType &element = return_type.elements[0];
				core.push_back(checked("GritRuntime::return_typed_array(" + variant_value(value, scope) + ", " + variant_type_name(element.builtin_type) + ", " + string_name(element.native_type) + ", " + script_value(element) + ", " + returned + ", frame, " + line + ")"));
			} else if (type == Variant::DICTIONARY && return_type.has_elements()) {
				const GritType key = return_type.element(0);
				const GritType element = return_type.element(1);
				core.push_back(checked("GritRuntime::return_typed_dictionary(" + variant_value(value, scope) + ", " + variant_type_name(key.builtin_type) + ", " + string_name(key.native_type) + ", " + script_value(key) + ", " + variant_type_name(element.builtin_type) + ", " + string_name(element.native_type) + ", " + script_value(element) + ", " + returned + ", frame, " + line + ")"));
			} else {
				core.push_back("GritRuntime::return_typed_builtin(" + variant_value(value, scope) + ", " + variant_type_name(type) + ", " + returned + ", frame, " + line + ");");
			}
		} break;
		case GritType::Kind::NATIVE:
			core.push_back(checked("GritRuntime::return_typed_native(" + variant_value(value, scope) + ", " + string_name(return_type.native_type) + ", " + returned + ", frame, " + line + ")"));
			break;
		case GritType::Kind::SCRIPT:
			core.push_back(checked("GritRuntime::return_typed_script(" + variant_value(value, scope) + ", " + script_value(return_type) + ", " + returned + ", frame, " + line + ")"));
			break;
		case GritType::Kind::VARIANT:
			break;
	}
	core.push_back("return " + returned + ";");
	write_scoped(scope, core);
}

bool GritCppEmitter::is_dynamic_numeric_operator(Variant::Operator p_operator) {
	switch (p_operator) {
		case Variant::OP_ADD:
		case Variant::OP_SUBTRACT:
		case Variant::OP_MULTIPLY:
		case Variant::OP_DIVIDE:
		case Variant::OP_MODULE:
		case Variant::OP_EQUAL:
		case Variant::OP_NOT_EQUAL:
		case Variant::OP_LESS:
		case Variant::OP_LESS_EQUAL:
		case Variant::OP_GREATER:
		case Variant::OP_GREATER_EQUAL:
			return true;
		default:
			return false;
	}
}

bool GritCppEmitter::is_numeric(Variant::Type p_type) {
	return p_type == Variant::BOOL || p_type == Variant::INT || p_type == Variant::FLOAT;
}

String GritCppEmitter::packed_element_type(Variant::Type p_type) {
	switch (p_type) {
		case Variant::PACKED_BYTE_ARRAY:
			return "uint8_t";
		case Variant::PACKED_INT32_ARRAY:
			return "int32_t";
		case Variant::PACKED_INT64_ARRAY:
			return "int64_t";
		case Variant::PACKED_FLOAT32_ARRAY:
			return "float";
		case Variant::PACKED_FLOAT64_ARRAY:
			return "double";
		case Variant::PACKED_STRING_ARRAY:
			return "String";
		case Variant::PACKED_VECTOR2_ARRAY:
			return "Vector2";
		case Variant::PACKED_VECTOR3_ARRAY:
			return "Vector3";
		case Variant::PACKED_COLOR_ARRAY:
			return "Color";
		case Variant::PACKED_VECTOR4_ARRAY:
			return "Vector4";
		default:
			return String();
	}
}

Variant::Type GritCppEmitter::packed_element_storage(Variant::Type p_type) {
	switch (p_type) {
		case Variant::PACKED_BYTE_ARRAY:
		case Variant::PACKED_INT32_ARRAY:
		case Variant::PACKED_INT64_ARRAY:
			return Variant::INT;
		case Variant::PACKED_FLOAT32_ARRAY:
		case Variant::PACKED_FLOAT64_ARRAY:
			return Variant::FLOAT;
		case Variant::PACKED_STRING_ARRAY:
			return Variant::STRING;
		case Variant::PACKED_VECTOR2_ARRAY:
			return Variant::VECTOR2;
		case Variant::PACKED_VECTOR3_ARRAY:
			return Variant::VECTOR3;
		case Variant::PACKED_COLOR_ARRAY:
			return Variant::COLOR;
		case Variant::PACKED_VECTOR4_ARRAY:
			return Variant::VECTOR4;
		default:
			return Variant::NIL;
	}
}

String GritCppEmitter::cpp_type(Variant::Type p_storage) {
	switch (p_storage) {
		case Variant::BOOL:
			return "bool";
		case Variant::INT:
			return "int64_t";
		case Variant::FLOAT:
			return "double";
		case Variant::STRING:
			return "String";
		case Variant::VECTOR2:
			return "Vector2";
		case Variant::VECTOR2I:
			return "Vector2i";
		case Variant::RECT2:
			return "Rect2";
		case Variant::RECT2I:
			return "Rect2i";
		case Variant::VECTOR3:
			return "Vector3";
		case Variant::VECTOR3I:
			return "Vector3i";
		case Variant::TRANSFORM2D:
			return "Transform2D";
		case Variant::VECTOR4:
			return "Vector4";
		case Variant::VECTOR4I:
			return "Vector4i";
		case Variant::PLANE:
			return "Plane";
		case Variant::QUATERNION:
			return "Quaternion";
		case Variant::AABB:
			return "AABB";
		case Variant::BASIS:
			return "Basis";
		case Variant::TRANSFORM3D:
			return "Transform3D";
		case Variant::PROJECTION:
			return "Projection";
		case Variant::COLOR:
			return "Color";
		case Variant::STRING_NAME:
			return "StringName";
		case Variant::NODE_PATH:
			return "NodePath";
		case Variant::RID:
			return "RID";
		case Variant::CALLABLE:
			return "Callable";
		case Variant::SIGNAL:
			return "Signal";
		case Variant::DICTIONARY:
			return "Dictionary";
		case Variant::ARRAY:
			return "Array";
		case Variant::PACKED_BYTE_ARRAY:
			return "PackedByteArray";
		case Variant::PACKED_INT32_ARRAY:
			return "PackedInt32Array";
		case Variant::PACKED_INT64_ARRAY:
			return "PackedInt64Array";
		case Variant::PACKED_FLOAT32_ARRAY:
			return "PackedFloat32Array";
		case Variant::PACKED_FLOAT64_ARRAY:
			return "PackedFloat64Array";
		case Variant::PACKED_STRING_ARRAY:
			return "PackedStringArray";
		case Variant::PACKED_VECTOR2_ARRAY:
			return "PackedVector2Array";
		case Variant::PACKED_VECTOR3_ARRAY:
			return "PackedVector3Array";
		case Variant::PACKED_COLOR_ARRAY:
			return "PackedColorArray";
		case Variant::PACKED_VECTOR4_ARRAY:
			return "PackedVector4Array";
		default:
			return "Variant";
	}
}

String GritCppEmitter::default_value(Variant::Type p_storage) {
	switch (p_storage) {
		case Variant::BOOL:
			return "false";
		case Variant::INT:
			return "0";
		case Variant::FLOAT:
			return "0.0";
		default:
			return cpp_type(p_storage) + "()";
	}
}

String GritCppEmitter::variant_type_name(Variant::Type p_type) {
	return "static_cast<Variant::Type>(" + itos(p_type) + ")";
}

String GritCppEmitter::float_literal(double p_value) {
	if (Math::is_nan(p_value)) {
		return "Math::NaN";
	}
	if (Math::is_inf(p_value)) {
		return p_value > 0 ? "Math::INF" : "(-Math::INF)";
	}
	char buffer[64];
	snprintf(buffer, sizeof(buffer), "%a", p_value);
	const String literal = buffer;
	return literal.begins_with("-") ? "(" + literal + ")" : literal;
}

String GritCppEmitter::int_literal(int64_t p_value) {
	if (p_value == INT64_MIN) {
		return "INT64_MIN";
	}
	return "int64_t(" + itos(p_value) + ")";
}

String GritCppEmitter::convert_expression(const String &p_expression, Variant::Type p_from, Variant::Type p_to) {
	if (p_from == p_to) {
		return p_expression;
	}
	if (p_to == Variant::NIL) {
		return "Variant(" + p_expression + ")";
	}
	if (p_from == Variant::NIL) {
		return "GritRuntime::unbox<" + cpp_type(p_to) + ">(" + p_expression + ", " + variant_type_name(p_to) + ")";
	}
	if (is_numeric(p_from) && is_numeric(p_to)) {
		switch (p_to) {
			case Variant::BOOL:
				return p_from == Variant::FLOAT ? "(" + p_expression + " != 0.0)" : "(" + p_expression + " != 0)";
			case Variant::INT:
				if (p_from == Variant::FLOAT) {
					return "GritRuntime::float_to_int(" + p_expression + ")";
				}
				return "static_cast<int64_t>(" + p_expression + ")";
			default:
				return "static_cast<double>(" + p_expression + ")";
		}
	}
	return "GritRuntime::unbox<" + cpp_type(p_to) + ">(Variant(" + p_expression + "), " + variant_type_name(p_to) + ")";
}

String GritCppEmitter::sanitize_identifier(const String &p_name) {
	String identifier;
	for (int i = 0; i < p_name.length(); i++) {
		const char32_t character = p_name[i];
		if (is_ascii_alphanumeric_char(character) || character == '_') {
			identifier += character;
		}
	}
	return identifier;
}

String GritCppEmitter::emit_function(const GritFunction &p_function, const String &p_symbol, String &r_constants_symbol) {
	function = &p_function;
	body = StringBuilder();
	indentation = 1;
	symbol_count = 0;
	loops.clear();
	statics.clear();
	static_symbols.clear();
	constant_initializers.clear();
	constant_slots.clear();
	script_slots.clear();
	hoisted.clear();
	statement_states.clear();
	resume_cases.clear();
	is_coroutine = p_function.await_count > 0;
	collect_reads();
	if (is_coroutine) {
		collect_hoisted(0);
	}

	write_block(0);

	StringBuilder output;
	const auto line = [&output](const String &p_text) {
		output.append(p_text);
		output.append("\n");
	};

	const String parameters = "GDScriptFunction *p_function, GDScriptInstance *p_instance, const Variant **p_args, int p_argcount, Callable::CallError &r_error";
	const bool is_direct = is_direct_callable(p_function);
	const auto read_parameters = [&]() {
		const int required = p_function.parameter_count - p_function.optional_parameter_count;
		const String first_call = is_coroutine ? String("!p_state && ") : String();
		const String maximum = p_function.rest_variable >= 0 ? String("2147483647") : itos(p_function.parameter_count);
		line("\tif (" + first_call + "unlikely(!GritRuntime::check_argument_count(frame, p_argcount, " + itos(required) + ", " + maximum + ", r_error))) {");
		line("\t\treturn GritRuntime::default_return(frame);");
		line("\t}");
		int argument_index = 0;
		for (uint32_t i = 0; i < p_function.variables.size(); i++) {
			const GritVariable &variable = p_function.variables[i];
			if (variable.role != GritVariable::Role::PARAMETER) {
				continue;
			}
			const String name = variable_name(i);
			String read;
			if (variable.storage == Variant::NIL || is_numeric(variable.storage)) {
				read = "GritRuntime::read_argument(frame, p_args, " + itos(argument_index) + ", " + name + ", r_error)";
			} else {
				read = "GritRuntime::read_native_argument<" + cpp_type(variable.storage) + ">(frame, p_args, " + itos(argument_index) + ", " + variant_type_name(variable.storage) + ", true, " + name + ", r_error)";
			}
			const String checked_read = "if (unlikely(!" + read + ")) { return GritRuntime::default_return(frame); }";
			line("\t" + cpp_type(variable.storage) + " " + name + " = " + default_value(variable.storage) + ";");
			if (argument_index < required && !is_coroutine) {
				line("\t" + checked_read);
			} else if (argument_index < required) {
				line("\tif (!p_state) { " + checked_read + " }");
			} else {
				line("\tif (" + first_call + "p_argcount > " + itos(argument_index) + ") { " + checked_read + " }");
			}
			argument_index++;
		}
	};

	if (is_coroutine) {
		line("Variant " + p_symbol + "_body(" + parameters + ", GDScriptFunction::CallState *p_state, GritRuntime::CallScope &call_scope, bool &r_awaited) {");
	} else if (is_direct) {
		line(body_declaration(p_function, p_symbol) + " {");
	} else {
		line("Variant " + p_symbol + "(" + parameters + ", [[maybe_unused]] GDScriptFunction::CallState *p_state) {");
	}
	for (const String &declaration : statics) {
		line("\t" + declaration);
	}
	if (is_coroutine) {
		line("\tif (p_state) {");
		line("\t\tp_instance = p_state->instance;");
		line("\t}");
	}
	line(String(is_direct ? "\t[[maybe_unused]] " : "\t") + "const GritFrame frame = { p_function, p_instance };");
	if (!is_direct) {
		read_parameters();
	}

	line("\t[[maybe_unused]] Variant self_variant = GritRuntime::self_of(p_instance);");
	if (uses_class) {
		line("\tVariant class_variant = GritRuntime::class_of(frame);");
	}
	if (uses_members) {
		line("\tVariant *members = GritRuntime::members_of(p_instance);");
	}
	if (!constant_initializers.is_empty()) {
		line("\tconst Variant *constants = GritRuntimeAccess::get_native_constants(p_function);");
	}
	for (uint32_t i = 0; i < p_function.variables.size(); i++) {
		const GritVariable &variable = p_function.variables[i];
		if (variable.role == GritVariable::Role::PARAMETER) {
			continue;
		}
		const String attribute = variables_read[i] ? String() : String("[[maybe_unused]] ");
		line("\t" + attribute + cpp_type(variable.storage) + " " + variable_name(i) + " = " + default_value(variable.storage) + ";");
	}
	for (const HoistedState &state : hoisted) {
		line("\t" + cpp_type(state.storage) + " " + state.name + " = " + default_value(state.storage) + ";");
	}
	if (p_function.rest_variable >= 0) {
		const String rest = variable_name(p_function.rest_variable) + " = GritRuntime::rest_arguments(p_args, p_argcount, " + itos(p_function.parameter_count) + ");";
		line(is_coroutine ? "\tif (!p_state) { " + rest + " }" : "\t" + rest);
	}
	if (!is_coroutine) {
		line("\tGritRuntime::CallScope call_scope(p_function, p_instance, " + itos(p_function.initial_line) + ");");
	}
	if (is_coroutine) {
		LocalVector<String> restores;
		restore_slots("resumed_slots", restores);
		line("\tif (p_state) {");
		line("\t\tGritRuntime::ResumedSlots resumed_slots(p_state);");
		for (const String &restore : restores) {
			line("\t\t" + restore);
		}
		line("\t\tswitch (p_state->ip) {");
		for (const String &resume_case : resume_cases) {
			line("\t\t\t" + resume_case);
		}
		line("\t\t\tdefault:");
		line("\t\t\t\treturn GritRuntime::error_return(frame);");
		line("\t\t}");
		line("\t}");
	}
	output.append(body.as_string());
	line("\treturn Variant();");
	line("}");

	if (is_direct) {
		String arguments;
		for (uint32_t i = 0; i < p_function.variables.size(); i++) {
			if (p_function.variables[i].role == GritVariable::Role::PARAMETER) {
				arguments += ", std::move(" + variable_name(i) + ")";
			}
		}
		line("");
		line("Variant " + p_symbol + "(" + parameters + ", [[maybe_unused]] GDScriptFunction::CallState *p_state) {");
		line("\tconst GritFrame frame = { p_function, p_instance };");
		read_parameters();
		line("\treturn " + p_symbol + "_body(p_function, p_instance, p_argcount" + arguments + ");");
		line("}");
	}

	if (is_coroutine) {
		line("");
		line("Variant " + p_symbol + "(" + parameters + ", GDScriptFunction::CallState *p_state) {");
		line("\tGritRuntime::CallScope call_scope(p_function, p_state ? p_state->instance : p_instance, p_state ? p_state->line : " + itos(p_function.initial_line) + ");");
		line("\tbool awaited = false;");
		line("\tVariant result = " + p_symbol + "_body(p_function, p_instance, p_args, p_argcount, r_error, p_state, call_scope, awaited);");
		line("\tif (p_state && !awaited) {");
		line("\t\tGritRuntime::complete_resumed(p_state, result);");
		line("\t}");
		line("\treturn result;");
		line("}");
	}

	r_constants_symbol = String();
	if (!constant_initializers.is_empty()) {
		r_constants_symbol = p_symbol + "_constants";
		line("");
		line("void " + r_constants_symbol + "(const GDScript *p_script, Vector<Variant> &r_constants) {");
		line("\tr_constants.resize(" + itos(constant_initializers.size()) + ");");
		line("\tVariant *constants = r_constants.ptrw();");
		for (uint32_t i = 0; i < constant_initializers.size(); i++) {
			line("\tconstants[" + itos(i) + "] = " + constant_initializers[i] + ";");
		}
		line("}");
	}

	function = nullptr;
	return output.as_string();
}

bool GritCppEmitter::is_direct_callable(const GritFunction &p_function) {
	return p_function.await_count == 0 && p_function.rest_variable < 0;
}

String GritCppEmitter::entry_declaration(const String &p_symbol) {
	return "Variant " + p_symbol + "(GDScriptFunction *p_function, GDScriptInstance *p_instance, const Variant **p_args, int p_argcount, Callable::CallError &r_error, GDScriptFunction::CallState *p_state)";
}

String GritCppEmitter::body_declaration(const GritFunction &p_function, const String &p_symbol) {
	String declaration = "Variant " + p_symbol + "_body(GDScriptFunction *p_function, GDScriptInstance *p_instance, [[maybe_unused]] int p_argcount";
	for (uint32_t i = 0; i < p_function.variables.size(); i++) {
		const GritVariable &variable = p_function.variables[i];
		if (variable.role == GritVariable::Role::PARAMETER) {
			declaration += ", " + cpp_type(variable.storage) + " " + variable_name_in(p_function, i);
		}
	}
	return declaration + ")";
}

String GritCppEmitter::registration_symbol(const String &p_script_path) {
	return "grit_register_" + sanitize_identifier(p_script_path.trim_prefix("res://").replace("::", "_").replace("/", "_").replace(".", "_")) + "_" + p_script_path.md5_text().substr(0, 8);
}

String GritCppEmitter::emit_script_file(const String &p_script_path, const LocalVector<GritFunction> &p_functions, bool p_obfuscate) {
	StringBuilder file;
	file.append("#include \"grit/runtime/grit_registry.h\"\n");
	file.append("#include \"grit/runtime/grit_runtime.h\"\n\n");
	file.append("#include \"core/object/class_db.h\"\n");
	file.append("#include \"core/variant/variant_construct.h\"\n");
	file.append("#include \"core/variant/variant_op.h\"\n");
	file.append("#include \"core/variant/variant_setget.h\"\n\n");
	file.append("namespace {\n\n");

	Vector<String> symbols;
	HashMap<String, DirectTarget> direct_targets;
	for (const GritFunction &grit_function : p_functions) {
		const String symbol = "function_" + itos(symbols.size()) + "_" + sanitize_identifier(grit_function.name);
		symbols.push_back(symbol);
		file.append(entry_declaration(symbol) + ";\n");
		if (!is_direct_callable(grit_function)) {
			continue;
		}
		file.append(body_declaration(grit_function, symbol) + ";\n");
		const bool is_lambda = grit_function.registry_key.contains(">");
		if (!is_lambda && !grit_function.class_path.is_empty()) {
			DirectTarget target;
			target.symbol = symbol;
			target.function = &grit_function;
			direct_targets.insert(grit_function.class_path + "::" + String(grit_function.name), target);
		}
	}
	file.append("\n");

	Vector<String> constants_symbols;
	for (uint32_t i = 0; i < p_functions.size(); i++) {
		String constants_symbol;
		GritCppEmitter emitter;
		emitter.direct_targets = &direct_targets;
		file.append(emitter.emit_function(p_functions[i], symbols[i], constants_symbol));
		file.append("\n");
		constants_symbols.push_back(constants_symbol);
	}

	file.append("}\n\n");
	file.append("void " + registration_symbol(p_script_path) + "(GritRegistry &r_registry) {\n");
	for (uint32_t i = 0; i < p_functions.size(); i++) {
		const GritFunction &grit_function = p_functions[i];
		const String raw_key = grit_function.registry_key.is_empty() ? GritRegistry::make_key(grit_function.script_path, grit_function.name, grit_function.initial_line) : grit_function.registry_key;
		const String key = p_obfuscate ? GritRegistry::obfuscate_key(raw_key) : raw_key;
		const String constants = constants_symbols[i].is_empty() ? String("nullptr") : "&" + constants_symbols[i];
		file.append("\tr_registry.add_function(" + string_literal(GritPiper::get_path(key)) + ", " + string_literal(grit_function.signature) + ", &" + symbols[i] + ", " + constants + ", " + String(grit_function.is_stripped ? "true" : "false") + ");\n");
	}
	file.append("}\n");
	return file.as_string();
}

String GritCppEmitter::emit_registry_file(const String &p_fingerprint, const Vector<String> &p_script_paths, bool p_obfuscate) {
	StringBuilder file;
	file.append("#include \"grit/runtime/grit_registry.h\"\n\n");
	for (const String &script_path : p_script_paths) {
		file.append("void " + registration_symbol(script_path) + "(GritRegistry &r_registry);\n");
	}
	file.append("\nvoid grit_register_generated_functions(GritRegistry &r_registry) {\n");
	file.append("\tr_registry.set_fingerprint(" + string_literal(p_fingerprint) + ");\n");
	if (p_obfuscate) {
		file.append("\tr_registry.set_keys_obfuscated(true);\n");
	}
	for (const String &script_path : p_script_paths) {
		file.append("\t" + registration_symbol(script_path) + "(r_registry);\n");
	}
	file.append("}\n");
	return file.as_string();
}
