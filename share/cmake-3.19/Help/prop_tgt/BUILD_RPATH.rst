BUILD_RPATH
-----------

.. versionadded:: 3.8

A :ref:`semicolon-separated list <CMake Language Lists>` specifying runtime path (``RPATH``)
entries to add to binaries linked in the build tree (for platforms that
support it).  The entries will *not* be used for binaries in the install
tree.  See also the :prop_tgt:`INSTALL_RPATH` target property.

This property is initialized by the value of the variable
:variable:`CMAKE_BUILD_RPATH` if it is set when a target is created.

This property supports
:manual:`generator expressions <cmake-generator-expressions(7)>`.
