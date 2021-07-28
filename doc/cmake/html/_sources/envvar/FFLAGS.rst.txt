FFLAGS
------

.. include:: ENV_VAR.txt

Default compilation flags to be used when compiling ``Fortran`` files. Will only
be used by CMake on the first configuration to determine ``Fortran`` default
compilation flags, after which the value for ``FFLAGS`` is stored in the cache
as :variable:`CMAKE_Fortran_FLAGS <CMAKE_<LANG>_FLAGS>`. For any configuration
run (including the first), the environment variable will be ignored if
the :variable:`CMAKE_Fortran_FLAGS <CMAKE_<LANG>_FLAGS>` variable is defined.

See also :variable:`CMAKE_Fortran_FLAGS_INIT <CMAKE_<LANG>_FLAGS_INIT>`.
