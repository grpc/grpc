enable_language
---------------
Enable a language (CXX/C/OBJC/OBJCXX/Fortran/etc)

.. code-block:: cmake

  enable_language(<lang> [OPTIONAL] )

Enables support for the named language in CMake.  This is
the same as the :command:`project` command but does not create any of the extra
variables that are created by the project command.  Example languages
are ``CXX``, ``C``, ``CUDA``, ``OBJC``, ``OBJCXX``, ``Fortran``,
``ISPC``, and ``ASM``.

If enabling ``ASM``, enable it last so that CMake can check whether
compilers for other languages like ``C`` work for assembly too.

This command must be called in file scope, not in a function call.
Furthermore, it must be called in the highest directory common to all
targets using the named language directly for compiling sources or
indirectly through link dependencies.  It is simplest to enable all
needed languages in the top-level directory of a project.

The ``OPTIONAL`` keyword is a placeholder for future implementation and
does not currently work. Instead you can use the :module:`CheckLanguage`
module to verify support before enabling.
