#pragma once

#include "tests/test_macros.h"

#ifdef TOOLS_ENABLED

#include "grit/compiler/grit_recording_code_generator.h"
#include "grit/compiler/grit_source_writer.h"
#include "grit/runtime/grit_registry.h"

#include "modules/gdscript/tests/gdscript_test_runner.h"

#include "core/os/os.h"
#include "core/templates/hash_set.h"

namespace TestGrit {

TEST_CASE("[Modules][Grit] Generate conformance code" * doctest::skip()) {
	const String output_directory = OS::get_singleton()->get_environment("GRIT_CONFORMANCE_OUTPUT");
	REQUIRE_MESSAGE(!output_directory.is_empty(), "Set GRIT_CONFORMANCE_OUTPUT to the directory receiving the generated code.");

	GritSourceWriter writer;
	HashSet<String> seen_functions;
	HashMap<String, int> rejection_counts;
	{
		GritScriptRecording recording(nullptr);
		GDScriptTests::GDScriptTestRunner runner("modules/gdscript/tests/scripts", true);
		runner.run_tests();

		for (const GritFunction &function : recording.functions) {
			const String &key = function.registry_key;
			if (seen_functions.has(key)) {
				continue;
			}
			seen_functions.insert(key);
			if (function.is_supported()) {
				writer.add_function(function);
			} else {
				const String reason = function.unsupported_reason.get_slicec('(', 0).strip_edges();
				rejection_counts[reason] = rejection_counts.has(reason) ? rejection_counts[reason] + 1 : 1;
			}
		}
	}

	print_line(vformat("Grit conformance: %d of %d functions compiled natively.", writer.get_function_count(), seen_functions.size()));
	for (const KeyValue<String, int> &entry : rejection_counts) {
		print_line(vformat("Grit rejection: %5d  %s", entry.value, entry.key));
	}
	CHECK(writer.write(output_directory, "conformance", false) == OK);
}

TEST_CASE("[Modules][Grit] Native coverage") {
	const GritRegistry *registry = GritRegistry::get_singleton();
	REQUIRE(registry != nullptr);
	print_line(vformat("Grit coverage: %d native functions registered, %d attached.", registry->get_function_count(), registry->get_attached_count()));
}

}

#endif
