CodeLite
----------

Generates CodeLite project files.

Project files for CodeLite will be created in the top directory and
in every subdirectory which features a CMakeLists.txt file containing
a :command:`project` call.
The :variable:`CMAKE_CODELITE_USE_TARGETS` variable may be set to ``ON``
to change the default behavior from projects to targets as the basis
for project files.
The appropriate make program can build the
project through the default ``all`` target.  An ``install`` target
is also provided.

This "extra" generator may be specified as:

``CodeLite - MinGW Makefiles``
 Generate with :generator:`MinGW Makefiles`.

``CodeLite - NMake Makefiles``
 Generate with :generator:`NMake Makefiles`.

``CodeLite - Ninja``
 Generate with :generator:`Ninja`.

``CodeLite - Unix Makefiles``
 Generate with :generator:`Unix Makefiles`.
