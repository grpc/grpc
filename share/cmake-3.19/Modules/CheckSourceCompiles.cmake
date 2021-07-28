# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.


#[=======================================================================[.rst:
CheckSourceCompiles
----------------------

.. versionadded:: 3.19

Check if given source compiles and links into an executable.

.. command:: check_source_compiles

  .. code-block:: cmake

    check_source_compiles(<lang> <code> <resultVar>
                          [FAIL_REGEX <regex1> [<regex2>...]]
                          [SRC_EXT <extension>])

  Check that the source supplied in ``<code>`` can be compiled as a source
  file for the requested language and linked as an executable (so it must
  contain at least a ``main()`` function). The result will be stored in the
  internal cache variable specified by ``<resultVar>``, with a boolean true
  value for success and boolean false for failure. If ``FAIL_REGEX`` is
  provided, then failure is determined by checking if anything in the output
  matches any of the specified regular expressions.

  By default, the test source file will be given a file extension that matches
  the requested language. The ``SRC_EXT`` option can be used to override this
  with ``.<extension>`` instead.

  The underlying check is performed by the :command:`try_compile` command. The
  compile and link commands can be influenced by setting any of the following
  variables prior to calling ``check_source_compiles()``:

  ``CMAKE_REQUIRED_FLAGS``
    Additional flags to pass to the compiler. Note that the contents of
    :variable:`CMAKE_<LANG>_FLAGS <CMAKE_<LANG>_FLAGS>` and its associated
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

function(CHECK_SOURCE_COMPILES _lang _source _var)
  cmake_check_source_compiles(${_lang} "${_source}" ${_var} ${ARGN})
endfunction()
