Sublime Text 2
--------------

Generates Sublime Text 2 project files.

Project files for Sublime Text 2 will be created in the top directory
and in every subdirectory which features a ``CMakeLists.txt`` file
containing a :command:`project` call.  Additionally ``Makefiles``
(or ``build.ninja`` files) are generated into the build tree.
The appropriate make program can build the project through the default ``all``
target.  An ``install`` target is also provided.

This "extra" generator may be specified as:

``Sublime Text 2 - MinGW Makefiles``
 Generate with :generator:`MinGW Makefiles`.

``Sublime Text 2 - NMake Makefiles``
 Generate with :generator:`NMake Makefiles`.

``Sublime Text 2 - Ninja``
 Generate with :generator:`Ninja`.

``Sublime Text 2 - Unix Makefiles``
 Generate with :generator:`Unix Makefiles`.
