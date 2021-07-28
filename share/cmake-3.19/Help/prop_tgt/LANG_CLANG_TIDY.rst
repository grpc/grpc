<LANG>_CLANG_TIDY
-----------------

.. versionadded:: 3.6

This property is implemented only when ``<LANG>`` is ``C`` or ``CXX``.

Specify a :ref:`semicolon-separated list <CMake Language Lists>` containing a command
line for the ``clang-tidy`` tool.  The :ref:`Makefile Generators`
and the :generator:`Ninja` generator will run this tool along with the
compiler and report a warning if the tool reports any problems.

This property is initialized by the value of
the :variable:`CMAKE_<LANG>_CLANG_TIDY` variable if it is set
when a target is created.
