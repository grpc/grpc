COMPILE_DEFINITIONS
-------------------

Preprocessor definitions for compiling a target's sources.

The ``COMPILE_DEFINITIONS`` property may be set to a semicolon-separated
list of preprocessor definitions using the syntax ``VAR`` or ``VAR=value``.
Function-style definitions are not supported.  CMake will
automatically escape the value correctly for the native build system
(note that CMake language syntax may require escapes to specify some
values).

CMake will automatically drop some definitions that are not supported
by the native build tool.

.. include:: /include/COMPILE_DEFINITIONS_DISCLAIMER.txt

Contents of ``COMPILE_DEFINITIONS`` may use "generator expressions" with the
syntax ``$<...>``.  See the :manual:`cmake-generator-expressions(7)` manual
for available expressions.  See the :manual:`cmake-buildsystem(7)` manual
for more on defining buildsystem properties.

The corresponding :prop_tgt:`COMPILE_DEFINITIONS_<CONFIG>` property may
be set to specify per-configuration definitions.  Generator expressions
should be preferred instead of setting the alternative property.
