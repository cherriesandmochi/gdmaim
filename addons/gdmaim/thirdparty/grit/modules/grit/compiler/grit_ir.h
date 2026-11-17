#pragma once

#include "modules/gdscript/gdscript_function.h"

#include "core/string/ustring.h"
#include "core/templates/local_vector.h"
#include "core/variant/variant.h"

struct GritType {
	enum class Kind {
		VARIANT,
		BUILTIN,
		NATIVE,
		SCRIPT,
	};

	Kind kind = Kind::VARIANT;
	Variant::Type builtin_type = Variant::NIL;
	StringName native_type;
	String script;
	LocalVector<GritType> elements;

	static GritType from(const GDScriptDataType &p_type);
	static String script_reference(const Script *p_script);

	GritType element(int p_index) const;
	bool has_elements() const { return !elements.is_empty(); }
	bool can_contain_object() const;
	bool is_referencable() const;
	bool operator==(const GritType &p_other) const;
};

_FORCE_INLINE_ bool grit_is_unboxed_type(Variant::Type p_type) {
	switch (p_type) {
		case Variant::NIL:
		case Variant::OBJECT:
		case Variant::DICTIONARY:
		case Variant::ARRAY:
		case Variant::PACKED_BYTE_ARRAY:
		case Variant::PACKED_INT32_ARRAY:
		case Variant::PACKED_INT64_ARRAY:
		case Variant::PACKED_FLOAT32_ARRAY:
		case Variant::PACKED_FLOAT64_ARRAY:
		case Variant::PACKED_STRING_ARRAY:
		case Variant::PACKED_VECTOR2_ARRAY:
		case Variant::PACKED_VECTOR3_ARRAY:
		case Variant::PACKED_COLOR_ARRAY:
		case Variant::PACKED_VECTOR4_ARRAY:
		case Variant::VARIANT_MAX:
			return false;
		default:
			return true;
	}
}

_FORCE_INLINE_ bool grit_is_pointer_type(Variant::Type p_type) {
	return p_type != Variant::OBJECT && p_type < Variant::VARIANT_MAX;
}

struct GritVariable {
	enum class Role {
		PARAMETER,
		LOCAL,
		TEMPORARY,
	};

	Role role = Role::LOCAL;
	Variant::Type storage = Variant::NIL;
	int type = 0;
	String name;
};

struct GritOperand {
	enum class Kind {
		NIL,
		VARIABLE,
		CONSTANT,
		SELF,
		CLASS,
		MEMBER,
	};

	Kind kind = Kind::NIL;
	int index = -1;
	int type = 0;
};

enum class GritOpcode {
	ASSIGN,
	ASSIGN_TYPED,
	TEST,
	CLEAR,
	UNARY,
	BINARY,
	TYPE_TEST,
	CAST,
	GET_KEYED,
	SET_KEYED,
	GET_NAMED,
	SET_NAMED,
	GET_PROPERTY,
	SET_PROPERTY,
	GET_STATIC,
	SET_STATIC,
	GET_GLOBAL,
	CONSTRUCT,
	CONSTRUCT_ARRAY,
	CONSTRUCT_DICTIONARY,
	CALL,
	CALL_ASYNC,
	CALL_SUPER,
	CALL_UTILITY,
	CALL_GDSCRIPT_UTILITY,
	CALL_BUILTIN,
	CALL_BUILTIN_STATIC,
	CALL_NATIVE_STATIC,
	CALL_METHOD_BIND,
	CREATE_LAMBDA,
	AWAIT,
	IF,
	IF_NOT_SHARED,
	WHILE,
	FOR_RANGE,
	FOR_EACH,
	BREAK,
	CONTINUE,
	RETURN,
	ASSERT,
	DEFAULT_ARGUMENT,
};

struct GritStatement {
	GritOpcode opcode = GritOpcode::ASSIGN;
	GritOperand target;
	LocalVector<GritOperand> operands;
	Variant::Operator op = Variant::OP_MAX;
	StringName name;
	StringName class_name;
	Variant::Type builtin_type = Variant::NIL;
	int type = 0;
	int secondary_type = 0;
	int index = -1;
	bool validated = false;
	int blocks[2] = { -1, -1 };
	int line = 0;
};

struct GritBlock {
	LocalVector<GritStatement> statements;
};

struct GritFunction {
	String script_path;
	StringName name;
	String registry_key;
	String class_path;
	StringName native_base;
	String signature;
	int initial_line = 0;
	int return_type = 0;
	int parameter_count = 0;
	int optional_parameter_count = 0;
	int await_count = 0;
	int rest_variable = -1;
	bool is_stripped = false;
	LocalVector<GritType> types;
	LocalVector<GritVariable> variables;
	LocalVector<Variant> constants;
	LocalVector<Variant::Type> constant_types;
	LocalVector<String> constant_initializers;
	LocalVector<GritBlock> blocks;
	String unsupported_reason;

	bool is_supported() const { return unsupported_reason.is_empty(); }
};
