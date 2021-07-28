Eclipse CDT4
------------

Generates Eclipse CDT 4.0 project files.

Project files for Eclipse will be created in the top directory.  In
out of source builds, a linked resource to the top level source
directory will be created.  Additionally a hierarchy of makefiles is
generated into the build tree.  The appropriate make program can build
the project through the default ``all`` target.  An ``install`` target
is also provided.

This "extra" generator may be specified as:

``Eclipse CDT4 - MinGW Makefiles``
 Generate with :generator:`MinGW Makefiles`.

``Eclipse CDT4 - NMake Makefiles``
 Generate with :generator:`NMake Makefiles`.

``Eclipse CDT4 - Ninja``
 Generate with :generator:`Ninja`.

``Eclipse CDT4 - Unix Makefiles``
 Generate with :generator:`Unix Makefiles`.
