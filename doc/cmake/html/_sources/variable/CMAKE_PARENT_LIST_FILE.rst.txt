CMAKE_PARENT_LIST_FILE
----------------------

Full path to the CMake file that included the current one.

While processing a CMake file loaded by :command:`include` or
:command:`find_package` this variable contains the full path to the file
including it.  The top of the include stack is always the ``CMakeLists.txt``
for the current directory.  See also :variable:`CMAKE_CURRENT_LIST_FILE`.
