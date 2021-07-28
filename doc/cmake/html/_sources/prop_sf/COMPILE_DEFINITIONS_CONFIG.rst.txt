COMPILE_DEFINITIONS_<CONFIG>
----------------------------

Ignored.  See CMake Policy :policy:`CMP0043`.

Per-configuration preprocessor definitions on a source file.

This is the configuration-specific version of :prop_tgt:`COMPILE_DEFINITIONS`.
Note that :generator:`Xcode` does not support per-configuration source
file flags so this property will be ignored by the :generator:`Xcode` generator.
