<LANG>_CPPCHECK
---------------

.. versionadded:: 3.10

This property is supported only when ``<LANG>`` is ``C`` or ``CXX``.

Specify a :ref:`semicolon-separated list <CMake Language Lists>` containing a command line
for the ``cppcheck`` static analysis tool.  The :ref:`Makefile Generators`
and the :generator:`Ninja` generator will run ``cppcheck`` along with the
compiler and report any problems.  If the command-line specifies the
exit code options to ``cppcheck`` then the build  will fail if the
tool returns non-zero.

This property is initialized by the value of the
:variable:`CMAKE_<LANG>_CPPCHECK` variable if it is set when a target is
created.
