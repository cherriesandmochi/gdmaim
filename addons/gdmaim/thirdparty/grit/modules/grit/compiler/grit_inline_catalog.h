#pragma once

#include "core/variant/variant.h"

namespace GritInlineCatalog {

String operator_evaluator(Variant::PTROperatorEvaluator p_evaluator);
String member_accessor(Variant::PTRGetter p_getter, Variant::PTRSetter p_setter);
String constructor(Variant::PTRConstructor p_constructor);

}
