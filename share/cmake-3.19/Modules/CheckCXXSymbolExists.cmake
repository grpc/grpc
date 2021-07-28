# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
CheckCXXSymbolExists
--------------------

Check if a symbol exists as a function, variable, or macro in ``C++``.

.. command:: check_cxx_symbol_exists

  .. code-block:: cmake

    check_cxx_symbol_exists(<symbol> <files> <variable>)

  Check that the ``<symbol>`` is available after including given header
  ``<files>`` and store the result in a ``<variable>``.  Specify the list of
  files in one argument as a semicolon-separated list.
  ``check_cxx_symbol_exists()`` can be used to check for symbols as seen by
  the C++ compiler, as opposed to :command:`check_symbol_exists`, which always
  uses the ``C`` compiler.

  If the header files define the symbol as a macro it is considered
  available and assumed to work.  If the header files declare the symbol
  as a function or variable then the symbol must also be available for
  linking.  If the symbol is a type, enum value, or C++ template it will
  not be recognized: consider using the :module:`CheckTypeSize`
  or :module:`CheckCXXSourceCompiles` module instead.

.. note::

  This command is unreliable when ``<symbol>`` is (potentially) an overloaded
  function. Since there is no reliable way to predict whether a given function
  in the system environment may be defined as an overloaded function or may be
  an overloaded function on other systems or will become so in the future, it
  is generally advised to use the :module:`CheckCXXSourceCompiles` module for
  checking any function symbol (unless somehow you surely know the checked
  function is not overloaded on other systems or will not be so in the
  future).

The following variables may be set before calling this macro to modify
the way the check is run:

``CMAKE_REQUIRED_FLAGS``
  string of compile command line flags.
``CMAKE_REQUIRED_DEFINITIONS``
  a :ref:`;-list <CMake Language Lists>` of macros to define (-DFOO=bar).
``CMAKE_REQUIRED_INCLUDES``
  a :ref:`;-list <CMake Language Lists>` of header search paths to pass to
  the compiler.
``CMAKE_REQUIRED_LINK_OPTIONS``
  a :ref:`;-list <CMake Language Lists>` of options to add to the link command.
``CMAKE_REQUIRED_LIBRARIES``
  a :ref:`;-list <CMake Language Lists>` of libraries to add to the link
  command. See policy :policy:`CMP0075`.
``CMAKE_REQUIRED_QUIET``
  execute quietly without messages.

For example:

.. code-block:: cmake

  include(CheckCXXSymbolExists)

  # Check for macro SEEK_SET
  check_cxx_symbol_exists(SEEK_SET "cstdio" HAVE_SEEK_SET)
  # Check for function std::fopen
  check_cxx_symbol_exists(std::fopen "cstdio" HAVE_STD_FOPEN)
#]=======================================================================]

include_guard(GLOBAL)
include(CheckSymbolExists)

macro(CHECK_CXX_SYMBOL_EXISTS SYMBOL FILES VARIABLE)
  __CHECK_SYMBOL_EXISTS_IMPL("${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CMakeTmp/CheckSymbolExists.cxx" "${SYMBOL}" "${FILES}" "${VARIABLE}" )
endmacro()
