BUILD_SHARED_LIBS
-----------------

Global flag to cause :command:`add_library` to create shared libraries if on.

If present and true, this will cause all libraries to be built shared
unless the library was explicitly added as a static library.  This
variable is often added to projects as an :command:`option` so that each user
of a project can decide if they want to build the project using shared or
static libraries.
