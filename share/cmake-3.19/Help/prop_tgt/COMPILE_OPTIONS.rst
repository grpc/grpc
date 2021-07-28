COMPILE_OPTIONS
---------------

List of options to pass to the compiler.

This property holds a :ref:`semicolon-separated list <CMake Language Lists>` of options
specified so far for its target.  Use the :command:`target_compile_options`
command to append more options.

This property is initialized by the :prop_dir:`COMPILE_OPTIONS` directory
property when a target is created, and is used by the generators to set
the options for the compiler.

Contents of ``COMPILE_OPTIONS`` may use "generator expressions" with the
syntax ``$<...>``.  See the :manual:`cmake-generator-expressions(7)` manual
for available expressions.  See the :manual:`cmake-buildsystem(7)` manual
for more on defining buildsystem properties.
