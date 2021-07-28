# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
CheckFortranSourceCompiles
--------------------------

.. versionadded:: 3.1

Check if given Fortran source compiles and links into an executable.

.. command:: check_fortran_source_compiles

  .. code-block:: cmake

    check_fortran_source_compiles(<code> <resultVar>
        [FAIL_REGEX <regex>...]
        [SRC_EXT <extension>]
    )

  Checks that the source supplied in ``<code>`` can be compiled as a Fortran
  source file and linked as an executable. The ``<code>`` must be a Fortran program
  containing at least an ``end`` statement--for example:

  .. code-block:: cmake

    check_fortran_source_compiles("character :: b; error stop b; end" F2018ESTOPOK SRC_EXT F90)

  This command can help avoid costly build processes when a compiler lacks support
  for a necessary feature, or a particular vendor library is not compatible with
  the Fortran compiler version being used. This generate-time check may advise the
  user of such before the main build process. See also the
  :command:`check_fortran_source_runs` command to actually run the compiled code.

  The result will be stored in the internal cache
  variable ``<resultVar>``, with a boolean true value for success and boolean
  false for failure.

  If ``FAIL_REGEX`` is provided, then failure is determined by checking
  if anything in the output matches any of the specified regular expressions.

  By default, the test source file will be given a ``.F`` file extension. The
  ``SRC_EXT`` option can be used to override this with ``.<extension>`` instead--
  ``.F90`` is a typical choice.

  The underlying check is performed by the :command:`try_compile` command. The
  compile and link commands can be influenced by setting any of the following
  variables prior to calling ``check_fortran_source_compiles()``:

  ``CMAKE_REQUIRED_FLAGS``
    Additional flags to pass to the compiler. Note that the contents of
    :variable:`CMAKE_Fortran_FLAGS <CMAKE_<LANG>_FLAGS>` and its associated
    configuration-specific variable are automatically added to the compiler
    command before the contents of ``CMAKE_REQUIRED_FLAGS``.

  ``CMAKE_REQUIRED_DEFINITIONS``
    A :ref:`;-list <CMake Language Lists>` of compiler definitions of the form
    ``-DFOO`` or ``-DFOO=bar``. A definition for the name specified by
    ``<resultVar>`` will also be added automatically.

  ``CMAKE_REQUIRED_INCLUDES``
    A :ref:`;-list <CMake Language Lists>` of header search paths to pass to
    the compiler. These will be the only header search paths used by
    ``try_compile()``, i.e. the contents of the :prop_dir:`INCLUDE_DIRECTORIES`
    directory property will be ignored.

  ``CMAKE_REQUIRED_LINK_OPTIONS``
    A :ref:`;-list <CMake Language Lists>` of options to add to the link
    command (see :command:`try_compile` for further details).

  ``CMAKE_REQUIRED_LIBRARIES``
    A :ref:`;-list <CMake Language Lists>` of libraries to add to the link
    command. These can be the name of system libraries or they can be
    :ref:`Imported Targets <Imported Targets>` (see :command:`try_compile` for
    further details).

  ``CMAKE_REQUIRED_QUIET``
    If this variable evaluates to a boolean true value, all status messages
    associated with the check will be suppressed.

  The check is only performed once, with the result cached in the variable
  named by ``<resultVar>``. Every subsequent CMake run will re-use this cached
  value rather than performing the check again, even if the ``<code>`` changes.
  In order to force the check to be re-evaluated, the variable named by
  ``<resultVar>`` must be manually removed from the cache.

#]=======================================================================]

include_guard(GLOBAL)
include(Internal/CheckSourceCompiles)

macro(CHECK_Fortran_SOURCE_COMPILES SOURCE VAR)
  # Pass the SRC_EXT we used by default historically.
  # A user-provided SRC_EXT argument in ARGN will override ours.
  cmake_check_source_compiles(Fortran "${SOURCE}" ${VAR} SRC_EXT "F" ${ARGN})
endmacro()
