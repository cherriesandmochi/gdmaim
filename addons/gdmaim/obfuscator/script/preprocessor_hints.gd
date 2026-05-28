extends RefCounted

const PRESERVE_ANNOTATION : String = "PRESERVE_ANNOTATION"
const LOCK_SYMBOLS : String = "LOCK_SYMBOLS"
const OBFUSCATE_STRINGS : String = "OBFUSCATE_STRINGS"
const OBFUSCATE_STRING_PARAMETERS : String = "OBFUSCATE_STRING_PARAMETERS" # param0, param1, ...
const FEATURE_FUNC : String = "FEATURE_FUNC" # feature_tag #TODO

# FILES HINT
const EXCLUDE_HINT : String = "EXCLUDE_FILE" # prevent obfuscate the script and maintains compatibility with external uses.
const STRIP_STATIC_TYPED_HINT : String = "STRIP_STATIC_TYPED_FILE" # Strip typed definitions in variables, functions and arrays of the file.
const STRIP_STATIC_TYPED_INITIALIZED_HINT : String = "STRIP_STATIC_TYPED_FILE_INITIALIZED"
const STRIP_IGNORE_STATIC_TYPED_HINT : String = "STRIP_STATIC_TYPED_FILE_IGNORE"
const OBFUSCATE_STRINGS_SEED : String = "OBFUSCATE_STRINGS_SEED"
