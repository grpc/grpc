CMAKE_FIND_LIBRARY_SUFFIXES
---------------------------

Suffixes to append when looking for libraries.

This specifies what suffixes to add to library names when the
:command:`find_library` command looks for libraries.  On Windows systems this
is typically ``.lib`` and ``.dll``, meaning that when trying to find the
``foo`` library it will look for ``foo.dll`` etc.
