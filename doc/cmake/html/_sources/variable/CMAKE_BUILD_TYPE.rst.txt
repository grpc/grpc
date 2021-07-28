CMAKE_BUILD_TYPE
----------------

Specifies the build type on single-configuration generators.

This statically specifies what build type (configuration) will be
built in this build tree.  Possible values are empty, ``Debug``, ``Release``,
``RelWithDebInfo``, ``MinSizeRel``, ...  This variable is only meaningful to
single-configuration generators (such as :ref:`Makefile Generators` and
:generator:`Ninja`) i.e.  those which choose a single configuration when CMake
runs to generate a build tree as opposed to multi-configuration generators
which offer selection of the build configuration within the generated build
environment.  There are many per-config properties and variables
(usually following clean ``SOME_VAR_<CONFIG>`` order conventions), such as
``CMAKE_C_FLAGS_<CONFIG>``, specified as uppercase:
``CMAKE_C_FLAGS_[DEBUG|RELEASE|RELWITHDEBINFO|MINSIZEREL|...]``.  For example,
in a build tree configured to build type ``Debug``, CMake will see to
having :variable:`CMAKE_C_FLAGS_DEBUG <CMAKE_<LANG>_FLAGS_DEBUG>` settings get
added to the :variable:`CMAKE_C_FLAGS <CMAKE_<LANG>_FLAGS>` settings.  See
also :variable:`CMAKE_CONFIGURATION_TYPES`.
