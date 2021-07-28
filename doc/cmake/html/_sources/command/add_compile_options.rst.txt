add_compile_options
-------------------

Add options to the compilation of source files.

.. code-block:: cmake

  add_compile_options(<option> ...)

Adds options to the :prop_dir:`COMPILE_OPTIONS` directory property.
These options are used when compiling targets from the current
directory and below.

Arguments
^^^^^^^^^

Arguments to ``add_compile_options`` may use "generator expressions" with
the syntax ``$<...>``.  See the :manual:`cmake-generator-expressions(7)`
manual for available expressions.  See the :manual:`cmake-buildsystem(7)`
manual for more on defining buildsystem properties.

.. include:: OPTIONS_SHELL.txt

Example
^^^^^^^

Since different compilers support different options, a typical use of
this command is in a compiler-specific conditional clause:

.. code-block:: cmake

  if (MSVC)
      # warning level 4 and all warnings as errors
      add_compile_options(/W4 /WX)
  else()
      # lots of warnings and all warnings as errors
      add_compile_options(-Wall -Wextra -pedantic -Werror)
  endif()

See Also
^^^^^^^^

This command can be used to add any options. However, for
adding preprocessor definitions and include directories it is recommended
to use the more specific commands :command:`add_compile_definitions`
and :command:`include_directories`.

The command :command:`target_compile_options` adds target-specific options.

The source file property :prop_sf:`COMPILE_OPTIONS` adds options to one
source file.
