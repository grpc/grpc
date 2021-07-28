CMAKE_BUILD_PARALLEL_LEVEL
--------------------------

.. versionadded:: 3.12

.. include:: ENV_VAR.txt

Specifies the maximum number of concurrent processes to use when building
using the ``cmake --build`` command line
:ref:`Build Tool Mode <Build Tool Mode>`.

If this variable is defined empty the native build tool's default number is
used.
