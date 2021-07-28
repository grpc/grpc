# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
CheckOBJCXXSourceRuns
---------------------

.. versionadded:: 3.16

Check if given Objective-C++ source compiles and links into an executable and can
subsequently be run.

.. command:: check_objcxx_source_runs

  .. code-block:: cmake

    check_objcxx_source_runs(<code> <resultVar>)

  Check that the source supplied in ``<code>`` can be compiled as a Objective-C++ source
  file, linked as an executable and then run. The ``<code>`` must contain at
  least a ``main()`` function. If the ``<code>`` could be built and run
  successfully, the internal cache variable specified by ``<resultVar>`` will
  be set to 1, otherwise it will be set to an value that evaluates to boolean
  false (e.g. an empty string or an error message).

  The underlying check is performed by the :command:`try_run` command. The
  compile and link commands can be influenced by setting any of the following
  variables prior to calling ``check_objcxx_source_runs()``:

  ``CMAKE_REQUIRED_FLAGS``
    Additional flags to pass to the compiler. Note that the contents of
    :variable:`CMAKE_OBJCXX_FLAGS <CMAKE_<LANG>_FLAGS>` and its associated
    configuration-specific variable are automatically added to the compiler
    command before the contents of ``CMAKE_REQUIRED_FLAGS``.

  ``CMAKE_REQUIRED_DEFINITIONS``
    A :ref:`;-list <CMake Language Lists>` of compiler definitions of the form
    ``-DFOO`` or ``-DFOO=bar``. A definition for the name specified by
    ``<resultVar>`` will also be added automatically.

  ``CMAKE_REQUIRED_INCLUDES``
    A :ref:`;-list <CMake Language Lists>` of header search paths to pass to
    the compiler. These will be the only header search paths used by
    ``try_run()``, i.e. the contents of the :prop_dir:`INCLUDE_DIRECTORIES`
    directory property will be ignored.

  ``CMAKE_REQUIRED_LINK_OPTIONS``
    A :ref:`;-list <CMake Language Lists>` of options to add to the link
    command (see :command:`try_run` for further details).

  ``CMAKE_REQUIRED_LIBRARIES``
    A :ref:`;-list <CMake Language Lists>` of libraries to add to the link
    command. These can be the name of system libraries or they can be
    :ref:`Imported Targets <Imported Targets>` (see :command:`try_run` for
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
include(Internal/CheckSourceRuns)

macro(CHECK_OBJCXX_SOURCE_RUNS SOURCE VAR)
  set(_CheckSourceRuns_old_signature 1)
  cmake_check_source_runs(OBJCXX "${SOURCE}" ${VAR} ${ARGN})
  unset(_CheckSourceRuns_old_signature)
endmacro()
