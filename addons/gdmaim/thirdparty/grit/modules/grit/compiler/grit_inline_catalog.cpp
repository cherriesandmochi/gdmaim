#include "grit_inline_catalog.h"

#include "core/variant/variant_construct.h"
#include "core/variant/variant_op.h"
#include "core/variant/variant_setget.h"

namespace {

struct OperatorEntry {
	Variant::PTROperatorEvaluator evaluator;
	const char *name;
};

struct MemberEntry {
	Variant::PTRGetter getter;
	Variant::PTRSetter setter;
	const char *name;
};

struct ConstructorEntry {
	Variant::PTRConstructor constructor;
	const char *name;
};

#include "grit_inline_catalog.gen.inc"

}

String GritInlineCatalog::operator_evaluator(Variant::PTROperatorEvaluator p_evaluator) {
	if (!p_evaluator) {
		return String();
	}
	for (const OperatorEntry &entry : operator_entries) {
		if (entry.evaluator == p_evaluator) {
			return entry.name;
		}
	}
	return String();
}

String GritInlineCatalog::member_accessor(Variant::PTRGetter p_getter, Variant::PTRSetter p_setter) {
	if (!p_getter && !p_setter) {
		return String();
	}
	for (const MemberEntry &entry : member_entries) {
		if ((!p_getter || entry.getter == p_getter) && (!p_setter || entry.setter == p_setter)) {
			return entry.name;
		}
	}
	return String();
}

String GritInlineCatalog::constructor(Variant::PTRConstructor p_constructor) {
	if (!p_constructor) {
		return String();
	}
	for (const ConstructorEntry &entry : constructor_entries) {
		if (entry.constructor == p_constructor) {
			return entry.name;
		}
	}
	return String();
}
