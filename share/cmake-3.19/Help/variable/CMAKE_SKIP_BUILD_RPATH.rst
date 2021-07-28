CMAKE_SKIP_BUILD_RPATH
----------------------

Do not include RPATHs in the build tree.

Normally CMake uses the build tree for the RPATH when building
executables etc on systems that use RPATH.  When the software is
installed the executables etc are relinked by CMake to have the
install RPATH.  If this variable is set to true then the software is
always built with no RPATH.
