# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
CheckCCompilerFlag
------------------

Check whether the C compiler supports a given flag.

.. command:: check_c_compiler_flag

  .. code-block:: cmake

    check_c_compiler_flag(<flag> <var>)

  Check that the ``<flag>`` is accepted by the compiler without
  a diagnostic.  Stores the result in an internal cache entry
  named ``<var>``.

This command temporarily sets the ``CMAKE_REQUIRED_DEFINITIONS`` variable
and calls the ``check_c_source_compiles`` macro from the
:module:`CheckCSourceCompiles` module.  See documentation of that
module for a listing of variables that can otherwise modify the build.

A positive result from this check indicates only that the compiler did not
issue a diagnostic message when given the flag.  Whether the flag has any
effect or even a specific one is beyond the scope of this module.

.. note::
  Since the :command:`try_compile` command forwards flags from variables
  like :variable:`CMAKE_C_FLAGS <CMAKE_<LANG>_FLAGS>`, unknown flags
  in such variables may cause a false negative for this check.
#]=======================================================================]

include_guard(GLOBAL)
include(CheckCSourceCompiles)
include(Internal/CheckCompilerFlag)

macro (CHECK_C_COMPILER_FLAG _FLAG _RESULT)
  cmake_check_compiler_flag(C "${_FLAG}" ${_RESULT})
endmacro ()
