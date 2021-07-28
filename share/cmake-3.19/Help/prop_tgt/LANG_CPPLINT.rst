<LANG>_CPPLINT
--------------

.. versionadded:: 3.8

This property is supported only when ``<LANG>`` is ``C`` or ``CXX``.

Specify a :ref:`semicolon-separated list <CMake Language Lists>` containing a command line
for the ``cpplint`` style checker.  The :ref:`Makefile Generators` and the
:generator:`Ninja` generator will run ``cpplint`` along with the compiler
and report any problems.

This property is initialized by the value of the
:variable:`CMAKE_<LANG>_CPPLINT` variable if it is set when a target is
created.
