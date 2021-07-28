INSTALL_RPATH
-------------

The rpath to use for installed targets.

A semicolon-separated list specifying the rpath to use in installed
targets (for platforms that support it).  This property is initialized
by the value of the variable :variable:`CMAKE_INSTALL_RPATH` if it is set when
a target is created.

Because the rpath may contain ``${ORIGIN}``, which coincides with CMake syntax,
the contents of ``INSTALL_RPATH`` are properly escaped in the
``cmake_install.cmake`` script (see policy :policy:`CMP0095`.)

This property supports
:manual:`generator expressions <cmake-generator-expressions(7)>`.
