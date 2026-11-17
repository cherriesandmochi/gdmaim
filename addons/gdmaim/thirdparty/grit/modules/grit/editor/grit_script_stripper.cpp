#include "grit_script_stripper.h"

#include "modules/gdscript/gdscript_analyzer.h"

String GritScriptStripper::function_id(const String &p_class_path, const StringName &p_name) {
	return p_class_path + "::" + String(p_name);
}

void GritScriptStripper::set_line(int p_line, const String &p_text) {
	const String &original = lines[p_line - 1];
	lines.write[p_line - 1] = original.ends_with("\r") ? p_text + "\r" : p_text;
}

String GritScriptStripper::node_text(const GDScriptParser::Node *p_node) const {
	if (!p_node || p_node->start_line != p_node->end_line || p_node->start_line < 1 || p_node->start_line > lines.size()) {
		return String();
	}
	const String &line = lines[p_node->start_line - 1];
	return line.substr(p_node->start_column - 1, p_node->end_column - p_node->start_column).strip_edges();
}

String GritScriptStripper::stub(const GDScriptParser::DataType &p_type, const GDScriptParser::Node *p_type_node) const {
	switch (p_type.kind) {
		case GDScriptParser::DataType::BUILTIN:
			switch (p_type.builtin_type) {
				case Variant::NIL:
					return "pass";
				case Variant::ARRAY:
					return "return []";
				case Variant::DICTIONARY:
					return "return {}";
				default:
					return "return " + Variant::get_type_name(p_type.builtin_type) + "()";
			}
		case GDScriptParser::DataType::ENUM: {
			const String type_text = node_text(p_type_node);
			return type_text.is_empty() ? String() : "return 0 as " + type_text;
		}
		case GDScriptParser::DataType::NATIVE:
		case GDScriptParser::DataType::SCRIPT:
		case GDScriptParser::DataType::CLASS:
		case GDScriptParser::DataType::VARIANT:
			return "return null";
		default:
			return String();
	}
}

void GritScriptStripper::strip_function(const GDScriptParser::FunctionNode *p_function, const String &p_id, const String &p_stub) {
	const GDScriptParser::SuiteNode *body = p_function->body;
	if (!body || body->statements.is_empty() || body->start_line < 1 || body->end_line > lines.size() || body->start_line > body->end_line) {
		return;
	}
	const String &first_line = lines[body->start_line - 1];
	const int body_start = body->start_column - 1;
	if (body_start < 0 || body_start > first_line.length() || !first_line.substr(0, body_start).strip_edges().is_empty()) {
		return;
	}
	if (p_stub.is_empty()) {
		return;
	}

	set_line(body->start_line, first_line.substr(0, body_start) + p_stub);
	for (int line = body->start_line + 1; line <= body->end_line; line++) {
		set_line(line, String());
	}
	stripped.insert(p_id);
}

void GritScriptStripper::strip_accessor(const GDScriptParser::FunctionNode *p_accessor, const String &p_class_path, const String &p_stub) {
	if (!p_accessor || !p_accessor->identifier) {
		return;
	}
	const String id = function_id(p_class_path, p_accessor->identifier->name);
	if (strippable->has(id)) {
		strip_function(p_accessor, id, p_stub);
	}
}

void GritScriptStripper::strip_class(const GDScriptParser::ClassNode *p_class, const String &p_class_path) {
	for (const GDScriptParser::ClassNode::Member &member : p_class->members) {
		switch (member.type) {
			case GDScriptParser::ClassNode::Member::FUNCTION: {
				const String id = function_id(p_class_path, member.function->identifier->name);
				if (strippable->has(id)) {
					const GDScriptParser::TypeNode *return_type = member.function->return_type;
					strip_function(member.function, id, return_type ? stub(return_type->get_datatype(), return_type) : String("pass"));
				}
			} break;
			case GDScriptParser::ClassNode::Member::VARIABLE: {
				const GDScriptParser::VariableNode *variable = member.variable;
				if (variable->property == GDScriptParser::VariableNode::PROP_INLINE) {
					strip_accessor(variable->setter, p_class_path, "pass");
					strip_accessor(variable->getter, p_class_path, stub(variable->get_datatype(), variable->datatype_specifier));
				}
			} break;
			case GDScriptParser::ClassNode::Member::CLASS:
				strip_class(member.m_class, p_class_path + "::" + String(member.m_class->identifier->name));
				break;
			default:
				break;
		}
	}
}

Error GritScriptStripper::strip(const String &p_path, const String &p_source, const HashSet<String> &p_strippable, String &r_stripped) {
	strippable = &p_strippable;
	stripped.clear();
	lines = p_source.split("\n");

	GDScriptParser parser;
	Error error = parser.parse(p_source, p_path, false);
	if (error != OK) {
		return error;
	}
	GDScriptAnalyzer analyzer(&parser);
	error = analyzer.analyze();
	if (error != OK) {
		return error;
	}

	strip_class(parser.get_tree(), p_path);
	r_stripped = String("\n").join(lines);
	return OK;
}
