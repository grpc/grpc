# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
GenerateExportHeader
--------------------

Function for generation of export macros for libraries

This module provides the function ``GENERATE_EXPORT_HEADER()``.

The ``GENERATE_EXPORT_HEADER`` function can be used to generate a file
suitable for preprocessor inclusion which contains EXPORT macros to be
used in library classes::

   GENERATE_EXPORT_HEADER( LIBRARY_TARGET
             [BASE_NAME <base_name>]
             [EXPORT_MACRO_NAME <export_macro_name>]
             [EXPORT_FILE_NAME <export_file_name>]
             [DEPRECATED_MACRO_NAME <deprecated_macro_name>]
             [NO_EXPORT_MACRO_NAME <no_export_macro_name>]
             [INCLUDE_GUARD_NAME <include_guard_name>]
             [STATIC_DEFINE <static_define>]
             [NO_DEPRECATED_MACRO_NAME <no_deprecated_macro_name>]
             [DEFINE_NO_DEPRECATED]
             [PREFIX_NAME <prefix_name>]
             [CUSTOM_CONTENT_FROM_VARIABLE <variable>]
   )


The target properties :prop_tgt:`CXX_VISIBILITY_PRESET <<LANG>_VISIBILITY_PRESET>`
and :prop_tgt:`VISIBILITY_INLINES_HIDDEN` can be used to add the appropriate
compile flags for targets.  See the documentation of those target properties,
and the convenience variables
:variable:`CMAKE_CXX_VISIBILITY_PRESET <CMAKE_<LANG>_VISIBILITY_PRESET>` and
:variable:`CMAKE_VISIBILITY_INLINES_HIDDEN`.

By default ``GENERATE_EXPORT_HEADER()`` generates macro names in a file
name determined by the name of the library.  This means that in the
simplest case, users of ``GenerateExportHeader`` will be equivalent to:

.. code-block:: cmake

   set(CMAKE_CXX_VISIBILITY_PRESET hidden)
   set(CMAKE_VISIBILITY_INLINES_HIDDEN 1)
   add_library(somelib someclass.cpp)
   generate_export_header(somelib)
   install(TARGETS somelib DESTINATION ${LIBRARY_INSTALL_DIR})
   install(FILES
    someclass.h
    ${PROJECT_BINARY_DIR}/somelib_export.h DESTINATION ${INCLUDE_INSTALL_DIR}
   )


And in the ABI header files:

.. code-block:: c++

   #include "somelib_export.h"
   class SOMELIB_EXPORT SomeClass {
     ...
   };


The CMake fragment will generate a file in the
``${CMAKE_CURRENT_BINARY_DIR}`` called ``somelib_export.h`` containing the
macros ``SOMELIB_EXPORT``, ``SOMELIB_NO_EXPORT``, ``SOMELIB_DEPRECATED``,
``SOMELIB_DEPRECATED_EXPORT`` and ``SOMELIB_DEPRECATED_NO_EXPORT``.
They will be followed by content taken from the variable specified by
the ``CUSTOM_CONTENT_FROM_VARIABLE`` option, if any.
The resulting file should be installed with other headers in the library.

The ``BASE_NAME`` argument can be used to override the file name and the
names used for the macros:

.. code-block:: cmake

   add_library(somelib someclass.cpp)
   generate_export_header(somelib
     BASE_NAME other_name
   )


Generates a file called ``other_name_export.h`` containing the macros
``OTHER_NAME_EXPORT``, ``OTHER_NAME_NO_EXPORT`` and ``OTHER_NAME_DEPRECATED``
etc.

The ``BASE_NAME`` may be overridden by specifying other options in the
function.  For example:

.. code-block:: cmake

   add_library(somelib someclass.cpp)
   generate_export_header(somelib
     EXPORT_MACRO_NAME OTHER_NAME_EXPORT
   )


creates the macro ``OTHER_NAME_EXPORT`` instead of ``SOMELIB_EXPORT``, but
other macros and the generated file name is as default:

.. code-block:: cmake

   add_library(somelib someclass.cpp)
   generate_export_header(somelib
     DEPRECATED_MACRO_NAME KDE_DEPRECATED
   )


creates the macro ``KDE_DEPRECATED`` instead of ``SOMELIB_DEPRECATED``.

If ``LIBRARY_TARGET`` is a static library, macros are defined without
values.

If the same sources are used to create both a shared and a static
library, the uppercased symbol ``${BASE_NAME}_STATIC_DEFINE`` should be
used when building the static library:

.. code-block:: cmake

   add_library(shared_variant SHARED ${lib_SRCS})
   add_library(static_variant ${lib_SRCS})
   generate_export_header(shared_variant BASE_NAME libshared_and_static)
   set_target_properties(static_variant PROPERTIES
     COMPILE_FLAGS -DLIBSHARED_AND_STATIC_STATIC_DEFINE)

This will cause the export macros to expand to nothing when building
the static library.

If ``DEFINE_NO_DEPRECATED`` is specified, then a macro
``${BASE_NAME}_NO_DEPRECATED`` will be defined This macro can be used to
remove deprecated code from preprocessor output:

.. code-block:: cmake

   option(EXCLUDE_DEPRECATED "Exclude deprecated parts of the library" FALSE)
   if (EXCLUDE_DEPRECATED)
     set(NO_BUILD_DEPRECATED DEFINE_NO_DEPRECATED)
   endif()
   generate_export_header(somelib ${NO_BUILD_DEPRECATED})


And then in somelib:

.. code-block:: c++

   class SOMELIB_EXPORT SomeClass
   {
   public:
   #ifndef SOMELIB_NO_DEPRECATED
     SOMELIB_DEPRECATED void oldMethod();
   #endif
   };

.. code-block:: c++

   #ifndef SOMELIB_NO_DEPRECATED
   void SomeClass::oldMethod() {  }
   #endif


If ``PREFIX_NAME`` is specified, the argument will be used as a prefix to
all generated macros.

For example:

.. code-block:: cmake

   generate_export_header(somelib PREFIX_NAME VTK_)

Generates the macros ``VTK_SOMELIB_EXPORT`` etc.

::

   ADD_COMPILER_EXPORT_FLAGS( [<output_variable>] )

The ``ADD_COMPILER_EXPORT_FLAGS`` function adds ``-fvisibility=hidden`` to
:variable:`CMAKE_CXX_FLAGS <CMAKE_<LANG>_FLAGS>` if supported, and is a no-op
on Windows which does not need extra compiler flags for exporting support.
You may optionally pass a single argument to ``ADD_COMPILER_EXPORT_FLAGS``
that will be populated with the ``CXX_FLAGS`` required to enable visibility
support for the compiler/architecture in use.

This function is deprecated.  Set the target properties
:prop_tgt:`CXX_VISIBILITY_PRESET <<LANG>_VISIBILITY_PRESET>` and
:prop_tgt:`VISIBILITY_INLINES_HIDDEN` instead.
#]=======================================================================]

include(CheckCCompilerFlag)
include(CheckCXXCompilerFlag)

# TODO: Install this macro separately?
macro(_check_cxx_compiler_attribute _ATTRIBUTE _RESULT)
  check_cxx_source_compiles("${_ATTRIBUTE} int somefunc() { return 0; }
    int main() { return somefunc();}" ${_RESULT}
  )
endmacro()

# TODO: Install this macro separately?
macro(_check_c_compiler_attribute _ATTRIBUTE _RESULT)
  check_c_source_compiles("${_ATTRIBUTE} int somefunc() { return 0; }
    int main() { return somefunc();}" ${_RESULT}
  )
endmacro()

macro(_test_compiler_hidden_visibility)

  if(CMAKE_COMPILER_IS_GNUCXX AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS "4.2")
    set(GCC_TOO_OLD TRUE)
  elseif(CMAKE_COMPILER_IS_GNUCC AND CMAKE_C_COMPILER_VERSION VERSION_LESS "4.2")
    set(GCC_TOO_OLD TRUE)
  elseif(CMAKE_CXX_COMPILER_ID MATCHES Intel AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS "12.0")
    set(_INTEL_TOO_OLD TRUE)
  endif()

  # Exclude XL here because it misinterprets -fvisibility=hidden even though
  # the check_cxx_compiler_flag passes
  if(NOT GCC_TOO_OLD
      AND NOT _INTEL_TOO_OLD
      AND NOT WIN32
      AND NOT CYGWIN
      AND NOT CMAKE_CXX_COMPILER_ID MATCHES XL
      AND NOT CMAKE_CXX_COMPILER_ID MATCHES PGI
      AND NOT CMAKE_CXX_COMPILER_ID MATCHES Watcom)
    if (CMAKE_CXX_COMPILER_LOADED)
      check_cxx_compiler_flag(-fvisibility=hidden COMPILER_HAS_HIDDEN_VISIBILITY)
      check_cxx_compiler_flag(-fvisibility-inlines-hidden
        COMPILER_HAS_HIDDEN_INLINE_VISIBILITY)
    else()
      check_c_compiler_flag(-fvisibility=hidden COMPILER_HAS_HIDDEN_VISIBILITY)
      check_c_compiler_flag(-fvisibility-inlines-hidden
        COMPILER_HAS_HIDDEN_INLINE_VISIBILITY)
    endif()
  endif()
endmacro()

macro(_test_compiler_has_deprecated)
  # NOTE:  Some Embarcadero compilers silently compile __declspec(deprecated)
  # without error, but this is not a documented feature and the attribute does
  # not actually generate any warnings.
  if(CMAKE_CXX_COMPILER_ID MATCHES Borland
      OR CMAKE_CXX_COMPILER_ID MATCHES Embarcadero
      OR CMAKE_CXX_COMPILER_ID MATCHES HP
      OR GCC_TOO_OLD
      OR CMAKE_CXX_COMPILER_ID MATCHES PGI
      OR CMAKE_CXX_COMPILER_ID MATCHES Watcom)
    set(COMPILER_HAS_DEPRECATED "" CACHE INTERNAL
      "Compiler support for a deprecated attribute")
  else()
    if (CMAKE_CXX_COMPILER_LOADED)
      _check_cxx_compiler_attribute("__attribute__((__deprecated__))"
        COMPILER_HAS_DEPRECATED_ATTR)
      if(COMPILER_HAS_DEPRECATED_ATTR)
        set(COMPILER_HAS_DEPRECATED "${COMPILER_HAS_DEPRECATED_ATTR}"
          CACHE INTERNAL "Compiler support for a deprecated attribute")
      else()
        _check_cxx_compiler_attribute("__declspec(deprecated)"
          COMPILER_HAS_DEPRECATED)
      endif()
    else()
      _check_c_compiler_attribute("__attribute__((__deprecated__))"
        COMPILER_HAS_DEPRECATED_ATTR)
      if(COMPILER_HAS_DEPRECATED_ATTR)
        set(COMPILER_HAS_DEPRECATED "${COMPILER_HAS_DEPRECATED_ATTR}"
          CACHE INTERNAL "Compiler support for a deprecated attribute")
      else()
        _check_c_compiler_attribute("__declspec(deprecated)"
          COMPILER_HAS_DEPRECATED)
      endif()

    endif()
  endif()
endmacro()

get_filename_component(_GENERATE_EXPORT_HEADER_MODULE_DIR
  "${CMAKE_CURRENT_LIST_FILE}" PATH)

macro(_DO_SET_MACRO_VALUES TARGET_LIBRARY)
  set(DEFINE_DEPRECATED)
  set(DEFINE_EXPORT)
  set(DEFINE_IMPORT)
  set(DEFINE_NO_EXPORT)

  if (COMPILER_HAS_DEPRECATED_ATTR)
    set(DEFINE_DEPRECATED "__attribute__ ((__deprecated__))")
  elseif(COMPILER_HAS_DEPRECATED)
    set(DEFINE_DEPRECATED "__declspec(deprecated)")
  endif()

  get_property(type TARGET ${TARGET_LIBRARY} PROPERTY TYPE)

  if(NOT ${type} STREQUAL "STATIC_LIBRARY")
    if(WIN32 OR CYGWIN)
      set(DEFINE_EXPORT "__declspec(dllexport)")
      set(DEFINE_IMPORT "__declspec(dllimport)")
    elseif(COMPILER_HAS_HIDDEN_VISIBILITY)
      set(DEFINE_EXPORT "__attribute__((visibility(\"default\")))")
      set(DEFINE_IMPORT "__attribute__((visibility(\"default\")))")
      set(DEFINE_NO_EXPORT "__attribute__((visibility(\"hidden\")))")
    endif()
  endif()
endmacro()

macro(_DO_GENERATE_EXPORT_HEADER TARGET_LIBRARY)
  # Option overrides
  set(options DEFINE_NO_DEPRECATED)
  set(oneValueArgs PREFIX_NAME BASE_NAME EXPORT_MACRO_NAME EXPORT_FILE_NAME
    DEPRECATED_MACRO_NAME NO_EXPORT_MACRO_NAME STATIC_DEFINE
    NO_DEPRECATED_MACRO_NAME CUSTOM_CONTENT_FROM_VARIABLE INCLUDE_GUARD_NAME)
  set(multiValueArgs)

  cmake_parse_arguments(_GEH "${options}" "${oneValueArgs}" "${multiValueArgs}"
    ${ARGN})

  set(BASE_NAME "${TARGET_LIBRARY}")

  if(_GEH_BASE_NAME)
    set(BASE_NAME ${_GEH_BASE_NAME})
  endif()

  string(TOUPPER ${BASE_NAME} BASE_NAME_UPPER)
  string(TOLOWER ${BASE_NAME} BASE_NAME_LOWER)

  # Default options
  set(EXPORT_MACRO_NAME "${_GEH_PREFIX_NAME}${BASE_NAME_UPPER}_EXPORT")
  set(NO_EXPORT_MACRO_NAME "${_GEH_PREFIX_NAME}${BASE_NAME_UPPER}_NO_EXPORT")
  set(EXPORT_FILE_NAME "${CMAKE_CURRENT_BINARY_DIR}/${BASE_NAME_LOWER}_export.h")
  set(DEPRECATED_MACRO_NAME "${_GEH_PREFIX_NAME}${BASE_NAME_UPPER}_DEPRECATED")
  set(STATIC_DEFINE "${_GEH_PREFIX_NAME}${BASE_NAME_UPPER}_STATIC_DEFINE")
  set(NO_DEPRECATED_MACRO_NAME
    "${_GEH_PREFIX_NAME}${BASE_NAME_UPPER}_NO_DEPRECATED")

  if(_GEH_UNPARSED_ARGUMENTS)
    message(FATAL_ERROR "Unknown keywords given to GENERATE_EXPORT_HEADER(): \"${_GEH_UNPARSED_ARGUMENTS}\"")
  endif()

  if(_GEH_EXPORT_MACRO_NAME)
    set(EXPORT_MACRO_NAME ${_GEH_PREFIX_NAME}${_GEH_EXPORT_MACRO_NAME})
  endif()
  string(MAKE_C_IDENTIFIER ${EXPORT_MACRO_NAME} EXPORT_MACRO_NAME)
  if(_GEH_EXPORT_FILE_NAME)
    if(IS_ABSOLUTE ${_GEH_EXPORT_FILE_NAME})
      set(EXPORT_FILE_NAME ${_GEH_EXPORT_FILE_NAME})
    else()
      set(EXPORT_FILE_NAME "${CMAKE_CURRENT_BINARY_DIR}/${_GEH_EXPORT_FILE_NAME}")
    endif()
  endif()
  if(_GEH_DEPRECATED_MACRO_NAME)
    set(DEPRECATED_MACRO_NAME ${_GEH_PREFIX_NAME}${_GEH_DEPRECATED_MACRO_NAME})
  endif()
  string(MAKE_C_IDENTIFIER ${DEPRECATED_MACRO_NAME} DEPRECATED_MACRO_NAME)
  if(_GEH_NO_EXPORT_MACRO_NAME)
    set(NO_EXPORT_MACRO_NAME ${_GEH_PREFIX_NAME}${_GEH_NO_EXPORT_MACRO_NAME})
  endif()
  string(MAKE_C_IDENTIFIER ${NO_EXPORT_MACRO_NAME} NO_EXPORT_MACRO_NAME)
  if(_GEH_STATIC_DEFINE)
    set(STATIC_DEFINE ${_GEH_PREFIX_NAME}${_GEH_STATIC_DEFINE})
  endif()
  string(MAKE_C_IDENTIFIER ${STATIC_DEFINE} STATIC_DEFINE)

  if(_GEH_DEFINE_NO_DEPRECATED)
    set(DEFINE_NO_DEPRECATED 1)
  else()
    set(DEFINE_NO_DEPRECATED 0)
  endif()

  if(_GEH_NO_DEPRECATED_MACRO_NAME)
    set(NO_DEPRECATED_MACRO_NAME
      ${_GEH_PREFIX_NAME}${_GEH_NO_DEPRECATED_MACRO_NAME})
  endif()
  string(MAKE_C_IDENTIFIER ${NO_DEPRECATED_MACRO_NAME} NO_DEPRECATED_MACRO_NAME)

  if(_GEH_INCLUDE_GUARD_NAME)
    set(INCLUDE_GUARD_NAME ${_GEH_INCLUDE_GUARD_NAME})
  else()
    set(INCLUDE_GUARD_NAME "${EXPORT_MACRO_NAME}_H")
  endif()

  get_target_property(EXPORT_IMPORT_CONDITION ${TARGET_LIBRARY} DEFINE_SYMBOL)

  if(NOT EXPORT_IMPORT_CONDITION)
    set(EXPORT_IMPORT_CONDITION ${TARGET_LIBRARY}_EXPORTS)
  endif()
  string(MAKE_C_IDENTIFIER ${EXPORT_IMPORT_CONDITION} EXPORT_IMPORT_CONDITION)

  if(_GEH_CUSTOM_CONTENT_FROM_VARIABLE)
    if(DEFINED "${_GEH_CUSTOM_CONTENT_FROM_VARIABLE}")
      set(CUSTOM_CONTENT "${${_GEH_CUSTOM_CONTENT_FROM_VARIABLE}}")
    else()
      set(CUSTOM_CONTENT "")
    endif()
  endif()

  configure_file("${_GENERATE_EXPORT_HEADER_MODULE_DIR}/exportheader.cmake.in"
    "${EXPORT_FILE_NAME}" @ONLY)
endmacro()

function(GENERATE_EXPORT_HEADER TARGET_LIBRARY)
  get_property(type TARGET ${TARGET_LIBRARY} PROPERTY TYPE)
  if(NOT ${type} STREQUAL "STATIC_LIBRARY"
      AND NOT ${type} STREQUAL "SHARED_LIBRARY"
      AND NOT ${type} STREQUAL "OBJECT_LIBRARY"
      AND NOT ${type} STREQUAL "MODULE_LIBRARY")
    message(WARNING "This macro can only be used with libraries")
    return()
  endif()
  _test_compiler_hidden_visibility()
  _test_compiler_has_deprecated()
  _do_set_macro_values(${TARGET_LIBRARY})
  _do_generate_export_header(${TARGET_LIBRARY} ${ARGN})
endfunction()

function(add_compiler_export_flags)
  if(NOT CMAKE_MINIMUM_REQUIRED_VERSION VERSION_LESS 2.8.12)
    message(DEPRECATION "The add_compiler_export_flags function is obsolete. Use the CXX_VISIBILITY_PRESET and VISIBILITY_INLINES_HIDDEN target properties instead.")
  endif()

  _test_compiler_hidden_visibility()
  _test_compiler_has_deprecated()

  option(USE_COMPILER_HIDDEN_VISIBILITY
    "Use HIDDEN visibility support if available." ON)
  mark_as_advanced(USE_COMPILER_HIDDEN_VISIBILITY)
  if(NOT (USE_COMPILER_HIDDEN_VISIBILITY AND COMPILER_HAS_HIDDEN_VISIBILITY))
    # Just return if there are no flags to add.
    return()
  endif()

  set (EXTRA_FLAGS "-fvisibility=hidden")

  if(COMPILER_HAS_HIDDEN_INLINE_VISIBILITY)
    set (EXTRA_FLAGS "${EXTRA_FLAGS} -fvisibility-inlines-hidden")
  endif()

  # Either return the extra flags needed in the supplied argument, or to the
  # CMAKE_CXX_FLAGS if no argument is supplied.
  if(ARGC GREATER 0)
    set(${ARGV0} "${EXTRA_FLAGS}" PARENT_SCOPE)
  else()
    string(APPEND CMAKE_CXX_FLAGS " ${EXTRA_FLAGS}")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}" PARENT_SCOPE)
  endif()
endfunction()
