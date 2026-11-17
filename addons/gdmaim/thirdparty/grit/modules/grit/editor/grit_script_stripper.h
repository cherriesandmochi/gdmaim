#pragma once

#include "modules/gdscript/gdscript_parser.h"

#include "core/string/ustring.h"
#include "core/templates/hash_set.h"

class GritScriptStripper {
	const HashSet<String> *strippable = nullptr;
	Vector<String> lines;
	HashSet<String> stripped;

	void strip_class(const GDScriptParser::ClassNode *p_class, const String &p_class_path);
	void strip_function(const GDScriptParser::FunctionNode *p_function, const String &p_id, const String &p_stub);
	void strip_accessor(const GDScriptParser::FunctionNode *p_accessor, const String &p_class_path, const String &p_stub);
	String node_text(const GDScriptParser::Node *p_node) const;
	String stub(const GDScriptParser::DataType &p_type, const GDScriptParser::Node *p_type_node) const;
	void set_line(int p_line, const String &p_text);

public:
	static String function_id(const String &p_class_path, const StringName &p_name);

	Error strip(const String &p_path, const String &p_source, const HashSet<String> &p_strippable, String &r_stripped);
	const HashSet<String> &get_stripped() const { return stripped; }
};
