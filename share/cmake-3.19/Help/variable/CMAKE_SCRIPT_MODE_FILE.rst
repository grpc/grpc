CMAKE_SCRIPT_MODE_FILE
----------------------

Full path to the :manual:`cmake(1)` ``-P`` script file currently being
processed.

When run in :manual:`cmake(1)` ``-P`` script mode, CMake sets this variable to
the full path of the script file.  When run to configure a ``CMakeLists.txt``
file, this variable is not set.
