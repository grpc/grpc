CMAKE_XCODE_PLATFORM_TOOLSET
----------------------------

Xcode compiler selection.

:generator:`Xcode` supports selection of a compiler from one of the installed
toolsets.  CMake provides the name of the chosen toolset in this
variable, if any is explicitly selected (e.g.  via the :manual:`cmake(1)`
``-T`` option).
