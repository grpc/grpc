CodeBlocks
----------

Generates CodeBlocks project files.

Project files for CodeBlocks will be created in the top directory and
in every subdirectory which features a ``CMakeLists.txt`` file containing
a :command:`project` call.  Additionally a hierarchy of makefiles is generated
into the build tree.
The :variable:`CMAKE_CODEBLOCKS_EXCLUDE_EXTERNAL_FILES` variable may
be set to ``ON`` to exclude any files which are located outside of
the project root directory.
The appropriate make program can build the
project through the default ``all`` target.  An ``install`` target is
also provided.

This "extra" generator may be specified as:

``CodeBlocks - MinGW Makefiles``
 Generate with :generator:`MinGW Makefiles`.

``CodeBlocks - NMake Makefiles``
 Generate with :generator:`NMake Makefiles`.

``CodeBlocks - NMake Makefiles JOM``
 Generate with :generator:`NMake Makefiles JOM`.

``CodeBlocks - Ninja``
 Generate with :generator:`Ninja`.

``CodeBlocks - Unix Makefiles``
 Generate with :generator:`Unix Makefiles`.
