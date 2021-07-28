COMPILE_FLAGS
-------------

Additional flags to use when compiling this target's sources.

The ``COMPILE_FLAGS`` property sets additional compiler flags used to
build sources within the target.  Use :prop_tgt:`COMPILE_DEFINITIONS`
to pass additional preprocessor definitions.

This property is deprecated.  Use the :prop_tgt:`COMPILE_OPTIONS`
property or the :command:`target_compile_options` command instead.
