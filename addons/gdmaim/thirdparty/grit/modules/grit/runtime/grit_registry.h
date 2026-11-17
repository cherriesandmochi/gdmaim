#pragma once

#include "modules/gdscript/gdscript_function.h"

class GDScript;

#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"
#include "core/templates/safe_refcount.h"

class GritRegistry {
public:
	static constexpr const char *STRIPPED_FLAG = "stripped";

	typedef void (*ConstantsFactory)(const GDScript *p_script, Vector<Variant> &r_constants);

	struct NativeFunction {
		String signature;
		GDScriptFunction::NativeCall call = nullptr;
		ConstantsFactory constants = nullptr;
		bool is_stripped = false;
	};

private:

	static GritRegistry *singleton;

	HashMap<String, NativeFunction> functions;
	HashSet<String> ambiguous_keys;
	String fingerprint;
	bool active = false;
	bool keys_obfuscated = false;
	mutable SafeNumeric<uint64_t> attached_count;

	bool matches_project() const;

public:
	static GritRegistry *get_singleton() { return singleton; }

	static String make_class_path(const GDScript *p_script);
	static String make_key(const String &p_script_path, const StringName &p_function_name, int p_line);
	static String make_lambda_key(const String &p_root_key, const StringName &p_function_name, int p_line, int p_ordinal);
	static String get_fingerprint_path();
	static String make_pack_fingerprint(const String &p_fingerprint, bool p_is_stripped);
	static String obfuscate_key(const String &p_key);

	void set_fingerprint(const String &p_fingerprint);
	void set_keys_obfuscated(bool p_obfuscated) { keys_obfuscated = p_obfuscated; }
	void add_function(const String &p_key, const String &p_signature, GDScriptFunction::NativeCall p_call, ConstantsFactory p_constants = nullptr, bool p_is_stripped = false);
	const NativeFunction *find(const String &p_key, const String &p_signature) const;
	bool is_stripped(const String &p_key) const;

	void activate();
	bool is_active() const { return active; }
	int get_function_count() const { return functions.size(); }
	uint64_t get_attached_count() const { return attached_count.get(); }

	GritRegistry();
	~GritRegistry();
};

void grit_register_generated_functions(GritRegistry &r_registry);
