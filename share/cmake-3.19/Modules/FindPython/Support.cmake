# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#
# This file is a "template" file used by various FindPython modules.
#

#
# Initial configuration
#

cmake_policy(PUSH)
# numbers and boolean constants
cmake_policy (SET CMP0012 NEW)
# IN_LIST operator
cmake_policy (SET CMP0057 NEW)

if (NOT DEFINED _PYTHON_PREFIX)
  message (FATAL_ERROR "FindPython: INTERNAL ERROR")
endif()
if (NOT DEFINED _${_PYTHON_PREFIX}_REQUIRED_VERSION_MAJOR)
  message (FATAL_ERROR "FindPython: INTERNAL ERROR")
endif()
if (_${_PYTHON_PREFIX}_REQUIRED_VERSION_MAJOR EQUAL "3")
  set(_${_PYTHON_PREFIX}_VERSIONS 3.10 3.9 3.8 3.7 3.6 3.5 3.4 3.3 3.2 3.1 3.0)
elseif (_${_PYTHON_PREFIX}_REQUIRED_VERSION_MAJOR EQUAL "2")
  set(_${_PYTHON_PREFIX}_VERSIONS 2.7 2.6 2.5 2.4 2.3 2.2 2.1 2.0)
else()
  message (FATAL_ERROR "FindPython: INTERNAL ERROR")
endif()

get_property(_${_PYTHON_PREFIX}_CMAKE_ROLE GLOBAL PROPERTY CMAKE_ROLE)

include (${CMAKE_CURRENT_LIST_DIR}/../FindPackageHandleStandardArgs.cmake)

#
# helper commands
#
macro (_PYTHON_DISPLAY_FAILURE _PYTHON_MSG)
  if (${_PYTHON_PREFIX}_FIND_REQUIRED)
    message (FATAL_ERROR "${_PYTHON_MSG}")
  else()
    if (NOT ${_PYTHON_PREFIX}_FIND_QUIETLY)
      message(STATUS "${_PYTHON_MSG}")
    endif ()
  endif()

  set (${_PYTHON_PREFIX}_FOUND FALSE)
  string (TOUPPER "${_PYTHON_PREFIX}" _${_PYTHON_PREFIX}_UPPER_PREFIX)
  set (${_PYTHON_UPPER_PREFIX}_FOUND FALSE)
endmacro()


function (_PYTHON_MARK_AS_INTERNAL)
  foreach (var IN LISTS ARGV)
    if (DEFINED CACHE{${var}})
      set_property (CACHE ${var} PROPERTY TYPE INTERNAL)
    endif()
  endforeach()
endfunction()


macro (_PYTHON_SELECT_LIBRARY_CONFIGURATIONS _PYTHON_BASENAME)
  if(NOT DEFINED ${_PYTHON_BASENAME}_LIBRARY_RELEASE)
    set(${_PYTHON_BASENAME}_LIBRARY_RELEASE "${_PYTHON_BASENAME}_LIBRARY_RELEASE-NOTFOUND")
  endif()
  if(NOT DEFINED ${_PYTHON_BASENAME}_LIBRARY_DEBUG)
    set(${_PYTHON_BASENAME}_LIBRARY_DEBUG "${_PYTHON_BASENAME}_LIBRARY_DEBUG-NOTFOUND")
  endif()

  get_property(_PYTHON_isMultiConfig GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
  if (${_PYTHON_BASENAME}_LIBRARY_DEBUG AND ${_PYTHON_BASENAME}_LIBRARY_RELEASE AND
      NOT ${_PYTHON_BASENAME}_LIBRARY_DEBUG STREQUAL ${_PYTHON_BASENAME}_LIBRARY_RELEASE AND
      (_PYTHON_isMultiConfig OR CMAKE_BUILD_TYPE))
    # if the generator is multi-config or if CMAKE_BUILD_TYPE is set for
    # single-config generators, set optimized and debug libraries
    set (${_PYTHON_BASENAME}_LIBRARIES "")
    foreach (_PYTHON_libname IN LISTS ${_PYTHON_BASENAME}_LIBRARY_RELEASE)
      list( APPEND ${_PYTHON_BASENAME}_LIBRARIES optimized "${_PYTHON_libname}")
    endforeach()
    foreach (_PYTHON_libname IN LISTS ${_PYTHON_BASENAME}_LIBRARY_DEBUG)
      list( APPEND ${_PYTHON_BASENAME}_LIBRARIES debug "${_PYTHON_libname}")
    endforeach()
  elseif (${_PYTHON_BASENAME}_LIBRARY_RELEASE)
    set (${_PYTHON_BASENAME}_LIBRARIES "${${_PYTHON_BASENAME}_LIBRARY_RELEASE}")
  elseif (${_PYTHON_BASENAME}_LIBRARY_DEBUG)
    set (${_PYTHON_BASENAME}_LIBRARIES "${${_PYTHON_BASENAME}_LIBRARY_DEBUG}")
  else()
    set (${_PYTHON_BASENAME}_LIBRARIES "${_PYTHON_BASENAME}_LIBRARY-NOTFOUND")
  endif()
endmacro()


macro (_PYTHON_FIND_FRAMEWORKS)
  if (CMAKE_HOST_APPLE OR APPLE)
    file(TO_CMAKE_PATH "$ENV{CMAKE_FRAMEWORK_PATH}" _pff_CMAKE_FRAMEWORK_PATH)
    set (_pff_frameworks ${CMAKE_FRAMEWORK_PATH}
                         ${_pff_CMAKE_FRAMEWORK_PATH}
                         ~/Library/Frameworks
                         /usr/local/Frameworks
                         ${CMAKE_SYSTEM_FRAMEWORK_PATH})
    list (REMOVE_DUPLICATES _pff_frameworks)
    foreach (_pff_implementation IN LISTS _${_PYTHON_PREFIX}_FIND_IMPLEMENTATIONS)
      unset (_${_PYTHON_PREFIX}_${_pff_implementation}_FRAMEWORKS)
      if (_pff_implementation STREQUAL "CPython")
        foreach (_pff_framework IN LISTS _pff_frameworks)
          if (EXISTS ${_pff_framework}/Python${_${_PYTHON_PREFIX}_REQUIRED_VERSION_MAJOR}.framework)
            list (APPEND _${_PYTHON_PREFIX}_${_pff_implementation}_FRAMEWORKS ${_pff_framework}/Python${_${_PYTHON_PREFIX}_REQUIRED_VERSION_MAJOR}.framework)
          endif()
          if (EXISTS ${_pff_framework}/Python.framework)
            list (APPEND _${_PYTHON_PREFIX}_${_pff_implementation}_FRAMEWORKS ${_pff_framework}/Python.framework)
          endif()
        endforeach()
      elseif (_pff_implementation STREQUAL "IronPython")
        foreach (_pff_framework IN LISTS _pff_frameworks)
          if (EXISTS ${_pff_framework}/IronPython.framework)
            list (APPEND _${_PYTHON_PREFIX}_${_pff_implementation}_FRAMEWORKS ${_pff_framework}/IronPython.framework)
          endif()
        endforeach()
      endif()
    endforeach()
    unset (_pff_implementation)
    unset (_pff_frameworks)
    unset (_pff_framework)
  endif()
endmacro()

function (_PYTHON_GET_FRAMEWORKS _PYTHON_PGF_FRAMEWORK_PATHS)
  cmake_parse_arguments (PARSE_ARGV 1 _PGF "" "" "IMPLEMENTATIONS;VERSION")

  if (NOT _PGF_IMPLEMENTATIONS)
    set (_PGF_IMPLEMENTATIONS ${_${_PYTHON_PREFIX}_FIND_IMPLEMENTATIONS})
  endif()

  set (framework_paths)

  foreach (implementation IN LISTS _PGF_IMPLEMENTATIONS)
    if (implementation STREQUAL "CPython")
      foreach (version IN LISTS _PGF_VERSION)
        foreach (framework IN LISTS _${_PYTHON_PREFIX}_${implementation}_FRAMEWORKS)
          if (EXISTS "${framework}/Versions/${version}")
            list (APPEND framework_paths "${framework}/Versions/${version}")
          endif()
        endforeach()
      endforeach()
    elseif (implementation STREQUAL "IronPython")
      foreach (version IN LISTS _PGF_VERSION)
        foreach (framework IN LISTS _${_PYTHON_PREFIX}_${implementation}_FRAMEWORKS)
          # pick-up all available versions
          file (GLOB versions LIST_DIRECTORIES true RELATIVE "${framework}/Versions/"
                              "${framework}/Versions/${version}*")
          list (SORT versions ORDER DESCENDING)
          list (TRANSFORM versions PREPEND "${framework}/Versions/")
          list (APPEND framework_paths ${versions})
        endforeach()
      endforeach()
    endif()
  endforeach()

  set (${_PYTHON_PGF_FRAMEWORK_PATHS} ${framework_paths} PARENT_SCOPE)
endfunction()

function (_PYTHON_GET_REGISTRIES _PYTHON_PGR_REGISTRY_PATHS)
  cmake_parse_arguments (PARSE_ARGV 1 _PGR "" "" "IMPLEMENTATIONS;VERSION")

  if (NOT _PGR_IMPLEMENTATIONS)
    set (_PGR_IMPLEMENTATIONS ${_${_PYTHON_PREFIX}_FIND_IMPLEMENTATIONS})
  endif()

  set (registries)

  foreach (implementation IN LISTS _PGR_IMPLEMENTATIONS)
    if (implementation STREQUAL "CPython")
      foreach (version IN LISTS _PGR_VERSION)
        string (REPLACE "." "" version_no_dots ${version})
        list (APPEND registries
                     [HKEY_CURRENT_USER\\SOFTWARE\\Python\\PythonCore\\${version}-${_${_PYTHON_PREFIX}_ARCH}\\InstallPath]
                     [HKEY_CURRENT_USER\\SOFTWARE\\Python\\PythonCore\\${version}-${_${_PYTHON_PREFIX}_ARCH2}\\InstallPath])
        if (version VERSION_GREATER_EQUAL "3.5")
          get_filename_component (arch "[HKEY_CURRENT_USER\\Software\\Python\\PythonCore\\${version};SysArchitecture]" NAME)
          if (arch MATCHES "(${_${_PYTHON_PREFIX}_ARCH}|${_${_PYTHON_PREFIX}_ARCH2})bit")
            list (APPEND registries
                         [HKEY_CURRENT_USER\\SOFTWARE\\Python\\PythonCore\\${version}\\InstallPath])
          endif()
        else()
          list (APPEND registries
                       [HKEY_CURRENT_USER\\SOFTWARE\\Python\\PythonCore\\${version}\\InstallPath])
        endif()
        list (APPEND registries
                     [HKEY_CURRENT_USER\\SOFTWARE\\Python\\ContinuumAnalytics\\Anaconda${version_no_dots}-${_${_PYTHON_PREFIX}_ARCH}\\InstallPath]
                     [HKEY_CURRENT_USER\\SOFTWARE\\Python\\ContinuumAnalytics\\Anaconda${version_no_dots}-${_${_PYTHON_PREFIX}_ARCH2}\\InstallPath]
                     [HKEY_LOCAL_MACHINE\\SOFTWARE\\Python\\PythonCore\\${version}-${_${_PYTHON_PREFIX}_ARCH}\\InstallPath]
                     [HKEY_LOCAL_MACHINE\\SOFTWARE\\Python\\PythonCore\\${version}-${_${_PYTHON_PREFIX}_ARCH2}\\InstallPath]
                     [HKEY_LOCAL_MACHINE\\SOFTWARE\\Python\\PythonCore\\${version}\\InstallPath]
                     [HKEY_LOCAL_MACHINE\\SOFTWARE\\Python\\ContinuumAnalytics\\Anaconda${version_no_dots}-${_${_PYTHON_PREFIX}_ARCH}\\InstallPath]
                     [HKEY_LOCAL_MACHINE\\SOFTWARE\\Python\\ContinuumAnalytics\\Anaconda${version_no_dots}-${_${_PYTHON_PREFIX}_ARCH2}\\InstallPath])
      endforeach()
    elseif (implementation STREQUAL "IronPython")
      foreach (version  IN LISTS _PGR_VERSION)
        list (APPEND registries [HKEY_LOCAL_MACHINE\\SOFTWARE\\IronPython\\${version}\\InstallPath])
      endforeach()
    endif()
  endforeach()

  set (${_PYTHON_PGR_REGISTRY_PATHS} "${registries}" PARENT_SCOPE)
endfunction()


function (_PYTHON_GET_ABIFLAGS _PGABIFLAGS)
  set (abiflags)
  list (GET _${_PYTHON_PREFIX}_FIND_ABI 0 pydebug)
  list (GET _${_PYTHON_PREFIX}_FIND_ABI 1 pymalloc)
  list (GET _${_PYTHON_PREFIX}_FIND_ABI 2 unicode)

  if (pymalloc STREQUAL "ANY" AND unicode STREQUAL "ANY")
    set (abiflags "mu" "m" "u" "")
  elseif (pymalloc STREQUAL "ANY" AND unicode STREQUAL "ON")
    set (abiflags "mu" "u")
  elseif (pymalloc STREQUAL "ANY" AND unicode STREQUAL "OFF")
    set (abiflags "m" "")
  elseif (pymalloc STREQUAL "ON" AND unicode STREQUAL "ANY")
    set (abiflags "mu" "m")
  elseif (pymalloc STREQUAL "ON" AND unicode STREQUAL "ON")
    set (abiflags "mu")
  elseif (pymalloc STREQUAL "ON" AND unicode STREQUAL "OFF")
    set (abiflags "m")
  elseif (pymalloc STREQUAL "ON" AND unicode STREQUAL "ANY")
    set (abiflags "u" "")
  elseif (pymalloc STREQUAL "OFF" AND unicode STREQUAL "ON")
    set (abiflags "u")
  endif()

  if (pydebug STREQUAL "ON")
    if (abiflags)
      list (TRANSFORM abiflags PREPEND "d")
    else()
      set (abiflags "d")
    endif()
  elseif (pydebug STREQUAL "ANY")
    if (abiflags)
      set (flags "${abiflags}")
      list (TRANSFORM flags PREPEND "d")
      list (APPEND abiflags "${flags}")
    else()
      set (abiflags "" "d")
    endif()
  endif()

  set (${_PGABIFLAGS} "${abiflags}" PARENT_SCOPE)
endfunction()

function (_PYTHON_GET_PATH_SUFFIXES _PYTHON_PGPS_PATH_SUFFIXES)
  cmake_parse_arguments (PARSE_ARGV 1 _PGPS "INTERPRETER;COMPILER;LIBRARY;INCLUDE" "" "IMPLEMENTATIONS;VERSION")

  if (NOT _PGPS_IMPLEMENTATIONS)
    set (_PGPS_IMPLEMENTATIONS ${_${_PYTHON_PREFIX}_FIND_IMPLEMENTATIONS})
  endif()

  if (DEFINED _${_PYTHON_PREFIX}_ABIFLAGS)
    set (abi "${_${_PYTHON_PREFIX}_ABIFLAGS}")
  else()
    set (abi "mu" "m" "u" "")
  endif()

  set (path_suffixes)

  foreach (implementation IN LISTS _PGPS_IMPLEMENTATIONS)
    if (implementation STREQUAL "CPython")
      if (_PGPS_INTERPRETER)
        list (APPEND path_suffixes bin Scripts)
      else()
        foreach (version IN LISTS _PGPS_VERSION)
          if (_PGPS_LIBRARY)
            if (CMAKE_LIBRARY_ARCHITECTURE)
              list (APPEND path_suffixes lib/${CMAKE_LIBRARY_ARCHITECTURE})
            endif()
            list (APPEND path_suffixes lib libs)

            if (CMAKE_LIBRARY_ARCHITECTURE)
              set (suffixes "${abi}")
              if (suffixes)
                list (TRANSFORM suffixes PREPEND "lib/python${_PGPS_VERSION}/config-${_PGPS_VERSION}")
                list (TRANSFORM suffixes APPEND "-${CMAKE_LIBRARY_ARCHITECTURE}")
              else()
                set (suffixes "lib/python${_PGPS_VERSION}/config-${_PGPS_VERSION}-${CMAKE_LIBRARY_ARCHITECTURE}")
              endif()
              list (APPEND path_suffixes ${suffixes})
            endif()
            set (suffixes "${abi}")
            if (suffixes)
              list (TRANSFORM suffixes PREPEND "lib/python${_PGPS_VERSION}/config-${_PGPS_VERSION}")
            else()
              set (suffixes "lib/python${_PGPS_VERSION}/config-${_PGPS_VERSION}")
            endif()
            list (APPEND path_suffixes ${suffixes})
          elseif (_PGPS_INCLUDE)
            set (suffixes "${abi}")
            if (suffixes)
              list (TRANSFORM suffixes PREPEND "include/python${_PGPS_VERSION}")
            else()
              set (suffixes "include/python${_PGPS_VERSION}")
            endif()
            list (APPEND path_suffixes ${suffixes} include)
          endif()
        endforeach()
      endif()
    elseif (implementation STREQUAL "IronPython")
      if (_PGPS_INTERPRETER OR _PGPS_COMPILER)
        foreach (version IN LISTS _PGPS_VERSION)
          list (APPEND path_suffixes "share/ironpython${version}")
        endforeach()
        list (APPEND path_suffixes ${_${_PYTHON_PREFIX}_IRON_PYTHON_PATH_SUFFIXES})
      endif()
    elseif (implementation STREQUAL "PyPy")
      if (_PGPS_INTERPRETER)
        list (APPEND path_suffixes ${_${_PYTHON_PREFIX}_PYPY_EXECUTABLE_PATH_SUFFIXES})
      elseif (_PGPS_LIBRARY)
        list (APPEND path_suffixes ${_${_PYTHON_PREFIX}_PYPY_LIBRARY_PATH_SUFFIXES})
      elseif (_PGPS_INCLUDE)
        list (APPEND path_suffixes ${_${_PYTHON_PREFIX}_PYPY_INCLUDE_PATH_SUFFIXES})
      endif()
    endif()
  endforeach()
  list (REMOVE_DUPLICATES path_suffixes)

  set (${_PYTHON_PGPS_PATH_SUFFIXES} ${path_suffixes} PARENT_SCOPE)
endfunction()

function (_PYTHON_GET_NAMES _PYTHON_PGN_NAMES)
  cmake_parse_arguments (PARSE_ARGV 1 _PGN "POSIX;INTERPRETER;COMPILER;CONFIG;LIBRARY;WIN32;DEBUG" "" "IMPLEMENTATIONS;VERSION")

  if (NOT _PGN_IMPLEMENTATIONS)
    set (_PGN_IMPLEMENTATIONS ${_${_PYTHON_PREFIX}_FIND_IMPLEMENTATIONS})
  endif()

  set (names)

  foreach (implementation IN LISTS _PGN_IMPLEMENTATIONS)
    if (implementation STREQUAL "CPython")
      foreach (version IN LISTS _PGN_VERSION)
        if (_PGN_WIN32)
          string (REPLACE "." "" version_no_dots ${version})

          set (name python${version_no_dots})
          if (_PGN_DEBUG)
            string (APPEND name "_d")
          endif()

          list (APPEND names "${name}")
        endif()

        if (_PGN_POSIX)
          if (DEFINED _${_PYTHON_PREFIX}_ABIFLAGS)
            set (abi "${_${_PYTHON_PREFIX}_ABIFLAGS}")
          else()
            if (_PGN_INTERPRETER OR _PGN_CONFIG)
              set (abi "")
            else()
              set (abi "mu" "m" "u" "")
            endif()
          endif()

          if (abi)
            if (_PGN_CONFIG AND DEFINED CMAKE_LIBRARY_ARCHITECTURE)
              set (abinames "${abi}")
              list (TRANSFORM abinames PREPEND "${CMAKE_LIBRARY_ARCHITECTURE}-python${version}")
              list (TRANSFORM abinames APPEND "-config")
              list (APPEND names ${abinames})
            endif()
            set (abinames "${abi}")
            list (TRANSFORM abinames PREPEND "python${version}")
            if (_PGN_CONFIG)
              list (TRANSFORM abinames APPEND "-config")
            endif()
            list (APPEND names ${abinames})
          else()
            unset (abinames)
            if (_PGN_CONFIG AND DEFINED CMAKE_LIBRARY_ARCHITECTURE)
              set (abinames "${CMAKE_LIBRARY_ARCHITECTURE}-python${version}")
            endif()
            list (APPEND abinames "python${version}")
            if (_PGN_CONFIG)
              list (TRANSFORM abinames APPEND "-config")
            endif()
            list (APPEND names ${abinames})
          endif()
        endif()
      endforeach()
      if (_PGN_INTERPRETER)
        list (APPEND names python${_${_PYTHON_PREFIX}_REQUIRED_VERSION_MAJOR} python)
      endif()
    elseif (implementation STREQUAL "IronPython")
      if (_PGN_INTERPRETER)
        if (NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
          # Do not use wrapper script on Linux because it is buggy: -c interpreter option cannot be used
          foreach (version IN LISTS _PGN_VERSION)
            list (APPEND names "ipy${version}")
          endforeach()
        endif()
        list (APPEND names ${_${_PYTHON_PREFIX}_IRON_PYTHON_INTERPRETER_NAMES})
      elseif (_PGN_COMPILER)
        list (APPEND names ${_${_PYTHON_PREFIX}_IRON_PYTHON_COMPILER_NAMES})
      endif()
    elseif (implementation STREQUAL "PyPy")
      if (_PGN_INTERPRETER)
        list (APPEND names ${_${_PYTHON_PREFIX}_PYPY_NAMES})
      elseif (_PGN_LIBRARY)
        if (_PGN_WIN32)
          foreach (version IN LISTS _PGN_VERSION)
            string (REPLACE "." "" version_no_dots ${version})

            set (name "python${version_no_dots}")
            if (_PGN_DEBUG)
              string (APPEND name "_d")
            endif()
            list (APPEND names "${name}")
          endforeach()
        endif()
        list (APPEND names ${_${_PYTHON_PREFIX}_PYPY_LIB_NAMES})
      endif()
    endif()
  endforeach()

  set (${_PYTHON_PGN_NAMES} ${names} PARENT_SCOPE)
endfunction()

function (_PYTHON_GET_CONFIG_VAR _PYTHON_PGCV_VALUE NAME)
  unset (${_PYTHON_PGCV_VALUE} PARENT_SCOPE)

  if (NOT NAME MATCHES "^(PREFIX|ABIFLAGS|CONFIGDIR|INCLUDES|LIBS|SOABI)$")
    return()
  endif()

  if (_${_PYTHON_PREFIX}_CONFIG)
    if (NAME STREQUAL "SOABI")
      set (config_flag "--extension-suffix")
    else()
      set (config_flag "--${NAME}")
    endif()
    string (TOLOWER "${config_flag}" config_flag)
    execute_process (COMMAND "${_${_PYTHON_PREFIX}_CONFIG}" ${config_flag}
                     RESULT_VARIABLE _result
                     OUTPUT_VARIABLE _values
                     ERROR_QUIET
                     OUTPUT_STRIP_TRAILING_WHITESPACE)
    if (_result)
      unset (_values)
    else()
      if (NAME STREQUAL "INCLUDES")
        # do some clean-up
        string (REGEX MATCHALL "(-I|-iwithsysroot)[ ]*[^ ]+" _values "${_values}")
        string (REGEX REPLACE "(-I|-iwithsysroot)[ ]*" "" _values "${_values}")
        list (REMOVE_DUPLICATES _values)
      elseif (NAME STREQUAL "SOABI")
        # clean-up: remove prefix character and suffix
        if (_values MATCHES "^(\\.${CMAKE_SHARED_LIBRARY_SUFFIX}|\\.so|\\.pyd)$")
          set(_values "")
        else()
          string (REGEX REPLACE "^[.-](.+)(${CMAKE_SHARED_LIBRARY_SUFFIX}|\\.(so|pyd))$" "\\1" _values "${_values}")
        endif()
      endif()
    endif()
  endif()

  if (_${_PYTHON_PREFIX}_EXECUTABLE AND NOT CMAKE_CROSSCOMPILING)
    if (NAME STREQUAL "PREFIX")
      execute_process (COMMAND ${_${_PYTHON_PREFIX}_INTERPRETER_LAUNCHER} "${_${_PYTHON_PREFIX}_EXECUTABLE}" -c "import sys\ntry:\n   from distutils import sysconfig\n   sys.stdout.write(';'.join([sysconfig.PREFIX,sysconfig.EXEC_PREFIX,sysconfig.BASE_EXEC_PREFIX]))\nexcept Exception:\n   import sysconfig\n   sys.stdout.write(';'.join([sysconfig.get_config_var('base') or '', sysconfig.get_config_var('installed_base') or '']))"
                       RESULT_VARIABLE _result
                       OUTPUT_VARIABLE _values
                       ERROR_QUIET
                       OUTPUT_STRIP_TRAILING_WHITESPACE)
      if (_result)
        unset (_values)
      else()
        list (REMOVE_DUPLICATES _values)
      endif()
    elseif (NAME STREQUAL "INCLUDES")
      if (WIN32)
        set (_scheme "nt")
      else()
        set (_scheme "posix_prefix")
      endif()
      execute_process (COMMAND ${_${_PYTHON_PREFIX}_INTERPRETER_LAUNCHER} "${_${_PYTHON_PREFIX}_EXECUTABLE}" -c
                               "import sys\ntry:\n   from distutils import sysconfig\n   sys.stdout.write(';'.join([sysconfig.get_python_inc(plat_specific=True),sysconfig.get_python_inc(plat_specific=False)]))\nexcept Exception:\n   import sysconfig\n   sys.stdout.write(';'.join([sysconfig.get_path('platinclude'),sysconfig.get_path('platinclude','${_scheme}'),sysconfig.get_path('include'),sysconfig.get_path('include','${_scheme}')]))"
                       RESULT_VARIABLE _result
                       OUTPUT_VARIABLE _values
                       ERROR_QUIET
                       OUTPUT_STRIP_TRAILING_WHITESPACE)
      if (_result)
        unset (_values)
      else()
        list (REMOVE_DUPLICATES _values)
      endif()
    elseif (NAME STREQUAL "SOABI")
      execute_process (COMMAND ${_${_PYTHON_PREFIX}_INTERPRETER_LAUNCHER} "${_${_PYTHON_PREFIX}_EXECUTABLE}" -c
                               "import sys\ntry:\n   from distutils import sysconfig\n   sys.stdout.write(';'.join([sysconfig.get_config_var('SOABI') or '',sysconfig.get_config_var('EXT_SUFFIX') or '',sysconfig.get_config_var('SO') or '']))\nexcept Exception:\n   import sysconfig;sys.stdout.write(';'.join([sysconfig.get_config_var('SOABI') or '',sysconfig.get_config_var('EXT_SUFFIX') or '',sysconfig.get_config_var('SO') or '']))"
                       RESULT_VARIABLE _result
                       OUTPUT_VARIABLE _soabi
                       ERROR_QUIET
                       OUTPUT_STRIP_TRAILING_WHITESPACE)
      if (_result)
        unset (_values)
      else()
        foreach (_item IN LISTS _soabi)
          if (_item)
            set (_values "${_item}")
            break()
          endif()
        endforeach()
        if (_values)
          # clean-up: remove prefix character and suffix
          if (_values MATCHES "^(\\.${CMAKE_SHARED_LIBRARY_SUFFIX}|\\.so|\\.pyd)$")
            set(_values "")
          else()
            string (REGEX REPLACE "^[.-](.+)(${CMAKE_SHARED_LIBRARY_SUFFIX}|\\.(so|pyd))$" "\\1" _values "${_values}")
          endif()
        endif()
      endif()
    else()
      set (config_flag "${NAME}")
      if (NAME STREQUAL "CONFIGDIR")
        set (config_flag "LIBPL")
      endif()
      execute_process (COMMAND ${_${_PYTHON_PREFIX}_INTERPRETER_LAUNCHER} "${_${_PYTHON_PREFIX}_EXECUTABLE}" -c
                               "import sys\ntry:\n   from distutils import sysconfig\n   sys.stdout.write(sysconfig.get_config_var('${config_flag}'))\nexcept Exception:\n   import sysconfig\n   sys.stdout.write(sysconfig.get_config_var('${config_flag}'))"
                       RESULT_VARIABLE _result
                       OUTPUT_VARIABLE _values
                       ERROR_QUIET
                       OUTPUT_STRIP_TRAILING_WHITESPACE)
      if (_result)
        unset (_values)
      endif()
    endif()
  endif()

  if (NAME STREQUAL "ABIFLAGS" OR NAME STREQUAL "SOABI")
    set (${_PYTHON_PGCV_VALUE} "${_values}" PARENT_SCOPE)
    return()
  endif()

  if (NOT _values OR _values STREQUAL "None")
    return()
  endif()

  if (NAME STREQUAL "LIBS")
    # do some clean-up
    string (REGEX MATCHALL "-(l|framework)[ ]*[^ ]+" _values "${_values}")
    # remove elements relative to python library itself
    list (FILTER _values EXCLUDE REGEX "-lpython")
    list (REMOVE_DUPLICATES _values)
  endif()

  if (WIN32 AND NAME MATCHES "^(PREFIX|CONFIGDIR|INCLUDES)$")
    file (TO_CMAKE_PATH "${_values}" _values)
  endif()

  set (${_PYTHON_PGCV_VALUE} "${_values}" PARENT_SCOPE)
endfunction()

function (_PYTHON_GET_VERSION)
  cmake_parse_arguments (PARSE_ARGV 0 _PGV "LIBRARY;INCLUDE" "PREFIX" "")

  unset (${_PGV_PREFIX}VERSION PARENT_SCOPE)
  unset (${_PGV_PREFIX}VERSION_MAJOR PARENT_SCOPE)
  unset (${_PGV_PREFIX}VERSION_MINOR PARENT_SCOPE)
  unset (${_PGV_PREFIX}VERSION_PATCH PARENT_SCOPE)
  unset (${_PGV_PREFIX}ABI PARENT_SCOPE)

  if (_PGV_LIBRARY)
    # retrieve version and abi from library name
    if (_${_PYTHON_PREFIX}_LIBRARY_RELEASE)
      get_filename_component (library_name "${_${_PYTHON_PREFIX}_LIBRARY_RELEASE}" NAME)
      # extract version from library name
      if (library_name MATCHES "python([23])([0-9]+)")
        set (${_PGV_PREFIX}VERSION_MAJOR "${CMAKE_MATCH_1}" PARENT_SCOPE)
        set (${_PGV_PREFIX}VERSION_MINOR "${CMAKE_MATCH_2}" PARENT_SCOPE)
        set (${_PGV_PREFIX}VERSION "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}" PARENT_SCOPE)
        set (${_PGV_PREFIX}ABI "" PARENT_SCOPE)
      elseif (library_name MATCHES "python([23])\\.([0-9]+)([dmu]*)")
        set (${_PGV_PREFIX}VERSION_MAJOR "${CMAKE_MATCH_1}" PARENT_SCOPE)
        set (${_PGV_PREFIX}VERSION_MINOR "${CMAKE_MATCH_2}" PARENT_SCOPE)
        set (${_PGV_PREFIX}VERSION "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}" PARENT_SCOPE)
        set (${_PGV_PREFIX}ABI "${CMAKE_MATCH_3}" PARENT_SCOPE)
      elseif (library_name MATCHES "pypy(3)?-c")
        set (version "${CMAKE_MATCH_1}")
        if (version EQUAL "3")
          set (${_PGV_PREFIX}VERSION_MAJOR "3" PARENT_SCOPE)
          set (${_PGV_PREFIX}VERSION "3" PARENT_SCOPE)
        else()
          set (${_PGV_PREFIX}VERSION_MAJOR "2" PARENT_SCOPE)
          set (${_PGV_PREFIX}VERSION "2" PARENT_SCOPE)
        endif()
        set (${_PGV_PREFIX}ABI "" PARENT_SCOPE)
      endif()
    endif()
  else()
    if (_${_PYTHON_PREFIX}_INCLUDE_DIR)
      # retrieve version from header file
      file (STRINGS "${_${_PYTHON_PREFIX}_INCLUDE_DIR}/patchlevel.h" version
            REGEX "^#define[ \t]+PY_VERSION[ \t]+\"[^\"]+\"")
      string (REGEX REPLACE "^#define[ \t]+PY_VERSION[ \t]+\"([^\"]+)\".*" "\\1"
                            version "${version}")
      string (REGEX MATCHALL "[0-9]+" versions "${version}")
      list (GET versions 0 version_major)
      list (GET versions 1 version_minor)
      list (GET versions 2 version_patch)

      set (${_PGV_PREFIX}VERSION "${version_major}.${version_minor}.${version_patch}" PARENT_SCOPE)
      set (${_PGV_PREFIX}VERSION_MAJOR ${version_major} PARENT_SCOPE)
      set (${_PGV_PREFIX}VERSION_MINOR ${version_minor} PARENT_SCOPE)
      set (${_PGV_PREFIX}VERSION_PATCH ${version_patch} PARENT_SCOPE)

      # compute ABI flags
      if (version_major VERSION_GREATER "2")
        file (STRINGS "${_${_PYTHON_PREFIX}_INCLUDE_DIR}/pyconfig.h" config REGEX "(Py_DEBUG|WITH_PYMALLOC|Py_UNICODE_SIZE|MS_WIN32)")
        set (abi)
        if (config MATCHES "#[ ]*define[ ]+MS_WIN32")
          # ABI not used on Windows
          set (abi "")
        else()
          if (NOT config)
            # pyconfig.h can be a wrapper to a platform specific pyconfig.h
            # In this case, try to identify ABI from include directory
            if (_${_PYTHON_PREFIX}_INCLUDE_DIR MATCHES "python${version_major}\\.${version_minor}+([dmu]*)")
              set (abi "${CMAKE_MATCH_1}")
            else()
              set (abi "")
            endif()
          else()
            if (config MATCHES "#[ ]*define[ ]+Py_DEBUG[ ]+1")
              string (APPEND abi "d")
            endif()
            if (config MATCHES "#[ ]*define[ ]+WITH_PYMALLOC[ ]+1")
              string (APPEND abi "m")
            endif()
            if (config MATCHES "#[ ]*define[ ]+Py_UNICODE_SIZE[ ]+4")
              string (APPEND abi "u")
            endif()
          endif()
          set (${_PGV_PREFIX}ABI "${abi}" PARENT_SCOPE)
        endif()
      else()
        # ABI not supported
        set (${_PGV_PREFIX}ABI "" PARENT_SCOPE)
      endif()
    endif()
  endif()
endfunction()

function (_PYTHON_GET_LAUNCHER _PYTHON_PGL_NAME)
  cmake_parse_arguments (PARSE_ARGV 1 _PGL "INTERPRETER;COMPILER" "" "")

  unset ({_PYTHON_PGL_NAME} PARENT_SCOPE)

  if ((_PGL_INTERPRETER AND NOT _${_PYTHON_PREFIX}_EXECUTABLE)
      OR (_PGL_COMPILER AND NOT _${_PYTHON_PREFIX}_COMPILER))
    return()
  endif()

  if ("IronPython" IN_LIST _${_PYTHON_PREFIX}_FIND_IMPLEMENTATIONS
      AND NOT SYSTEM_NAME MATCHES "Windows|Linux")
    if (_PGL_INTERPRETER)
      get_filename_component (name "${_${_PYTHON_PREFIX}_EXECUTABLE}" NAME)
      get_filename_component (ext "${_${_PYTHON_PREFIX}_EXECUTABLE}" LAST_EXT)
      if (name IN_LIST _${_PYTHON_PREFIX}_IRON_PYTHON_INTERPRETER_NAMES
          AND ext STREQUAL ".exe")
        set (${_PYTHON_PGL_NAME} "${${_PYTHON_PREFIX}_DOTNET_LAUNCHER}" PARENT_SCOPE)
      endif()
    else()
      get_filename_component (name "${_${_PYTHON_PREFIX}_COMPILER}" NAME)
      get_filename_component (ext "${_${_PYTHON_PREFIX}_COMPILER}" LAST_EXT)
      if (name IN_LIST _${_PYTHON_PREFIX}_IRON_PYTHON_COMPILER_NAMES
          AND ext STREQUAL ".exe")
        set (${_PYTHON_PGL_NAME} "${${_PYTHON_PREFIX}_DOTNET_LAUNCHER}" PARENT_SCOPE)
      endif()
    endif()
  endif()
endfunction()


function (_PYTHON_VALIDATE_INTERPRETER)
  if (NOT _${_PYTHON_PREFIX}_EXECUTABLE)
    return()
  endif()

  cmake_parse_arguments (PARSE_ARGV 0 _PVI "IN_RANGE;EXACT;CHECK_EXISTS" "VERSION" "")

  if (_PVI_CHECK_EXISTS AND NOT EXISTS "${_${_PYTHON_PREFIX}_EXECUTABLE}")
    # interpreter does not exist anymore
    set (_${_PYTHON_PREFIX}_Interpreter_REASON_FAILURE "Cannot find the interpreter \"${_${_PYTHON_PREFIX}_EXECUTABLE}\"" PARENT_SCOPE)
    set_property (CACHE _${_PYTHON_PREFIX}_EXECUTABLE PROPERTY VALUE "${_PYTHON_PREFIX}_EXECUTABLE-NOTFOUND")
    return()
  endif()

  _python_get_launcher (launcher INTERPRETER)

  # validate ABI compatibility
  if (DEFINED _${_PYTHON_PREFIX}_FIND_ABI)
    execute_process (COMMAND ${launcher} "${_${_PYTHON_PREFIX}_EXECUTABLE}" -c
                             "import sys; sys.stdout.write(sys.abiflags)"
                     RESULT_VARIABLE result
                     OUTPUT_VARIABLE abi
                     ERROR_QUIET
                     OUTPUT_STRIP_TRAILING_WHITESPACE)
    if (result)
      # assume ABI is not supported
      set (abi "")
    endif()
    if (NOT abi IN_LIST _${_PYTHON_PREFIX}_ABIFLAGS)
      # incompatible ABI
      set (_${_PYTHON_PREFIX}_Interpreter_REASON_FAILURE "Wrong ABI for the interpreter \"${_${_PYTHON_PREFIX}_EXECUTABLE}\"" PARENT_SCOPE)
      set_property (CACHE _${_PYTHON_PREFIX}_EXECUTABLE PROPERTY VALUE "${_PYTHON_PREFIX}_EXECUTABLE-NOTFOUND")
      return()
    endif()
  endif()

  if (_PVI_IN_RANGE OR _PVI_VERSION)
    # retrieve full version
    execute_process (COMMAND ${launcher} "${_${_PYTHON_PREFIX}_EXECUTABLE}" -c
                             "import sys; sys.stdout.write('.'.join([str(x) for x in sys.version_info[:3]]))"
                     RESULT_VARIABLE result
                     OUTPUT_VARIABLE version
                     ERROR_QUIET
                     OUTPUT_STRIP_TRAILING_WHITESPACE)
    if (result)
      # interpreter is not usable
      set (_${_PYTHON_PREFIX}_Interpreter_REASON_FAILURE "Cannot use the interpreter \"${_${_PYTHON_PREFIX}_EXECUTABLE}\"" PARENT_SCOPE)
      set_property (CACHE _${_PYTHON_PREFIX}_EXECUTABLE PROPERTY VALUE "${_PYTHON_PREFIX}_EXECUTABLE-NOTFOUND")
      return()
    endif()

    if (_PVI_VERSION)
      # check against specified version
      ## compute number of components for version
      string (REGEX REPLACE "[^.]" "" dots "${_PVI_VERSION}")
      ## add one dot because there is one dot less than there are components
      string (LENGTH "${dots}." count)
      if (count GREATER 3)
        set (count 3)
      endif()
      set (version_regex "^[0-9]+")
      if (count EQUAL 3)
        string (APPEND version_regex "\\.[0-9]+\\.[0-9]+")
      elseif (count EQUAL 2)
        string (APPEND version_regex "\\.[0-9]+")
      endif()
      # extract needed range
      string (REGEX MATCH "${version_regex}" version "${version}")

      if (_PVI_EXACT AND NOT version VERSION_EQUAL _PVI_VERSION)
        # interpreter has wrong version
        set (_${_PYTHON_PREFIX}_Interpreter_REASON_FAILURE "Wrong version for the interpreter \"${_${_PYTHON_PREFIX}_EXECUTABLE}\"" PARENT_SCOPE)
        set_property (CACHE _${_PYTHON_PREFIX}_EXECUTABLE PROPERTY VALUE "${_PYTHON_PREFIX}_EXECUTABLE-NOTFOUND")
        return()
      else()
        # check that version is OK
        string(REGEX REPLACE "^([0-9]+)\\.?.*$" "\\1" major_version "${version}")
        string(REGEX REPLACE "^([0-9]+)\\.?.*$" "\\1" expected_major_version "${_PVI_VERSION}")
        if (NOT major_version VERSION_EQUAL expected_major_version
            OR NOT version VERSION_GREATER_EQUAL _PVI_VERSION)
          set (_${_PYTHON_PREFIX}_Interpreter_REASON_FAILURE "Wrong version for the interpreter \"${_${_PYTHON_PREFIX}_EXECUTABLE}\"" PARENT_SCOPE)
          set_property (CACHE _${_PYTHON_PREFIX}_EXECUTABLE PROPERTY VALUE "${_PYTHON_PREFIX}_EXECUTABLE-NOTFOUND")
          return()
        endif()
      endif()
    endif()

    if (_PVI_IN_RANGE)
      # check if version is in the requested range
      find_package_check_version ("${version}" in_range HANDLE_VERSION_RANGE)
      if (NOT in_range)
        # interpreter has invalid version
        set (_${_PYTHON_PREFIX}_Interpreter_REASON_FAILURE "Wrong version for the interpreter \"${_${_PYTHON_PREFIX}_EXECUTABLE}\"" PARENT_SCOPE)
        set_property (CACHE _${_PYTHON_PREFIX}_EXECUTABLE PROPERTY VALUE "${_PYTHON_PREFIX}_EXECUTABLE-NOTFOUND")
        return()
      endif()
    endif()
  else()
    get_filename_component (python_name "${_${_PYTHON_PREFIX}_EXECUTABLE}" NAME)
    if (NOT python_name STREQUAL "python${_${_PYTHON_PREFIX}_REQUIRED_VERSION_MAJOR}${CMAKE_EXECUTABLE_SUFFIX}")
      # executable found do not have version in name
      # ensure major version is OK
      execute_process (COMMAND ${launcher} "${_${_PYTHON_PREFIX}_EXECUTABLE}" -c
                               "import sys; sys.stdout.write(str(sys.version_info[0]))"
                       RESULT_VARIABLE result
                       OUTPUT_VARIABLE version
                       ERROR_QUIET
                       OUTPUT_STRIP_TRAILING_WHITESPACE)
      if (result OR NOT version EQUAL _${_PYTHON_PREFIX}_REQUIRED_VERSION_MAJOR)
        # interpreter not usable or has wrong major version
        if (result)
          set (_${_PYTHON_PREFIX}_Interpreter_REASON_FAILURE "Cannot use the interpreter \"${_${_PYTHON_PREFIX}_EXECUTABLE}\"" PARENT_SCOPE)
        else()
          set (_${_PYTHON_PREFIX}_Interpreter_REASON_FAILURE "Wrong major version for the interpreter \"${_${_PYTHON_PREFIX}_EXECUTABLE}\"" PARENT_SCOPE)
        endif()
        set_property (CACHE _${_PYTHON_PREFIX}_EXECUTABLE PROPERTY VALUE "${_PYTHON_PREFIX}_EXECUTABLE-NOTFOUND")
        return()
      endif()
    endif()
  endif()

  if (CMAKE_SIZEOF_VOID_P AND ("Development.Module" IN_LIST ${_PYTHON_PREFIX}_FIND_COMPONENTS
        OR "Development.Embed" IN_LIST ${_PYTHON_PREFIX}_FIND_COMPONENTS)
      AND NOT CMAKE_CROSSCOMPILING)
    # In this case, interpreter must have same architecture as environment
    execute_process (COMMAND ${launcher} "${_${_PYTHON_PREFIX}_EXECUTABLE}" -c
                             "import sys, struct; sys.stdout.write(str(struct.calcsize(\"P\")))"
                     RESULT_VARIABLE result
                     OUTPUT_VARIABLE size
                     ERROR_QUIET
                     OUTPUT_STRIP_TRAILING_WHITESPACE)
    if (result OR NOT size EQUAL CMAKE_SIZEOF_VOID_P)
      # interpreter not usable or has wrong architecture
      if (result)
        set (_${_PYTHON_PREFIX}_Interpreter_REASON_FAILURE "Cannot use the interpreter \"${_${_PYTHON_PREFIX}_EXECUTABLE}\"" PARENT_SCOPE)
      else()
        set (_${_PYTHON_PREFIX}_Interpreter_REASON_FAILURE "Wrong architecture for the interpreter \"${_${_PYTHON_PREFIX}_EXECUTABLE}\"" PARENT_SCOPE)
      endif()
      set_property (CACHE _${_PYTHON_PREFIX}_EXECUTABLE PROPERTY VALUE "${_PYTHON_PREFIX}_EXECUTABLE-NOTFOUND")
      return()
    endif()
  endif()
endfunction()


function (_PYTHON_VALIDATE_COMPILER)
  if (NOT _${_PYTHON_PREFIX}_COMPILER)
    return()
  endif()

  cmake_parse_arguments (PARSE_ARGV 0 _PVC "IN_RANGE;EXACT;CHECK_EXISTS" "VERSION" "")

  if (_PVC_CHECK_EXISTS AND NOT EXISTS "${_${_PYTHON_PREFIX}_COMPILER}")
    # Compiler does not exist anymore
    set (_${_PYTHON_PREFIX}_Compiler_REASON_FAILURE "Cannot find the compiler \"${_${_PYTHON_PREFIX}_COMPILER}\"" PARENT_SCOPE)
    set_property (CACHE _${_PYTHON_PREFIX}_COMPILER PROPERTY VALUE "${_PYTHON_PREFIX}_COMPILER-NOTFOUND")
    return()
  endif()

  _python_get_launcher (launcher COMPILER)

  # retrieve python environment version from compiler
  set (working_dir "${CMAKE_CURRENT_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/PythonCompilerVersion.dir")
  file (WRITE "${working_dir}/version.py" "import sys; sys.stdout.write('.'.join([str(x) for x in sys.version_info[:3]]))\n")
  execute_process (COMMAND ${launcher} "${_${_PYTHON_PREFIX}_COMPILER}"
                           ${_${_PYTHON_PREFIX}_IRON_PYTHON_COMPILER_ARCH_FLAGS}
                           /target:exe /embed "${working_dir}/version.py"
                   WORKING_DIRECTORY "${working_dir}"
                   OUTPUT_QUIET
                   ERROR_QUIET
                   OUTPUT_STRIP_TRAILING_WHITESPACE)
  get_filename_component (ir_dir "${_${_PYTHON_PREFIX}_COMPILER}" DIRECTORY)
  execute_process (COMMAND "${CMAKE_COMMAND}" -E env "MONO_PATH=${ir_dir}"
                                              ${${_PYTHON_PREFIX}_DOTNET_LAUNCHER} "${working_dir}/version.exe"
                   WORKING_DIRECTORY "${working_dir}"
                   RESULT_VARIABLE result
                   OUTPUT_VARIABLE version
                   ERROR_QUIET)
  file (REMOVE_RECURSE "${working_dir}")
  if (result)
    # compiler is not usable
    set (_${_PYTHON_PREFIX}_Compiler_REASON_FAILURE "Cannot use the compiler \"${_${_PYTHON_PREFIX}_COMPILER}\"" PARENT_SCOPE)
    set_property (CACHE _${_PYTHON_PREFIX}_COMPILER PROPERTY VALUE "${_PYTHON_PREFIX}_COMPILER-NOTFOUND")
    return()
  endif()

  if (_PVC_VERSION OR _PVC_IN_RANGE)
    if (_PVC_VERSION)
      # check against specified version
      ## compute number of components for version
      string (REGEX REPLACE "[^.]" "" dots "${_PVC_VERSION}")
      ## add one dot because there is one dot less than there are components
      string (LENGTH "${dots}." count)
      if (count GREATER 3)
        set (count 3)
      endif()
      set (version_regex "^[0-9]+")
      if (count EQUAL 3)
        string (APPEND version_regex "\\.[0-9]+\\.[0-9]+")
      elseif (count EQUAL 2)
        string (APPEND version_regex "\\.[0-9]+")
      endif()
      # extract needed range
      string (REGEX MATCH "${version_regex}" version "${version}")

      if (_PVC_EXACT AND NOT version VERSION_EQUAL _PVC_VERSION)
        # interpreter has wrong version
        set (_${_PYTHON_PREFIX}_Compiler_REASON_FAILURE "Wrong version for the compiler \"${_${_PYTHON_PREFIX}_COMPILER}\"" PARENT_SCOPE)
        set_property (CACHE _${_PYTHON_PREFIX}_COMPILER PROPERTY VALUE "${_PYTHON_PREFIX}_COMPILER-NOTFOUND")
        return()
      else()
        # check that version is OK
        string(REGEX REPLACE "^([0-9]+)\\.?.*$" "\\1" major_version "${version}")
        string(REGEX REPLACE "^([0-9]+)\\.?.*$" "\\1" expected_major_version "${_PVC_VERSION}")
        if (NOT major_version VERSION_EQUAL expected_major_version
            OR NOT version VERSION_GREATER_EQUAL _PVC_VERSION)
          set (_${_PYTHON_PREFIX}_Compiler_REASON_FAILURE "Wrong version for the compiler \"${_${_PYTHON_PREFIX}_COMPILER}\"" PARENT_SCOPE)
          set_property (CACHE _${_PYTHON_PREFIX}_COMPILER PROPERTY VALUE "${_PYTHON_PREFIX}_COMPILER-NOTFOUND")
          return()
        endif()
      endif()
    endif()

    if (_PVC_IN_RANGE)
      # check if version is in the requested range
      find_package_check_version ("${version}" in_range HANDLE_VERSION_RANGE)
      if (NOT in_range)
        # interpreter has invalid version
        set (_${_PYTHON_PREFIX}_Compiler_REASON_FAILURE "Wrong version for the compiler \"${_${_PYTHON_PREFIX}_COMPILER}\"" PARENT_SCOPE)
        set_property (CACHE _${_PYTHON_PREFIX}_COMPILER PROPERTY VALUE "${_PYTHON_PREFIX}_COMPILER-NOTFOUND")
        return()
      endif()
    endif()
  else()
    string(REGEX REPLACE "^([0-9]+)\\.?.*$" "\\1" major_version "${version}")
    if (NOT major_version EQUAL _${_PYTHON_PREFIX}_REQUIRED_VERSION_MAJOR)
      # Compiler has wrong major version
      set (_${_PYTHON_PREFIX}_Compiler_REASON_FAILURE "Wrong major version for the compiler \"${_${_PYTHON_PREFIX}_COMPILER}\"" PARENT_SCOPE)
      set_property (CACHE _${_PYTHON_PREFIX}_COMPILER PROPERTY VALUE "${_PYTHON_PREFIX}_COMPILER-NOTFOUND")
      return()
    endif()
  endif()
endfunction()


function (_PYTHON_VALIDATE_LIBRARY)
  if (NOT _${_PYTHON_PREFIX}_LIBRARY_RELEASE)
    unset (_${_PYTHON_PREFIX}_LIBRARY_DEBUG)
    return()
  endif()

  cmake_parse_arguments (PARSE_ARGV 0 _PVL "IN_RANGE;EXACT;CHECK_EXISTS" "VERSION" "")

  if (_PVL_CHECK_EXISTS AND NOT EXISTS "${_${_PYTHON_PREFIX}_LIBRARY_RELEASE}")
    # library does not exist anymore
    set (_${_PYTHON_PREFIX}_Development_REASON_FAILURE "Cannot find the library \"${_${_PYTHON_PREFIX}_LIBRARY_RELEASE}\"" PARENT_SCOPE)
    set_property (CACHE _${_PYTHON_PREFIX}_LIBRARY_RELEASE PROPERTY VALUE "${_PYTHON_PREFIX}_LIBRARY_RELEASE-NOTFOUND")
    if (WIN32)
      set_property (CACHE _${_PYTHON_PREFIX}_LIBRARY_DEBUG PROPERTY VALUE "${_PYTHON_PREFIX}_LIBRARY_DEBUG-NOTFOUND")
    endif()
    set_property (CACHE _${_PYTHON_PREFIX}_INCLUDE_DIR PROPERTY VALUE "${_PYTHON_PREFIX}_INCLUDE_DIR-NOTFOUND")
    return()
  endif()

  # retrieve version and abi from library name
  _python_get_version (LIBRARY PREFIX lib_)

  if (DEFINED _${_PYTHON_PREFIX}_FIND_ABI AND NOT lib_ABI IN_LIST _${_PYTHON_PREFIX}_ABIFLAGS)
    # incompatible ABI
    set (_${_PYTHON_PREFIX}_Development_REASON_FAILURE "Wrong ABI for the library \"${_${_PYTHON_PREFIX}_LIBRARY_RELEASE}\"" PARENT_SCOPE)
    set_property (CACHE _${_PYTHON_PREFIX}_LIBRARY_RELEASE PROPERTY VALUE "${_PYTHON_PREFIX}_LIBRARY_RELEASE-NOTFOUND")
  else()
    if (_PVL_VERSION OR _PVL_IN_RANGE)
      if (_PVL_VERSION)
        # library have only major.minor information
        string (REGEX MATCH "[0-9](\\.[0-9]+)?" version "${_PVL_VERSION}")
        if ((_PVL_EXACT AND NOT lib_VERSION VERSION_EQUAL version) OR (lib_VERSION VERSION_LESS version))
          # library has wrong version
          set (_${_PYTHON_PREFIX}_Development_REASON_FAILURE "Wrong version for the library \"${_${_PYTHON_PREFIX}_LIBRARY_RELEASE}\"" PARENT_SCOPE)
          set_property (CACHE _${_PYTHON_PREFIX}_LIBRARY_RELEASE PROPERTY VALUE "${_PYTHON_PREFIX}_LIBRARY_RELEASE-NOTFOUND")
        endif()
      endif()

      if (_${_PYTHON_PREFIX}_LIBRARY_RELEASE AND _PVL_IN_RANGE)
        # check if library version is in the requested range
        find_package_check_version ("${lib_VERSION}" in_range HANDLE_VERSION_RANGE)
        if (NOT in_range)
          # library has wrong version
          set (_${_PYTHON_PREFIX}_Development_REASON_FAILURE "Wrong version for the library \"${_${_PYTHON_PREFIX}_LIBRARY_RELEASE}\"" PARENT_SCOPE)
          set_property (CACHE _${_PYTHON_PREFIX}_LIBRARY_RELEASE PROPERTY VALUE "${_PYTHON_PREFIX}_LIBRARY_RELEASE-NOTFOUND")
        endif()
      endif()
    else()
      if (NOT lib_VERSION_MAJOR VERSION_EQUAL _${_PYTHON_PREFIX}_REQUIRED_VERSION_MAJOR)
        # library has wrong major version
        set (_${_PYTHON_PREFIX}_Development_REASON_FAILURE "Wrong major version for the library \"${_${_PYTHON_PREFIX}_LIBRARY_RELEASE}\"" PARENT_SCOPE)
        set_property (CACHE _${_PYTHON_PREFIX}_LIBRARY_RELEASE PROPERTY VALUE "${_PYTHON_PREFIX}_LIBRARY_RELEASE-NOTFOUND")
      endif()
    endif()
  endif()

  if (NOT _${_PYTHON_PREFIX}_LIBRARY_RELEASE)
    if (WIN32)
      set_property (CACHE _${_PYTHON_PREFIX}_LIBRARY_DEBUG PROPERTY VALUE "${_PYTHON_PREFIX}_LIBRARY_DEBUG-NOTFOUND")
    endif()
    unset (_${_PYTHON_PREFIX}_RUNTIME_LIBRARY_RELEASE CACHE)
    unset (_${_PYTHON_PREFIX}_RUNTIME_LIBRARY_DEBUG CACHE)
    set_property (CACHE _${_PYTHON_PREFIX}_INCLUDE_DIR PROPERTY VALUE "${_PYTHON_PREFIX}_INCLUDE_DIR-NOTFOUND")
  endif()
endfunction()


function (_PYTHON_VALIDATE_INCLUDE_DIR)
  if (NOT _${_PYTHON_PREFIX}_INCLUDE_DIR)
    return()
  endif()

  cmake_parse_arguments (PARSE_ARGV 0 _PVID "IN_RANGE;EXACT;CHECK_EXISTS" "VERSION" "")

  if (_PVID_CHECK_EXISTS AND NOT EXISTS "${_${_PYTHON_PREFIX}_INCLUDE_DIR}")
    # include file does not exist anymore
    set (_${_PYTHON_PREFIX}_Development_REASON_FAILURE "Cannot find the directory \"${_${_PYTHON_PREFIX}_INCLUDE_DIR}\"" PARENT_SCOPE)
    set_property (CACHE _${_PYTHON_PREFIX}_INCLUDE_DIR PROPERTY VALUE "${_PYTHON_PREFIX}_INCLUDE_DIR-NOTFOUND")
    return()
  endif()

  # retrieve version from header file
  _python_get_version (INCLUDE PREFIX inc_)

  if (DEFINED _${_PYTHON_PREFIX}_FIND_ABI AND NOT inc_ABI IN_LIST _${_PYTHON_PREFIX}_ABIFLAGS)
    # incompatible ABI
    set (_${_PYTHON_PREFIX}_Development_REASON_FAILURE "Wrong ABI for the directory \"${_${_PYTHON_PREFIX}_INCLUDE_DIR}\"" PARENT_SCOPE)
    set_property (CACHE _${_PYTHON_PREFIX}_INCLUDE_DIR PROPERTY VALUE "${_PYTHON_PREFIX}_INCLUDE_DIR-NOTFOUND")
  else()
    if (_PVID_VERSION OR _PVID_IN_RANGE)
      if (_PVID_VERSION)
        if ((_PVID_EXACT AND NOT inc_VERSION VERSION_EQUAL expected_version) OR (inc_VERSION VERSION_LESS expected_version))
          # include dir has wrong version
          set (_${_PYTHON_PREFIX}_Development_REASON_FAILURE "Wrong version for the directory \"${_${_PYTHON_PREFIX}_INCLUDE_DIR}\"" PARENT_SCOPE)
          set_property (CACHE _${_PYTHON_PREFIX}_INCLUDE_DIR PROPERTY VALUE "${_PYTHON_PREFIX}_INCLUDE_DIR-NOTFOUND")
        endif()
      endif()

      if (_${_PYTHON_PREFIX}_INCLUDE_DIR AND PVID_IN_RANGE)
        # check if include dir is in the request range
        find_package_check_version ("${inc_VERSION}" in_range HANDLE_VERSION_RANGE)
        if (NOT in_range)
          # include dir has wrong version
          set (_${_PYTHON_PREFIX}_Development_REASON_FAILURE "Wrong version for the directory \"${_${_PYTHON_PREFIX}_INCLUDE_DIR}\"" PARENT_SCOPE)
          set_property (CACHE _${_PYTHON_PREFIX}_INCLUDE_DIR PROPERTY VALUE "${_PYTHON_PREFIX}_INCLUDE_DIR-NOTFOUND")
        endif()
      endif()
    else()
      if (NOT inc_VERSION_MAJOR VERSION_EQUAL _${_PYTHON_PREFIX}_REQUIRED_VERSION_MAJOR)
        # include dir has wrong major version
        set (_${_PYTHON_PREFIX}_Development_REASON_FAILURE "Wrong major version for the directory \"${_${_PYTHON_PREFIX}_INCLUDE_DIR}\"" PARENT_SCOPE)
        set_property (CACHE _${_PYTHON_PREFIX}_INCLUDE_DIR PROPERTY VALUE "${_PYTHON_PREFIX}_INCLUDE_DIR-NOTFOUND")
      endif()
    endif()
  endif()
endfunction()


function (_PYTHON_FIND_RUNTIME_LIBRARY _PYTHON_LIB)
  string (REPLACE "_RUNTIME" "" _PYTHON_LIB "${_PYTHON_LIB}")
  # look at runtime part on systems supporting it
  if (CMAKE_SYSTEM_NAME STREQUAL "Windows" OR
      (CMAKE_SYSTEM_NAME MATCHES "MSYS|CYGWIN"
        AND ${_PYTHON_LIB} MATCHES "${CMAKE_IMPORT_LIBRARY_SUFFIX}$"))
    set (CMAKE_FIND_LIBRARY_SUFFIXES ${CMAKE_SHARED_LIBRARY_SUFFIX})
    # MSYS has a special syntax for runtime libraries
    if (CMAKE_SYSTEM_NAME MATCHES "MSYS")
      list (APPEND CMAKE_FIND_LIBRARY_PREFIXES "msys-")
    endif()
    find_library (${ARGV})
  endif()
endfunction()


function (_PYTHON_SET_LIBRARY_DIRS _PYTHON_SLD_RESULT)
  unset (_PYTHON_DIRS)
  set (_PYTHON_LIBS ${ARGN})
  foreach (_PYTHON_LIB IN LISTS _PYTHON_LIBS)
    if (${_PYTHON_LIB})
      get_filename_component (_PYTHON_DIR "${${_PYTHON_LIB}}" DIRECTORY)
      list (APPEND _PYTHON_DIRS "${_PYTHON_DIR}")
    endif()
  endforeach()
  list (REMOVE_DUPLICATES _PYTHON_DIRS)
  set (${_PYTHON_SLD_RESULT} ${_PYTHON_DIRS} PARENT_SCOPE)
endfunction()


function (_PYTHON_SET_DEVELOPMENT_MODULE_FOUND module)
  if ("Development.${module}" IN_LIST ${_PYTHON_PREFIX}_FIND_COMPONENTS)
    string(TOUPPER "${module}" id)
    set (module_found TRUE)

    if ("LIBRARY" IN_LIST _${_PYTHON_PREFIX}_FIND_DEVELOPMENT_${id}_ARTIFACTS
        AND NOT _${_PYTHON_PREFIX}_LIBRARY_RELEASE)
      set (module_found FALSE)
    endif()
    if ("INCLUDE_DIR" IN_LIST _${_PYTHON_PREFIX}_FIND_DEVELOPMENT_${id}_ARTIFACTS
        AND NOT _${_PYTHON_PREFIX}_INCLUDE_DIR)
      set (module_found FALSE)
    endif()

    set (${_PYTHON_PREFIX}_Development.${module}_FOUND ${module_found} PARENT_SCOPE)
  endif()
endfunction()


if (${_PYTHON_PREFIX}_FIND_VERSION_RANGE)
  # range must include internal major version
  if (${_PYTHON_PREFIX}_FIND_VERSION_MIN_MAJOR VERSION_GREATER _${_PYTHON_PREFIX}_REQUIRED_VERSION_MAJOR
      OR ((${_PYTHON_PREFIX}_FIND_VERSION_RANGE_MAX STREQUAL "INCLUDE"
          AND ${_PYTHON_PREFIX}_FIND_VERSION_MAX VERSION_LESS _${_PYTHON_PREFIX}_REQUIRED_VERSION_MAJOR)
        OR (${_PYTHON_PREFIX}_FIND_VERSION_RANGE_MAX STREQUAL "EXCLUDE"
          AND ${_PYTHON_PREFIX}_FIND_VERSION_MAX VERSION_LESS_EQUAL _${_PYTHON_PREFIX}_REQUIRED_VERSION_MAJOR)))
    _python_display_failure ("Could NOT find ${_PYTHON_PREFIX}: Wrong version range specified is \"${${_PYTHON_PREFIX}_FIND_VERSION_RANGE}\", but expected version range must include major version \"${_${_PYTHON_PREFIX}_REQUIRED_VERSION_MAJOR}\"")

    cmake_policy(POP)
    return()
  endif()
else()
  if (DEFINED ${_PYTHON_PREFIX}_FIND_VERSION_MAJOR
      AND NOT ${_PYTHON_PREFIX}_FIND_VERSION_MAJOR VERSION_EQUAL _${_PYTHON_PREFIX}_REQUIRED_VERSION_MAJOR)
    # If major version is specified, it must be the same as internal major version
    _python_display_failure ("Could NOT find ${_PYTHON_PREFIX}: Wrong major version specified is \"${${_PYTHON_PREFIX}_FIND_VERSION_MAJOR}\", but expected major version is \"${_${_PYTHON_PREFIX}_REQUIRED_VERSION_MAJOR}\"")

    cmake_policy(POP)
    return()
  endif()
endif()


# handle components
if (NOT ${_PYTHON_PREFIX}_FIND_COMPONENTS)
  set (${_PYTHON_PREFIX}_FIND_COMPONENTS Interpreter)
  set (${_PYTHON_PREFIX}_FIND_REQUIRED_Interpreter TRUE)
endif()
if ("NumPy" IN_LIST ${_PYTHON_PREFIX}_FIND_COMPONENTS)
  list (APPEND ${_PYTHON_PREFIX}_FIND_COMPONENTS "Interpreter" "Development.Module")
endif()
if ("Development" IN_LIST ${_PYTHON_PREFIX}_FIND_COMPONENTS)
  list (APPEND ${_PYTHON_PREFIX}_FIND_COMPONENTS "Development.Module" "Development.Embed")
endif()
list (REMOVE_DUPLICATES ${_PYTHON_PREFIX}_FIND_COMPONENTS)
foreach (_${_PYTHON_PREFIX}_COMPONENT IN ITEMS Interpreter Compiler Development Development.Module Development.Embed NumPy)
  set (${_PYTHON_PREFIX}_${_${_PYTHON_PREFIX}_COMPONENT}_FOUND FALSE)
endforeach()
if (${_PYTHON_PREFIX}_FIND_REQUIRED_Development)
  set (${_PYTHON_PREFIX}_FIND_REQUIRED_Development.Module TRUE)
  set (${_PYTHON_PREFIX}_FIND_REQUIRED_Development.Embed TRUE)
endif()

unset (_${_PYTHON_PREFIX}_FIND_DEVELOPMENT_ARTIFACTS)
unset (_${_PYTHON_PREFIX}_FIND_DEVELOPMENT_MODULE_ARTIFACTS)
unset (_${_PYTHON_PREFIX}_FIND_DEVELOPMENT_EMBED_ARTIFACTS)
if ("Development.Module" IN_LIST ${_PYTHON_PREFIX}_FIND_COMPONENTS)
  if (CMAKE_SYSTEM_NAME MATCHES "^(Windows.*|CYGWIN|MSYS)$")
    list (APPEND _${_PYTHON_PREFIX}_FIND_DEVELOPMENT_MODULE_ARTIFACTS "LIBRARY")
  endif()
  list (APPEND _${_PYTHON_PREFIX}_FIND_DEVELOPMENT_MODULE_ARTIFACTS "INCLUDE_DIR")
endif()
if ("Development.Embed" IN_LIST ${_PYTHON_PREFIX}_FIND_COMPONENTS)
  list (APPEND _${_PYTHON_PREFIX}_FIND_DEVELOPMENT_EMBED_ARTIFACTS "LIBRARY" "INCLUDE_DIR")
endif()
set (_${_PYTHON_PREFIX}_FIND_DEVELOPMENT_ARTIFACTS ${_${_PYTHON_PREFIX}_FIND_DEVELOPMENT_MODULE_ARTIFACTS} ${_${_PYTHON_PREFIX}_FIND_DEVELOPMENT_EMBED_ARTIFACTS})
list (REMOVE_DUPLICATES _${_PYTHON_PREFIX}_FIND_DEVELOPMENT_ARTIFACTS)

# Set versions to search
## default: search any version
set (_${_PYTHON_PREFIX}_FIND_VERSIONS ${_${_PYTHON_PREFIX}_VERSIONS})
unset (_${_PYTHON_PREFIX}_FIND_VERSION_EXACT)

if (${_PYTHON_PREFIX}_FIND_VERSION_RANGE)
  unset (_${_PYTHON_PREFIX}_FIND_VERSIONS)
  foreach (_${_PYTHON_PREFIX}_VERSION IN LISTS _${_PYTHON_PREFIX}_VERSIONS)
    if ((${_PYTHON_PREFIX}_FIND_VERSION_RANGE_MIN STREQUAL "INCLUDE"
          AND _${_PYTHON_PREFIX}_VERSION VERSION_GREATER_EQUAL ${_PYTHON_PREFIX}_FIND_VERSION_MIN)
        AND ((${_PYTHON_PREFIX}_FIND_VERSION_RANGE_MAX STREQUAL "INCLUDE"
            AND _${_PYTHON_PREFIX}_VERSION VERSION_LESS_EQUAL ${_PYTHON_PREFIX}_FIND_VERSION_MAX)
          OR (${_PYTHON_PREFIX}_FIND_VERSION_RANGE_MAX STREQUAL "EXCLUDE"
            AND _${_PYTHON_PREFIX}_VERSION VERSION_LESS ${_PYTHON_PREFIX}_FIND_VERSION_MAX)))
      list (APPEND _${_PYTHON_PREFIX}_FIND_VERSIONS ${_${_PYTHON_PREFIX}_VERSION})
    endif()
  endforeach()
else()
  if (${_PYTHON_PREFIX}_FIND_VERSION_COUNT GREATER 1)
    if (${_PYTHON_PREFIX}_FIND_VERSION_EXACT)
      set (_${_PYTHON_PREFIX}_FIND_VERSION_EXACT "EXACT")
      set (_${_PYTHON_PREFIX}_FIND_VERSIONS ${${_PYTHON_PREFIX}_FIND_VERSION_MAJOR}.${${_PYTHON_PREFIX}_FIND_VERSION_MINOR})
    else()
      unset (_${_PYTHON_PREFIX}_FIND_VERSIONS)
      # add all compatible versions
      foreach (_${_PYTHON_PREFIX}_VERSION IN LISTS _${_PYTHON_PREFIX}_VERSIONS)
        if (_${_PYTHON_PREFIX}_VERSION VERSION_GREATER_EQUAL "${${_PYTHON_PREFIX}_FIND_VERSION_MAJOR}.${${_PYTHON_PREFIX}_FIND_VERSION_MINOR}")
          list (APPEND _${_PYTHON_PREFIX}_FIND_VERSIONS ${_${_PYTHON_PREFIX}_VERSION})
        endif()
      endforeach()
    endif()
  endif()
endif()

# Set ABIs to search
## default: search any ABI
if (_${_PYTHON_PREFIX}_REQUIRED_VERSION_MAJOR VERSION_LESS "3")
  # ABI not supported
  unset (_${_PYTHON_PREFIX}_FIND_ABI)
  set (_${_PYTHON_PREFIX}_ABIFLAGS "")
else()
  unset (_${_PYTHON_PREFIX}_FIND_ABI)
  unset (_${_PYTHON_PREFIX}_ABIFLAGS)
  if (DEFINED ${_PYTHON_PREFIX}_FIND_ABI)
    # normalization
    string (TOUPPER "${${_PYTHON_PREFIX}_FIND_ABI}" _${_PYTHON_PREFIX}_FIND_ABI)
    list (TRANSFORM _${_PYTHON_PREFIX}_FIND_ABI REPLACE "^(TRUE|Y(ES)?|1)$" "ON")
    list (TRANSFORM _${_PYTHON_PREFIX}_FIND_ABI REPLACE "^(FALSE|N(O)?|0)$" "OFF")
    if (NOT _${_PYTHON_PREFIX}_FIND_ABI MATCHES "^(ON|OFF|ANY);(ON|OFF|ANY);(ON|OFF|ANY)$")
      message (AUTHOR_WARNING "Find${_PYTHON_PREFIX}: ${${_PYTHON_PREFIX}_FIND_ABI}: invalid value for '${_PYTHON_PREFIX}_FIND_ABI'. Ignore it")
      unset (_${_PYTHON_PREFIX}_FIND_ABI)
    endif()
    _python_get_abiflags (_${_PYTHON_PREFIX}_ABIFLAGS)
  endif()
endif()
unset (${_PYTHON_PREFIX}_SOABI)

# Define lookup strategy
cmake_policy (GET CMP0094 _${_PYTHON_PREFIX}_LOOKUP_POLICY)
if (_${_PYTHON_PREFIX}_LOOKUP_POLICY STREQUAL "NEW")
  set (_${_PYTHON_PREFIX}_FIND_STRATEGY "LOCATION")
else()
  set (_${_PYTHON_PREFIX}_FIND_STRATEGY "VERSION")
endif()
if (DEFINED ${_PYTHON_PREFIX}_FIND_STRATEGY)
  if (NOT ${_PYTHON_PREFIX}_FIND_STRATEGY MATCHES "^(VERSION|LOCATION)$")
    message (AUTHOR_WARNING "Find${_PYTHON_PREFIX}: ${${_PYTHON_PREFIX}_FIND_STRATEGY}: invalid value for '${_PYTHON_PREFIX}_FIND_STRATEGY'. 'VERSION' or 'LOCATION' expected.")
    set (_${_PYTHON_PREFIX}_FIND_STRATEGY "VERSION")
  else()
    set (_${_PYTHON_PREFIX}_FIND_STRATEGY "${${_PYTHON_PREFIX}_FIND_STRATEGY}")
  endif()
endif()

# Python and Anaconda distributions: define which architectures can be used
if (CMAKE_SIZEOF_VOID_P)
  # In this case, search only for 64bit or 32bit
  math (EXPR _${_PYTHON_PREFIX}_ARCH "${CMAKE_SIZEOF_VOID_P} * 8")
  set (_${_PYTHON_PREFIX}_ARCH2 ${_${_PYTHON_PREFIX}_ARCH})
else()
  # architecture unknown, search for both 64bit and 32bit
  set (_${_PYTHON_PREFIX}_ARCH 64)
  set (_${_PYTHON_PREFIX}_ARCH2 32)
endif()

# IronPython support
unset (_${_PYTHON_PREFIX}_IRON_PYTHON_INTERPRETER_NAMES)
unset (_${_PYTHON_PREFIX}_IRON_PYTHON_COMPILER_NAMES)
unset (_${_PYTHON_PREFIX}_IRON_PYTHON_COMPILER_ARCH_FLAGS)
if (CMAKE_SIZEOF_VOID_P)
  if (_${_PYTHON_PREFIX}_ARCH EQUAL "32")
    set (_${_PYTHON_PREFIX}_IRON_PYTHON_COMPILER_ARCH_FLAGS "/platform:x86")
  else()
    set (_${_PYTHON_PREFIX}_IRON_PYTHON_COMPILER_ARCH_FLAGS "/platform:x64")
  endif()
endif()
if (NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
  # Do not use wrapper script on Linux because it is buggy: -c interpreter option cannot be used
  list (APPEND _${_PYTHON_PREFIX}_IRON_PYTHON_INTERPRETER_NAMES "ipy${_${_PYTHON_PREFIX}_REQUIRED_VERSION_MAJOR}" "ipy64" "ipy32" "ipy")
  list (APPEND _${_PYTHON_PREFIX}_IRON_PYTHON_COMPILER_NAMES "ipyc")
endif()
list (APPEND _${_PYTHON_PREFIX}_IRON_PYTHON_INTERPRETER_NAMES "ipy.exe")
list (APPEND _${_PYTHON_PREFIX}_IRON_PYTHON_COMPILER_NAMES "ipyc.exe")
set (_${_PYTHON_PREFIX}_IRON_PYTHON_PATH_SUFFIXES net45 net40 bin)

# PyPy support
if (_${_PYTHON_PREFIX}_REQUIRED_VERSION_MAJOR EQUAL "3")
  set (_${_PYTHON_PREFIX}_PYPY_NAMES pypy3)
  set (_${_PYTHON_PREFIX}_PYPY_LIB_NAMES pypy3-c)
  if (WIN32)
    # special name for runtime part
    list (APPEND _${_PYTHON_PREFIX}_PYPY_LIB_NAMES libpypy3-c)
  endif()
  set (_${_PYTHON_PREFIX}_PYPY_INCLUDE_PATH_SUFFIXES lib/pypy3)
else()
  set (_${_PYTHON_PREFIX}_PYPY_NAMES pypy)
  set (_${_PYTHON_PREFIX}_PYPY_LIB_NAMES pypy-c)
  if (WIN32)
    # special name for runtime part
    list (APPEND _${_PYTHON_PREFIX}_PYPY_LIB_NAMES libpypy-c)
  endif()
  set (_${_PYTHON_PREFIX}_PYPY_INCLUDE_PATH_SUFFIXES lib/pypy)
endif()
set (_${_PYTHON_PREFIX}_PYPY_EXECUTABLE_PATH_SUFFIXES bin)
set (_${_PYTHON_PREFIX}_PYPY_LIBRARY_PATH_SUFFIXES lib libs bin)
list (APPEND _${_PYTHON_PREFIX}_PYPY_INCLUDE_PATH_SUFFIXES include)

# Python Implementations handling
unset (_${_PYTHON_PREFIX}_FIND_IMPLEMENTATIONS)
if (DEFINED ${_PYTHON_PREFIX}_FIND_IMPLEMENTATIONS)
  foreach (_${_PYTHON_PREFIX}_IMPLEMENTATION IN LISTS ${_PYTHON_PREFIX}_FIND_IMPLEMENTATIONS)
    if (NOT _${_PYTHON_PREFIX}_IMPLEMENTATION MATCHES "^(CPython|IronPython|PyPy)$")
      message (AUTHOR_WARNING "Find${_PYTHON_PREFIX}: ${_${_PYTHON_PREFIX}_IMPLEMENTATION}: invalid value for '${_PYTHON_PREFIX}_FIND_IMPLEMENTATIONS'. 'CPython', 'IronPython' or 'PyPy' expected. Value will be ignored.")
    else()
      list (APPEND _${_PYTHON_PREFIX}_FIND_IMPLEMENTATIONS ${_${_PYTHON_PREFIX}_IMPLEMENTATION})
    endif()
  endforeach()
else()
  if (WIN32)
    set (_${_PYTHON_PREFIX}_FIND_IMPLEMENTATIONS CPython IronPython)
  else()
    set (_${_PYTHON_PREFIX}_FIND_IMPLEMENTATIONS CPython)
  endif()
endif()

# compute list of names for header file
unset (_${_PYTHON_PREFIX}_INCLUDE_NAMES)
foreach (_${_PYTHON_PREFIX}_IMPLEMENTATION IN LISTS _${_PYTHON_PREFIX}_FIND_IMPLEMENTATIONS)
  if (_${_PYTHON_PREFIX}_IMPLEMENTATION STREQUAL "CPython")
    list (APPEND _${_PYTHON_PREFIX}_INCLUDE_NAMES "Python.h")
  elseif (_${_PYTHON_PREFIX}_IMPLEMENTATION STREQUAL "PyPy")
    list (APPEND _${_PYTHON_PREFIX}_INCLUDE_NAMES "PyPy.h")
  endif()
endforeach()


# Apple frameworks handling
_python_find_frameworks ()

set (_${_PYTHON_PREFIX}_FIND_FRAMEWORK "FIRST")

if (DEFINED ${_PYTHON_PREFIX}_FIND_FRAMEWORK)
  if (NOT ${_PYTHON_PREFIX}_FIND_FRAMEWORK MATCHES "^(FIRST|LAST|NEVER)$")
    message (AUTHOR_WARNING "Find${_PYTHON_PREFIX}: ${${_PYTHON_PREFIX}_FIND_FRAMEWORK}: invalid value for '${_PYTHON_PREFIX}_FIND_FRAMEWORK'. 'FIRST', 'LAST' or 'NEVER' expected. 'FIRST' will be used instead.")
  else()
    set (_${_PYTHON_PREFIX}_FIND_FRAMEWORK ${${_PYTHON_PREFIX}_FIND_FRAMEWORK})
  endif()
elseif (DEFINED CMAKE_FIND_FRAMEWORK)
  if (CMAKE_FIND_FRAMEWORK STREQUAL "ONLY")
    message (AUTHOR_WARNING "Find${_PYTHON_PREFIX}: CMAKE_FIND_FRAMEWORK: 'ONLY' value is not supported. 'FIRST' will be used instead.")
  elseif (NOT CMAKE_FIND_FRAMEWORK MATCHES "^(FIRST|LAST|NEVER)$")
    message (AUTHOR_WARNING "Find${_PYTHON_PREFIX}: ${CMAKE_FIND_FRAMEWORK}: invalid value for 'CMAKE_FIND_FRAMEWORK'. 'FIRST', 'LAST' or 'NEVER' expected. 'FIRST' will be used instead.")
  else()
    set (_${_PYTHON_PREFIX}_FIND_FRAMEWORK ${CMAKE_FIND_FRAMEWORK})
  endif()
endif()

# Save CMAKE_FIND_APPBUNDLE
if (DEFINED CMAKE_FIND_APPBUNDLE)
  set (_${_PYTHON_PREFIX}_CMAKE_FIND_APPBUNDLE ${CMAKE_FIND_APPBUNDLE})
else()
  unset (_${_PYTHON_PREFIX}_CMAKE_FIND_APPBUNDLE)
endif()
# To avoid app bundle lookup
set (CMAKE_FIND_APPBUNDLE "NEVER")

# Save CMAKE_FIND_FRAMEWORK
if (DEFINED CMAKE_FIND_FRAMEWORK)
  set (_${_PYTHON_PREFIX}_CMAKE_FIND_FRAMEWORK ${CMAKE_FIND_FRAMEWORK})
else()
  unset (_${_PYTHON_PREFIX}_CMAKE_FIND_FRAMEWORK)
endif()
# To avoid framework lookup
set (CMAKE_FIND_FRAMEWORK "NEVER")

# Windows Registry handling
if (DEFINED ${_PYTHON_PREFIX}_FIND_REGISTRY)
  if (NOT ${_PYTHON_PREFIX}_FIND_REGISTRY MATCHES "^(FIRST|LAST|NEVER)$")
    message (AUTHOR_WARNING "Find${_PYTHON_PREFIX}: ${${_PYTHON_PREFIX}_FIND_REGISTRY}: invalid value for '${_PYTHON_PREFIX}_FIND_REGISTRY'. 'FIRST', 'LAST' or 'NEVER' expected. 'FIRST' will be used instead.")
    set (_${_PYTHON_PREFIX}_FIND_REGISTRY "FIRST")
  else()
    set (_${_PYTHON_PREFIX}_FIND_REGISTRY ${${_PYTHON_PREFIX}_FIND_REGISTRY})
  endif()
else()
  set (_${_PYTHON_PREFIX}_FIND_REGISTRY "FIRST")
endif()

# virtual environments recognition
if (DEFINED ENV{VIRTUAL_ENV} OR DEFINED ENV{CONDA_PREFIX})
  if (DEFINED ${_PYTHON_PREFIX}_FIND_VIRTUALENV)
    if (NOT ${_PYTHON_PREFIX}_FIND_VIRTUALENV MATCHES "^(FIRST|ONLY|STANDARD)$")
      message (AUTHOR_WARNING "Find${_PYTHON_PREFIX}: ${${_PYTHON_PREFIX}_FIND_VIRTUALENV}: invalid value for '${_PYTHON_PREFIX}_FIND_VIRTUALENV'. 'FIRST', 'ONLY' or 'STANDARD' expected. 'FIRST' will be used instead.")
      set (_${_PYTHON_PREFIX}_FIND_VIRTUALENV "FIRST")
    else()
      set (_${_PYTHON_PREFIX}_FIND_VIRTUALENV ${${_PYTHON_PREFIX}_FIND_VIRTUALENV})
    endif()
  else()
    set (_${_PYTHON_PREFIX}_FIND_VIRTUALENV FIRST)
  endif()
else()
  set (_${_PYTHON_PREFIX}_FIND_VIRTUALENV STANDARD)
endif()


# Compute search signature
# This signature will be used to check validity of cached variables on new search
set (_${_PYTHON_PREFIX}_SIGNATURE "${${_PYTHON_PREFIX}_ROOT_DIR}:${_${_PYTHON_PREFIX}_FIND_IMPLEMENTATIONS}:${_${_PYTHON_PREFIX}_FIND_STRATEGY}:${${_PYTHON_PREFIX}_FIND_VIRTUALENV}")
if (NOT WIN32)
  string (APPEND _${_PYTHON_PREFIX}_SIGNATURE ":${${_PYTHON_PREFIX}_USE_STATIC_LIBS}:")
endif()
if (CMAKE_HOST_APPLE)
  string (APPEND _${_PYTHON_PREFIX}_SIGNATURE ":${_${_PYTHON_PREFIX}_FIND_FRAMEWORK}")
endif()
if (CMAKE_HOST_WIN32)
  string (APPEND _${_PYTHON_PREFIX}_SIGNATURE ":${_${_PYTHON_PREFIX}_FIND_REGISTRY}")
endif()

function (_PYTHON_CHECK_DEVELOPMENT_SIGNATURE module)
  if ("Development.${module}" IN_LIST ${_PYTHON_PREFIX}_FIND_COMPONENTS)
    string (TOUPPER "${module}" id)
    set (signature "${_${_PYTHON_PREFIX}_SIGNATURE}:")
    if ("LIBRARY" IN_LIST _${_PYTHON_PREFIX}_FIND_DEVELOPMENT_${id}_ARTIFACTS)
      list (APPEND signature "${_${_PYTHON_PREFIX}_LIBRARY_RELEASE}:")
    endif()
    if ("INCLUDE_DIR" IN_LIST _${_PYTHON_PREFIX}_FIND_DEVELOPMENT_${id}_ARTIFACTS)
      list (APPEND signature "${_${_PYTHON_PREFIX}_INCLUDE_DIR}:")
    endif()
    string (MD5 signature "${signature}")
    if (signature STREQUAL _${_PYTHON_PREFIX}_DEVELOPMENT_${id}_SIGNATURE)
      if ("LIBRARY" IN_LIST _${_PYTHON_PREFIX}_FIND_DEVELOPMENT_${id}_ARTIFACTS)
        if (${_PYTHON_PREFIX}_FIND_VERSION_EXACT)
          _python_validate_library (VERSION ${${_PYTHON_PREFIX}_FIND_VERSION} EXACT CHECK_EXISTS)
        elseif (${_PYTHON_PREFIX}_FIND_VERSION_RANGE)
          _python_validate_library (IN_RANGE CHECK_EXISTS)
        elseif (DEFINED ${_PYTHON_PREFIX}_FIND_VERSION)
          _python_validate_library (VERSION ${${_PYTHON_PREFIX}_FIND_VERSION} CHECK_EXISTS)
        else()
          _python_validate_library (CHECK_EXISTS)
        endif()
      endif()
      if ("INCLUDE_DIR" IN_LIST _${_PYTHON_PREFIX}_FIND_DEVELOPMENT_${id}_ARTIFACTS)
        if (${_PYTHON_PREFIX}_FIND_VERSION_EXACT)
          _python_validate_include_dir (VERSION ${${_PYTHON_PREFIX}_FIND_VERSION} EXACT CHECK_EXISTS)
        elseif (${_PYTHON_PREFIX}_FIND_VERSION_RANGE)
          _python_validate_include_dir (IN_RANGE CHECK_EXISTS)
        elseif (${_PYTHON_PREFIX}_FIND_VERSION)
          _python_validate_include_dir (VERSION ${${_PYTHON_PREFIX}_FIND_VERSION} CHECK_EXISTS)
        else()
          _python_validate_include_dir (CHECK_EXISTS)
        endif()
      endif()
    else()
      if ("LIBRARY" IN_LIST _${_PYTHON_PREFIX}_FIND_DEVELOPMENT_${id}_ARTIFACTS)
        unset (_${_PYTHON_PREFIX}_LIBRARY_RELEASE CACHE)
        unset (_${_PYTHON_PREFIX}_LIBRARY_DEBUG CACHE)
      endif()
      if ("INCLUDE_DIR" IN_LIST _${_PYTHON_PREFIX}_FIND_DEVELOPMENT_${id}_ARTIFACTS)
        unset (_${_PYTHON_PREFIX}_INCLUDE_DIR CACHE)
      endif()
    endif()
    if (("LIBRARY" IN_LIST _${_PYTHON_PREFIX}_FIND_DEVELOPMENT_${id}_ARTIFACTS
          AND NOT _${_PYTHON_PREFIX}_LIBRARY_RELEASE)
        OR ("INCLUDE_DIR" IN_LIST _${_PYTHON_PREFIX}_FIND_DEVELOPMENT_${id}_ARTIFACTS
          AND NOT _${_PYTHON_PREFIX}_INCLUDE_DIR))
      unset (_${_PYTHON_PREFIX}_CONFIG CACHE)
      unset (_${_PYTHON_PREFIX}_DEVELOPMENT_${id}_SIGNATURE CACHE)
    endif()
  endif()
endfunction()

function (_PYTHON_COMPUTE_DEVELOPMENT_SIGNATURE module)
  string (TOUPPER "${module}" id)
  if (${_PYTHON_PREFIX}_Development.${module}_FOUND)
    set (signature "${_${_PYTHON_PREFIX}_SIGNATURE}:")
    if ("LIBRARY" IN_LIST _${_PYTHON_PREFIX}_FIND_DEVELOPMENT_${id}_ARTIFACTS)
      list (APPEND signature "${_${_PYTHON_PREFIX}_LIBRARY_RELEASE}:")
    endif()
    if ("INCLUDE_DIR" IN_LIST _${_PYTHON_PREFIX}_FIND_DEVELOPMENT_${id}_ARTIFACTS)
      list (APPEND signature "${_${_PYTHON_PREFIX}_INCLUDE_DIR}:")
    endif()
    string (MD5 signature "${signature}")
    set (_${_PYTHON_PREFIX}_DEVELOPMENT_${id}_SIGNATURE "${signature}" CACHE INTERNAL "")
  else()
    unset (_${_PYTHON_PREFIX}_DEVELOPMENT_${id}_SIGNATURE CACHE)
  endif()
endfunction()


unset (_${_PYTHON_PREFIX}_REQUIRED_VARS)
unset (_${_PYTHON_PREFIX}_CACHED_VARS)
unset (_${_PYTHON_PREFIX}_Interpreter_REASON_FAILURE)
unset (_${_PYTHON_PREFIX}_Compiler_REASON_FAILURE)
unset (_${_PYTHON_PREFIX}_Development_REASON_FAILURE)
unset (_${_PYTHON_PREFIX}_NumPy_REASON_FAILURE)


# preamble
## For IronPython on platforms other than Windows, search for the .Net interpreter
if ("IronPython" IN_LIST _${_PYTHON_PREFIX}_FIND_IMPLEMENTATIONS
    AND NOT WIN32)
  find_program (${_PYTHON_PREFIX}_DOTNET_LAUNCHER
                NAMES "mono")
endif()


# first step, search for the interpreter
if ("Interpreter" IN_LIST ${_PYTHON_PREFIX}_FIND_COMPONENTS)
  list (APPEND _${_PYTHON_PREFIX}_CACHED_VARS _${_PYTHON_PREFIX}_EXECUTABLE
                                              _${_PYTHON_PREFIX}_INTERPRETER_PROPERTIES)
  if (${_PYTHON_PREFIX}_FIND_REQUIRED_Interpreter)
    list (APPEND _${_PYTHON_PREFIX}_REQUIRED_VARS ${_PYTHON_PREFIX}_EXECUTABLE)
  endif()

  if (DEFINED ${_PYTHON_PREFIX}_EXECUTABLE
      AND IS_ABSOLUTE "${${_PYTHON_PREFIX}_EXECUTABLE}")
    if (NOT ${_PYTHON_PREFIX}_EXECUTABLE STREQUAL _${_PYTHON_PREFIX}_EXECUTABLE)
      # invalidate cache properties
      unset (_${_PYTHON_PREFIX}_INTERPRETER_PROPERTIES CACHE)
    endif()
    set (_${_PYTHON_PREFIX}_EXECUTABLE "${${_PYTHON_PREFIX}_EXECUTABLE}" CACHE INTERNAL "")
  elseif (DEFINED _${_PYTHON_PREFIX}_EXECUTABLE)
    # compute interpreter signature and check validity of definition
    string (MD5 __${_PYTHON_PREFIX}_INTERPRETER_SIGNATURE "${_${_PYTHON_PREFIX}_SIGNATURE}:${_${_PYTHON_PREFIX}_EXECUTABLE}")
    if (__${_PYTHON_PREFIX}_INTERPRETER_SIGNATURE STREQUAL _${_PYTHON_PREFIX}_INTERPRETER_SIGNATURE)
      # check version validity
      if (${_PYTHON_PREFIX}_FIND_VERSION_EXACT)
        _python_validate_interpreter (VERSION ${${_PYTHON_PREFIX}_FIND_VERSION} EXACT CHECK_EXISTS)
      elseif (${_PYTHON_PREFIX}_FIND_VERSION_RANGE)
        _python_validate_interpreter (IN_RANGE CHECK_EXISTS)
      elseif (DEFINED ${_PYTHON_PREFIX}_FIND_VERSION)
        _python_validate_interpreter (VERSION ${${_PYTHON_PREFIX}_FIND_VERSION} CHECK_EXISTS)
      else()
        _python_validate_interpreter (CHECK_EXISTS)
      endif()
    else()
      unset (_${_PYTHON_PREFIX}_EXECUTABLE CACHE)
    endif()
    if (NOT _${_PYTHON_PREFIX}_EXECUTABLE)
      unset (_${_PYTHON_PREFIX}_INTERPRETER_SIGNATURE CACHE)
      unset (_${_PYTHON_PREFIX}_INTERPRETER_PROPERTIES CACHE)
    endif()
  endif()

  if (NOT _${_PYTHON_PREFIX}_EXECUTABLE)
    set (_${_PYTHON_PREFIX}_HINTS "${${_PYTHON_PREFIX}_ROOT_DIR}" ENV ${_PYTHON_PREFIX}_ROOT_DIR)

    if (_${_PYTHON_PREFIX}_FIND_STRATEGY STREQUAL "LOCATION")
      # build all executable names
      _python_get_names (_${_PYTHON_PREFIX}_NAMES VERSION ${_${_PYTHON_PREFIX}_FIND_VERSIONS} POSIX INTERPRETER)
      _python_get_path_suffixes (_${_PYTHON_PREFIX}_PATH_SUFFIXES VERSION ${_${_PYTHON_PREFIX}_FIND_VERSIONS} INTERPRETER)

      # Framework Paths
      _python_get_frameworks (_${_PYTHON_PREFIX}_FRAMEWORK_PATHS VERSION ${_${_PYTHON_PREFIX}_FIND_VERSIONS})
      # Registry Paths
      _python_get_registries (_${_PYTHON_PREFIX}_REGISTRY_PATHS VERSION ${_${_PYTHON_PREFIX}_FIND_VERSIONS})

      set (_${_PYTHON_PREFIX}_VALIDATE_OPTIONS ${_${_PYTHON_PREFIX}_FIND_VERSION_EXACT})
      if (${_PYTHON_PREFIX}_FIND_VERSION_RANGE)
        list (APPEND _${_PYTHON_PREFIX}_VALIDATE_OPTIONS IN_RANGE)
      elseif (DEFINED ${_PYTHON_PREFIX}_FIND_VERSION)
        list (APPEND _${_PYTHON_PREFIX}_VALIDATE_OPTIONS VERSION ${${_PYTHON_PREFIX}_FIND_VERSION})
      endif()

      while (TRUE)
        # Virtual environments handling
        if (_${_PYTHON_PREFIX}_FIND_VIRTUALENV MATCHES "^(FIRST|ONLY)$")
          find_program (_${_PYTHON_PREFIX}_EXECUTABLE
                        NAMES ${_${_PYTHON_PREFIX}_NAMES}
                        NAMES_PER_DIR
                        HINTS ${_${_PYTHON_PREFIX}_HINTS}
                        PATHS ENV VIRTUAL_ENV ENV CONDA_PREFIX
                        PATH_SUFFIXES ${_${_PYTHON_PREFIX}_PATH_SUFFIXES}
                        NO_CMAKE_PATH
                        NO_CMAKE_ENVIRONMENT_PATH
                        NO_SYSTEM_ENVIRONMENT_PATH
                        NO_CMAKE_SYSTEM_PATH)

          _python_validate_interpreter (${_${_PYTHON_PREFIX}_VALIDATE_OPTIONS})
          if (_${_PYTHON_PREFIX}_EXECUTABLE)
            break()
          endif()
          if (_${_PYTHON_PREFIX}_FIND_VIRTUALENV STREQUAL "ONLY")
            break()
          endif()
        endif()

        # Apple frameworks handling
        if (CMAKE_HOST_APPLE AND _${_PYTHON_PREFIX}_FIND_FRAMEWORK STREQUAL "FIRST")
          find_program (_${_PYTHON_PREFIX}_EXECUTABLE
                        NAMES ${_${_PYTHON_PREFIX}_NAMES}
                        NAMES_PER_DIR
                        HINTS ${_${_PYTHON_PREFIX}_HINTS}
                        PATHS ${_${_PYTHON_PREFIX}_FRAMEWORK_PATHS}
                        PATH_SUFFIXES ${_${_PYTHON_PREFIX}_PATH_SUFFIXES}
                        NO_CMAKE_PATH
                        NO_CMAKE_ENVIRONMENT_PATH
                        NO_SYSTEM_ENVIRONMENT_PATH
                        NO_CMAKE_SYSTEM_PATH)
          _python_validate_interpreter (${${_PYTHON_PREFIX}_VALIDATE_OPTIONS})
          if (_${_PYTHON_PREFIX}_EXECUTABLE)
            break()
          endif()
        endif()
        # Windows registry
        if (CMAKE_HOST_WIN32 AND _${_PYTHON_PREFIX}_FIND_REGISTRY STREQUAL "FIRST")
          find_program (_${_PYTHON_PREFIX}_EXECUTABLE
                        NAMES ${_${_PYTHON_PREFIX}_NAMES}
                        NAMES_PER_DIR
                        HINTS ${_${_PYTHON_PREFIX}_HINTS}
                        PATHS ${_${_PYTHON_PREFIX}_REGISTRY_PATHS}
                        PATH_SUFFIXES ${_${_PYTHON_PREFIX}_PATH_SUFFIXES}
                        NO_SYSTEM_ENVIRONMENT_PATH
                        NO_CMAKE_SYSTEM_PATH)
          _python_validate_interpreter (${${_PYTHON_PREFIX}_VALIDATE_OPTIONS})
          if (_${_PYTHON_PREFIX}_EXECUTABLE)
            break()
          endif()
        endif()

        # try using HINTS
        find_program (_${_PYTHON_PREFIX}_EXECUTABLE
                      NAMES ${_${_PYTHON_PREFIX}_NAMES}
                      NAMES_PER_DIR
                      HINTS ${_${_PYTHON_PREFIX}_HINTS}
                      PATH_SUFFIXES ${_${_PYTHON_PREFIX}_PATH_SUFFIXES}
                      NO_SYSTEM_ENVIRONMENT_PATH
                      NO_CMAKE_SYSTEM_PATH)
        _python_validate_interpreter (${${_PYTHON_PREFIX}_VALIDATE_OPTIONS})
        if (_${_PYTHON_PREFIX}_EXECUTABLE)
          break()
        endif()
        # try using standard paths
        find_program (_${_PYTHON_PREFIX}_EXECUTABLE
                      NAMES ${_${_PYTHON_PREFIX}_NAMES}
                      NAMES_PER_DIR
                      PATH_SUFFIXES ${_${_PYTHON_PREFIX}_PATH_SUFFIXES})
        _python_validate_interpreter (${${_PYTHON_PREFIX}_VALIDATE_OPTIONS})
        if (_${_PYTHON_PREFIX}_EXECUTABLE)
          break()
        endif()

        # Apple frameworks handling
        if (CMAKE_HOST_APPLE AND _${_PYTHON_PREFIX}_FIND_FRAMEWORK STREQUAL "LAST")
          find_program (_${_PYTHON_PREFIX}_EXECUTABLE
                        NAMES ${_${_PYTHON_PREFIX}_NAMES}
                        NAMES_PER_DIR
                        PATHS ${_${_PYTHON_PREFIX}_FRAMEWORK_PATHS}
                        PATH_SUFFIXES ${_${_PYTHON_PREFIX}_PATH_SUFFIXES}
                        NO_DEFAULT_PATH)
          _python_validate_interpreter (${${_PYTHON_PREFIX}_VALIDATE_OPTIONS})
          if (_${_PYTHON_PREFIX}_EXECUTABLE)
            break()
          endif()
        endif()
        # Windows registry
        if (CMAKE_HOST_WIN32 AND _${_PYTHON_PREFIX}_FIND_REGISTRY STREQUAL "LAST")
          find_program (_${_PYTHON_PREFIX}_EXECUTABLE
                        NAMES ${_${_PYTHON_PREFIX}_NAMES}
                        NAMES_PER_DIR
                        PATHS ${_${_PYTHON_PREFIX}_REGISTRY_PATHS}
                        PATH_SUFFIXES ${_${_PYTHON_PREFIX}_PATH_SUFFIXES}
                        NO_DEFAULT_PATH)
          _python_validate_interpreter (${${_PYTHON_PREFIX}_VALIDATE_OPTIONS})
          if (_${_PYTHON_PREFIX}_EXECUTABLE)
            break()
          endif()
        endif()

        break()
      endwhile()
    else()
      # look-up for various versions and locations
      set (_${_PYTHON_PREFIX}_VALIDATE_OPTIONS EXACT)
      if (${_PYTHON_PREFIX}_FIND_VERSION_RANGE)
        list (APPEND _${_PYTHON_PREFIX}_VALIDATE_OPTIONS IN_RANGE)
      endif()

      foreach (_${_PYTHON_PREFIX}_VERSION IN LISTS _${_PYTHON_PREFIX}_FIND_VERSIONS)
        _python_get_names (_${_PYTHON_PREFIX}_NAMES VERSION ${_${_PYTHON_PREFIX}_VERSION} POSIX INTERPRETER)
        _python_get_path_suffixes (_${_PYTHON_PREFIX}_PATH_SUFFIXES VERSION ${_${_PYTHON_PREFIX}_VERSION} INTERPRETER)

        _python_get_frameworks (_${_PYTHON_PREFIX}_FRAMEWORK_PATHS VERSION ${_${_PYTHON_PREFIX}_VERSION})
        _python_get_registries (_${_PYTHON_PREFIX}_REGISTRY_PATHS VERSION ${_${_PYTHON_PREFIX}_VERSION})

        # Virtual environments handling
        if (_${_PYTHON_PREFIX}_FIND_VIRTUALENV MATCHES "^(FIRST|ONLY)$")
          find_program (_${_PYTHON_PREFIX}_EXECUTABLE
                        NAMES ${_${_PYTHON_PREFIX}_NAMES}
                        NAMES_PER_DIR
                        HINTS ${_${_PYTHON_PREFIX}_HINTS}
                        PATHS ENV VIRTUAL_ENV ENV CONDA_PREFIX
                        PATH_SUFFIXES ${_${_PYTHON_PREFIX}_PATH_SUFFIXES}
                        NO_CMAKE_PATH
                        NO_CMAKE_ENVIRONMENT_PATH
                        NO_SYSTEM_ENVIRONMENT_PATH
                        NO_CMAKE_SYSTEM_PATH)
          _python_validate_interpreter (VERSION ${_${_PYTHON_PREFIX}_VERSION} ${_${_PYTHON_PREFIX}_VALIDATE_OPTIONS})
          if (_${_PYTHON_PREFIX}_EXECUTABLE)
            break()
          endif()
          if (_${_PYTHON_PREFIX}_FIND_VIRTUALENV STREQUAL "ONLY")
            continue()
          endif()
        endif()

        # Apple frameworks handling
        if (CMAKE_HOST_APPLE AND _${_PYTHON_PREFIX}_FIND_FRAMEWORK STREQUAL "FIRST")
          find_program (_${_PYTHON_PREFIX}_EXECUTABLE
                        NAMES ${_${_PYTHON_PREFIX}_NAMES}
                        NAMES_PER_DIR
                        HINTS ${_${_PYTHON_PREFIX}_HINTS}
                        PATHS ${_${_PYTHON_PREFIX}_FRAMEWORK_PATHS}
                        PATH_SUFFIXES ${_${_PYTHON_PREFIX}_PATH_SUFFIXES}
                        NO_CMAKE_PATH
                        NO_CMAKE_ENVIRONMENT_PATH
                        NO_SYSTEM_ENVIRONMENT_PATH
                        NO_CMAKE_SYSTEM_PATH)
        endif()

        # Windows registry
        if (CMAKE_HOST_WIN32 AND _${_PYTHON_PREFIX}_FIND_REGISTRY STREQUAL "FIRST")
          find_program (_${_PYTHON_PREFIX}_EXECUTABLE
                        NAMES ${_${_PYTHON_PREFIX}_NAMES}
                        NAMES_PER_DIR
                        HINTS ${_${_PYTHON_PREFIX}_HINTS}
                        PATHS ${_${_PYTHON_PREFIX}_REGISTRY_PATHS}
                        PATH_SUFFIXES ${_${_PYTHON_PREFIX}_PATH_SUFFIXES}
                        NO_SYSTEM_ENVIRONMENT_PATH
                        NO_CMAKE_SYSTEM_PATH)
        endif()

        _python_validate_interpreter (VERSION ${_${_PYTHON_PREFIX}_VERSION} ${_${_PYTHON_PREFIX}_VALIDATE_OPTIONS})
        if (_${_PYTHON_PREFIX}_EXECUTABLE)
          break()
        endif()

        # try using HINTS
        find_program (_${_PYTHON_PREFIX}_EXECUTABLE
                      NAMES ${_${_PYTHON_PREFIX}_NAMES}
                      NAMES_PER_DIR
                      HINTS ${_${_PYTHON_PREFIX}_HINTS}
                      PATH_SUFFIXES ${_${_PYTHON_PREFIX}_PATH_SUFFIXES}
                      NO_SYSTEM_ENVIRONMENT_PATH
                      NO_CMAKE_SYSTEM_PATH)
        _python_validate_interpreter (VERSION ${_${_PYTHON_PREFIX}_VERSION} ${_${_PYTHON_PREFIX}_VALIDATE_OPTIONS})
        if (_${_PYTHON_PREFIX}_EXECUTABLE)
          break()
        endif()
        # try using standard paths.
        # NAMES_PER_DIR is not defined on purpose to have a chance to find
        # expected version.
        # For example, typical systems have 'python' for version 2.* and 'python3'
        # for version 3.*. So looking for names per dir will find, potentially,
        # systematically 'python' (i.e. version 2) even if version 3 is searched.
        find_program (_${_PYTHON_PREFIX}_EXECUTABLE
                      NAMES ${_${_PYTHON_PREFIX}_NAMES}
                      PATH_SUFFIXES ${_${_PYTHON_PREFIX}_PATH_SUFFIXES})
        _python_validate_interpreter (VERSION ${_${_PYTHON_PREFIX}_VERSION} ${_${_PYTHON_PREFIX}_VALIDATE_OPTIONS})
        if (_${_PYTHON_PREFIX}_EXECUTABLE)
          break()
        endif()

        # Apple frameworks handling
        if (CMAKE_HOST_APPLE AND _${_PYTHON_PREFIX}_FIND_FRAMEWORK STREQUAL "LAST")
          find_program (_${_PYTHON_PREFIX}_EXECUTABLE
                        NAMES ${_${_PYTHON_PREFIX}_NAMES}
                        NAMES_PER_DIR
                        PATHS ${_${_PYTHON_PREFIX}_FRAMEWORK_PATHS}
                        PATH_SUFFIXES ${_${_PYTHON_PREFIX}_PATH_SUFFIXES}
                        NO_DEFAULT_PATH)
        endif()

        # Windows registry
        if (CMAKE_HOST_WIN32 AND _${_PYTHON_PREFIX}_FIND_REGISTRY STREQUAL "LAST")
          find_program (_${_PYTHON_PREFIX}_EXECUTABLE
                        NAMES ${_${_PYTHON_PREFIX}_NAMES}
                        NAMES_PER_DIR
                        PATHS ${_${_PYTHON_PREFIX}_REGISTRY_PATHS}
                        PATH_SUFFIXES ${_${_PYTHON_PREFIX}_PATH_SUFFIXES}
                        NO_DEFAULT_PATH)
        endif()

        _python_validate_interpreter (VERSION ${_${_PYTHON_PREFIX}_VERSION} ${_${_PYTHON_PREFIX}_VALIDATE_OPTIONS})
        if (_${_PYTHON_PREFIX}_EXECUTABLE)
          break()
        endif()
      endforeach()

      if (NOT _${_PYTHON_PREFIX}_EXECUTABLE AND
          NOT _${_PYTHON_PREFIX}_FIND_VIRTUALENV STREQUAL "ONLY")
        # No specific version found. Retry with generic names and standard paths.
        # NAMES_PER_DIR is not defined on purpose to have a chance to find
        # expected version.
        # For example, typical systems have 'python' for version 2.* and 'python3'
        # for version 3.*. So looking for names per dir will find, potentially,
        # systematically 'python' (i.e. version 2) even if version 3 is searched.
        _python_get_names (_${_PYTHON_PREFIX}_NAMES POSIX INTERPRETER)
        find_program (_${_PYTHON_PREFIX}_EXECUTABLE
                      NAMES ${_${_PYTHON_PREFIX}_NAMES})
        _python_validate_interpreter ()
      endif()
    endif()
  endif()

  set (${_PYTHON_PREFIX}_EXECUTABLE "${_${_PYTHON_PREFIX}_EXECUTABLE}")
  _python_get_launcher (_${_PYTHON_PREFIX}_INTERPRETER_LAUNCHER INTERPRETER)

  # retrieve exact version of executable found
  if (_${_PYTHON_PREFIX}_EXECUTABLE)
    execute_process (COMMAND ${_${_PYTHON_PREFIX}_INTERPRETER_LAUNCHER} "${_${_PYTHON_PREFIX}_EXECUTABLE}" -c
                             "import sys; sys.stdout.write('.'.join([str(x) for x in sys.version_info[:3]]))"
                     RESULT_VARIABLE _${_PYTHON_PREFIX}_RESULT
                     OUTPUT_VARIABLE ${_PYTHON_PREFIX}_VERSION
                     ERROR_QUIET
                     OUTPUT_STRIP_TRAILING_WHITESPACE)
    if (NOT _${_PYTHON_PREFIX}_RESULT)
      set (_${_PYTHON_PREFIX}_EXECUTABLE_USABLE TRUE)
    else()
      # Interpreter is not usable
      set (_${_PYTHON_PREFIX}_EXECUTABLE_USABLE FALSE)
      unset (${_PYTHON_PREFIX}_VERSION)
      set (_${_PYTHON_PREFIX}_Interpreter_REASON_FAILURE "Cannot run the interpreter \"${_${_PYTHON_PREFIX}_EXECUTABLE}\"")
    endif()
  endif()

  if (_${_PYTHON_PREFIX}_EXECUTABLE AND _${_PYTHON_PREFIX}_EXECUTABLE_USABLE)
    if (_${_PYTHON_PREFIX}_INTERPRETER_PROPERTIES)
      set (${_PYTHON_PREFIX}_Interpreter_FOUND TRUE)

      list (GET _${_PYTHON_PREFIX}_INTERPRETER_PROPERTIES 0 ${_PYTHON_PREFIX}_INTERPRETER_ID)

      list (GET _${_PYTHON_PREFIX}_INTERPRETER_PROPERTIES 1 ${_PYTHON_PREFIX}_VERSION_MAJOR)
      list (GET _${_PYTHON_PREFIX}_INTERPRETER_PROPERTIES 2 ${_PYTHON_PREFIX}_VERSION_MINOR)
      list (GET _${_PYTHON_PREFIX}_INTERPRETER_PROPERTIES 3 ${_PYTHON_PREFIX}_VERSION_PATCH)

      list (GET _${_PYTHON_PREFIX}_INTERPRETER_PROPERTIES 4 _${_PYTHON_PREFIX}_ARCH)
      set (_${_PYTHON_PREFIX}_ARCH2 ${_${_PYTHON_PREFIX}_ARCH})

      list (GET _${_PYTHON_PREFIX}_INTERPRETER_PROPERTIES 5 _${_PYTHON_PREFIX}_ABIFLAGS)
      list (GET _${_PYTHON_PREFIX}_INTERPRETER_PROPERTIES 6 ${_PYTHON_PREFIX}_SOABI)

      list (GET _${_PYTHON_PREFIX}_INTERPRETER_PROPERTIES 7 ${_PYTHON_PREFIX}_STDLIB)
      list (GET _${_PYTHON_PREFIX}_INTERPRETER_PROPERTIES 8 ${_PYTHON_PREFIX}_STDARCH)
      list (GET _${_PYTHON_PREFIX}_INTERPRETER_PROPERTIES 9 ${_PYTHON_PREFIX}_SITELIB)
      list (GET _${_PYTHON_PREFIX}_INTERPRETER_PROPERTIES 10 ${_PYTHON_PREFIX}_SITEARCH)
    else()
      string (REGEX MATCHALL "[0-9]+" _${_PYTHON_PREFIX}_VERSIONS "${${_PYTHON_PREFIX}_VERSION}")
      list (GET _${_PYTHON_PREFIX}_VERSIONS 0 ${_PYTHON_PREFIX}_VERSION_MAJOR)
      list (GET _${_PYTHON_PREFIX}_VERSIONS 1 ${_PYTHON_PREFIX}_VERSION_MINOR)
      list (GET _${_PYTHON_PREFIX}_VERSIONS 2 ${_PYTHON_PREFIX}_VERSION_PATCH)

      if (${_PYTHON_PREFIX}_VERSION_MAJOR VERSION_EQUAL _${_PYTHON_PREFIX}_REQUIRED_VERSION_MAJOR)
        set (${_PYTHON_PREFIX}_Interpreter_FOUND TRUE)

        # Use interpreter version and ABI for future searches to ensure consistency
        set (_${_PYTHON_PREFIX}_FIND_VERSIONS ${${_PYTHON_PREFIX}_VERSION_MAJOR}.${${_PYTHON_PREFIX}_VERSION_MINOR})
        execute_process (COMMAND ${_${_PYTHON_PREFIX}_INTERPRETR_LAUNCHER} "${_${_PYTHON_PREFIX}_EXECUTABLE}" -c
                                 "import sys; sys.stdout.write(sys.abiflags)"
                         RESULT_VARIABLE _${_PYTHON_PREFIX}_RESULT
                         OUTPUT_VARIABLE _${_PYTHON_PREFIX}_ABIFLAGS
                         ERROR_QUIET
                         OUTPUT_STRIP_TRAILING_WHITESPACE)
        if (_${_PYTHON_PREFIX}_RESULT)
          # assunme ABI is not supported
          set (_${_PYTHON_PREFIX}_ABIFLAGS "")
        endif()
      endif()

      if (${_PYTHON_PREFIX}_Interpreter_FOUND)
        unset (_${_PYTHON_PREFIX}_Interpreter_REASON_FAILURE)

        # compute and save interpreter signature
        string (MD5 __${_PYTHON_PREFIX}_INTERPRETER_SIGNATURE "${_${_PYTHON_PREFIX}_SIGNATURE}:${_${_PYTHON_PREFIX}_EXECUTABLE}")
        set (_${_PYTHON_PREFIX}_INTERPRETER_SIGNATURE "${__${_PYTHON_PREFIX}_INTERPRETER_SIGNATURE}" CACHE INTERNAL "")

        if (NOT CMAKE_SIZEOF_VOID_P)
          # determine interpreter architecture
          execute_process (COMMAND ${_${_PYTHON_PREFIX}_INTERPRETER_LAUNCHER} "${_${_PYTHON_PREFIX}_EXECUTABLE}" -c
                                   "import sys; sys.stdout.write(str(sys.maxsize > 2**32))"
                           RESULT_VARIABLE _${_PYTHON_PREFIX}_RESULT
                           OUTPUT_VARIABLE ${_PYTHON_PREFIX}_IS64BIT
                           ERROR_VARIABLE ${_PYTHON_PREFIX}_IS64BIT)
          if (NOT _${_PYTHON_PREFIX}_RESULT)
            if (${_PYTHON_PREFIX}_IS64BIT)
              set (_${_PYTHON_PREFIX}_ARCH 64)
              set (_${_PYTHON_PREFIX}_ARCH2 64)
            else()
              set (_${_PYTHON_PREFIX}_ARCH 32)
              set (_${_PYTHON_PREFIX}_ARCH2 32)
            endif()
          endif()
        endif()

        # retrieve interpreter identity
        execute_process (COMMAND ${_${_PYTHON_PREFIX}_INTERPRETER_LAUNCHER} "${_${_PYTHON_PREFIX}_EXECUTABLE}" -V
                         RESULT_VARIABLE _${_PYTHON_PREFIX}_RESULT
                         OUTPUT_VARIABLE ${_PYTHON_PREFIX}_INTERPRETER_ID
                         ERROR_VARIABLE ${_PYTHON_PREFIX}_INTERPRETER_ID)
        if (NOT _${_PYTHON_PREFIX}_RESULT)
          if (${_PYTHON_PREFIX}_INTERPRETER_ID MATCHES "Anaconda")
            set (${_PYTHON_PREFIX}_INTERPRETER_ID "Anaconda")
          elseif (${_PYTHON_PREFIX}_INTERPRETER_ID MATCHES "Enthought")
            set (${_PYTHON_PREFIX}_INTERPRETER_ID "Canopy")
          elseif (${_PYTHON_PREFIX}_INTERPRETER_ID MATCHES "PyPy ([0-9.]+)")
            set (${_PYTHON_PREFIX}_INTERPRETER_ID "PyPy")
            set  (${_PYTHON_PREFIX}_PyPy_VERSION "${CMAKE_MATCH_1}")
          else()
            string (REGEX REPLACE "^([^ ]+).*" "\\1" ${_PYTHON_PREFIX}_INTERPRETER_ID "${${_PYTHON_PREFIX}_INTERPRETER_ID}")
            if (${_PYTHON_PREFIX}_INTERPRETER_ID STREQUAL "Python")
              # try to get a more precise ID
              execute_process (COMMAND ${_${_PYTHON_PREFIX}_INTERPRETER_LAUNCHER} "${_${_PYTHON_PREFIX}_EXECUTABLE}" -c
                                       "import sys; sys.stdout.write(sys.copyright)"
                               RESULT_VARIABLE _${_PYTHON_PREFIX}_RESULT
                               OUTPUT_VARIABLE ${_PYTHON_PREFIX}_COPYRIGHT
                               ERROR_QUIET)
              if (${_PYTHON_PREFIX}_COPYRIGHT MATCHES "ActiveState")
                set (${_PYTHON_PREFIX}_INTERPRETER_ID "ActivePython")
              endif()
            endif()
          endif()
        else()
          set (${_PYTHON_PREFIX}_INTERPRETER_ID Python)
        endif()

        # retrieve various package installation directories
        execute_process (COMMAND ${_${_PYTHON_PREFIX}_INTERPRETER_LAUNCHER} "${_${_PYTHON_PREFIX}_EXECUTABLE}" -c
                                 "import sys\ntry:\n   from distutils import sysconfig\n   sys.stdout.write(';'.join([sysconfig.get_python_lib(plat_specific=False,standard_lib=True),sysconfig.get_python_lib(plat_specific=True,standard_lib=True),sysconfig.get_python_lib(plat_specific=False,standard_lib=False),sysconfig.get_python_lib(plat_specific=True,standard_lib=False)]))\nexcept Exception:\n   import sysconfig\n   sys.stdout.write(';'.join([sysconfig.get_path('stdlib'),sysconfig.get_path('platstdlib'),sysconfig.get_path('purelib'),sysconfig.get_path('platlib')]))"
                         RESULT_VARIABLE _${_PYTHON_PREFIX}_RESULT
                         OUTPUT_VARIABLE _${_PYTHON_PREFIX}_LIBPATHS
                         ERROR_QUIET)
        if (NOT _${_PYTHON_PREFIX}_RESULT)
          list (GET _${_PYTHON_PREFIX}_LIBPATHS 0 ${_PYTHON_PREFIX}_STDLIB)
          list (GET _${_PYTHON_PREFIX}_LIBPATHS 1 ${_PYTHON_PREFIX}_STDARCH)
          list (GET _${_PYTHON_PREFIX}_LIBPATHS 2 ${_PYTHON_PREFIX}_SITELIB)
          list (GET _${_PYTHON_PREFIX}_LIBPATHS 3 ${_PYTHON_PREFIX}_SITEARCH)
        else()
          unset (${_PYTHON_PREFIX}_STDLIB)
          unset (${_PYTHON_PREFIX}_STDARCH)
          unset (${_PYTHON_PREFIX}_SITELIB)
          unset (${_PYTHON_PREFIX}_SITEARCH)
        endif()

        _python_get_config_var (${_PYTHON_PREFIX}_SOABI SOABI)

        # store properties in the cache to speed-up future searches
        set (_${_PYTHON_PREFIX}_INTERPRETER_PROPERTIES
          "${${_PYTHON_PREFIX}_INTERPRETER_ID};${${_PYTHON_PREFIX}_VERSION_MAJOR};${${_PYTHON_PREFIX}_VERSION_MINOR};${${_PYTHON_PREFIX}_VERSION_PATCH};${_${_PYTHON_PREFIX}_ARCH};${_${_PYTHON_PREFIX}_ABIFLAGS};${${_PYTHON_PREFIX}_SOABI};${${_PYTHON_PREFIX}_STDLIB};${${_PYTHON_PREFIX}_STDARCH};${${_PYTHON_PREFIX}_SITELIB};${${_PYTHON_PREFIX}_SITEARCH}" CACHE INTERNAL "${_PYTHON_PREFIX} Properties")
      else()
        unset (_${_PYTHON_PREFIX}_INTERPRETER_SIGNATURE CACHE)
        unset (${_PYTHON_PREFIX}_INTERPRETER_ID)
      endif()
    endif()
  endif()

  if (${_PYTHON_PREFIX}_ARTIFACTS_INTERACTIVE)
    set (${_PYTHON_PREFIX}_EXECUTABLE "${_${_PYTHON_PREFIX}_EXECUTABLE}" CACHE FILEPATH "${_PYTHON_PREFIX} Interpreter")
  endif()

  _python_mark_as_internal (_${_PYTHON_PREFIX}_EXECUTABLE
                            _${_PYTHON_PREFIX}_INTERPRETER_PROPERTIES
                            _${_PYTHON_PREFIX}_INTERPRETER_SIGNATURE)
endif()


# second step, search for compiler (IronPython)
if ("Compiler" IN_LIST ${_PYTHON_PREFIX}_FIND_COMPONENTS)
  list (APPEND _${_PYTHON_PREFIX}_CACHED_VARS _${_PYTHON_PREFIX}_COMPILER)
  if (${_PYTHON_PREFIX}_FIND_REQUIRED_Compiler)
    list (APPEND _${_PYTHON_PREFIX}_REQUIRED_VARS ${_PYTHON_PREFIX}_COMPILER)
  endif()

  if (NOT "IronPython" IN_LIST _${_PYTHON_PREFIX}_FIND_IMPLEMENTATIONS)
    unset (_${_PYTHON_PREFIX}_COMPILER CACHE)
    unset (_${_PYTHON_PREFIX}_COMPILER_SIGNATURE CACHE)
  elseif (DEFINED ${_PYTHON_PREFIX}_COMPILER
      AND IS_ABSOLUTE "${${_PYTHON_PREFIX}_COMPILER}")
    set (_${_PYTHON_PREFIX}_COMPILER "${${_PYTHON_PREFIX}_COMPILER}" CACHE INTERNAL "")
  elseif (DEFINED _${_PYTHON_PREFIX}_COMPILER)
    # compute compiler signature and check validity of definition
    string (MD5 __${_PYTHON_PREFIX}_COMPILER_SIGNATURE "${_${_PYTHON_PREFIX}_SIGNATURE}:${_${_PYTHON_PREFIX}_COMPILER}")
    if (__${_PYTHON_PREFIX}_COMPILER_SIGNATURE STREQUAL _${_PYTHON_PREFIX}_COMPILER_SIGNATURE)
      # check version validity
      if (${_PYTHON_PREFIX}_FIND_VERSION_EXACT)
        _python_validate_compiler (VERSION ${${_PYTHON_PREFIX}_FIND_VERSION} EXACT CHECK_EXISTS)
      elseif (${_PYTHON_PREFIX}_FIND_VERSION_RANGE)
        _python_validate_compiler (IN_RANGE CHECK_EXISTS)
      elseif (DEFINED ${_PYTHON_PREFIX}_FIND_VERSION)
        _python_validate_compiler (VERSION ${${_PYTHON_PREFIX}_FIND_VERSION} CHECK_EXISTS)
      else()
        _python_validate_compiler (CHECK_EXISTS)
      endif()
    else()
      unset (_${_PYTHON_PREFIX}_COMPILER CACHE)
      unset (_${_PYTHON_PREFIX}_COMPILER_SIGNATURE CACHE)
    endif()
  endif()

  if ("IronPython" IN_LIST _${_PYTHON_PREFIX}_FIND_IMPLEMENTATIONS
      AND NOT _${_PYTHON_PREFIX}_COMPILER)
    # IronPython specific artifacts
    # If IronPython interpreter is found, use its path
    unset (_${_PYTHON_PREFIX}_IRON_ROOT)
    if (${_PYTHON_PREFIX}_Interpreter_FOUND AND ${_PYTHON_PREFIX}_INTERPRETER_ID STREQUAL "IronPython")
      get_filename_component (_${_PYTHON_PREFIX}_IRON_ROOT "${${_PYTHON_PREFIX}_EXECUTABLE}" DIRECTORY)
    endif()

    if (_${_PYTHON_PREFIX}_FIND_STRATEGY STREQUAL "LOCATION")
      _python_get_names (_${_PYTHON_PREFIX}_COMPILER_NAMES
                         IMPLEMENTATIONS IronPython
                         VERSION ${_${_PYTHON_PREFIX}_FIND_VERSIONS}
                         COMPILER)

      _python_get_path_suffixes (_${_PYTHON_PREFIX}_PATH_SUFFIXES
                                 IMPLEMENTATIONS IronPython
                                 VERSION ${_${_PYTHON_PREFIX}_FIND_VERSIONS}
                                 COMPILER)

      _python_get_frameworks (_${_PYTHON_PREFIX}_FRAMEWORK_PATHS
                              IMPLEMENTATIONS IronPython
                              VERSION ${_${_PYTHON_PREFIX}_FIND_VERSIONS})
      _python_get_registries (_${_PYTHON_PREFIX}_REGISTRY_PATHS
                              IMPLEMENTATIONS IronPython
                              VERSION ${_${_PYTHON_PREFIX}_FIND_VERSIONS})

      set (_${_PYTHON_PREFIX}_VALIDATE_OPTIONS ${_${_PYTHON_PREFIX}_FIND_VERSION_EXACT})
      if (${_PYTHON_PREFIX}_FIND_VERSION_RANGE)
        list (APPEND _${_PYTHON_PREFIX}_VALIDATE_OPTIONS IN_RANGE)
      elseif (DEFINED ${_PYTHON_PREFIX}_FIND_VERSION)
        list (APPEND _${_PYTHON_PREFIX}_VALIDATE_OPTIONS VERSION ${${_PYTHON_PREFIX}_FIND_VERSION})
      endif()

      while (TRUE)
        # Apple frameworks handling
        if (CMAKE_HOST_APPLE AND _${_PYTHON_PREFIX}_FIND_FRAMEWORK STREQUAL "FIRST")
          find_program (_${_PYTHON_PREFIX}_COMPILER
                        NAMES ${_${_PYTHON_PREFIX}_COMPILER_NAMES}
                        NAMES_PER_DIR
                        HINTS ${_${_PYTHON_PREFIX}_IRON_ROOT} ${_${_PYTHON_PREFIX}_HINTS}
                        PATHS ${_${_PYTHON_PREFIX}_FRAMEWORK_PATHS}
                        PATH_SUFFIXES ${_${_PYTHON_PREFIX}_PATH_SUFFIXES}
                        NO_CMAKE_PATH
                        NO_CMAKE_ENVIRONMENT_PATH
                        NO_SYSTEM_ENVIRONMENT_PATH
                        NO_CMAKE_SYSTEM_PATH)
          _python_validate_compiler (${_${_PYTHON_PREFIX}_VALIDATE_OPTIONS})
          if (_${_PYTHON_PREFIX}_COMPILER)
            break()
          endif()
        endif()
        # Windows registry
        if (CMAKE_HOST_WIN32 AND _${_PYTHON_PREFIX}_FIND_REGISTRY STREQUAL "FIRST")
          find_program (_${_PYTHON_PREFIX}_COMPILER
                        NAMES ${_${_PYTHON_PREFIX}_COMPILER_NAMES}
                        NAMES_PER_DIR
                        HINTS ${_${_PYTHON_PREFIX}_IRON_ROOT} ${_${_PYTHON_PREFIX}_HINTS}
                        PATHS ${_${_PYTHON_PREFIX}_REGISTRY_PATHS}
                        PATH_SUFFIXES ${_${_PYTHON_PREFIX}_PATH_SUFFIXES}
                        NO_SYSTEM_ENVIRONMENT_PATH
                        NO_CMAKE_SYSTEM_PATH)
          _python_validate_compiler (${_${_PYTHON_PREFIX}_VALIDATE_OPTIONS})
          if (_${_PYTHON_PREFIX}_COMPILER)
            break()
          endif()
        endif()

        # try using HINTS
        find_program (_${_PYTHON_PREFIX}_COMPILER
                      NAMES ${_${_PYTHON_PREFIX}_COMPILER_NAMES}
                      NAMES_PER_DIR
                      HINTS ${_${_PYTHON_PREFIX}_IRON_ROOT} ${_${_PYTHON_PREFIX}_HINTS}
                      PATH_SUFFIXES ${_${_PYTHON_PREFIX}_PATH_SUFFIXES}
                      NO_SYSTEM_ENVIRONMENT_PATH
                      NO_CMAKE_SYSTEM_PATH)
        _python_validate_compiler (${_${_PYTHON_PREFIX}_VALIDATE_OPTIONS})
        if (_${_PYTHON_PREFIX}_COMPILER)
          break()
        endif()

        # try using standard paths
        find_program (_${_PYTHON_PREFIX}_COMPILER
                      NAMES ${_${_PYTHON_PREFIX}_COMPILER_NAMES}
                      NAMES_PER_DIR
                      PATH_SUFFIXES ${_${_PYTHON_PREFIX}_PATH_SUFFIXES})
        _python_validate_compiler (${_${_PYTHON_PREFIX}_VALIDATE_OPTIONS})
        if (_${_PYTHON_PREFIX}_COMPILER)
          break()
        endif()

        # Apple frameworks handling
        if (CMAKE_HOST_APPLE AND _${_PYTHON_PREFIX}_FIND_FRAMEWORK STREQUAL "LAST")
          find_program (_${_PYTHON_PREFIX}_COMPILER
                        NAMES ${_${_PYTHON_PREFIX}_COMPILER_NAMES}
                        NAMES_PER_DIR
                        PATHS ${_${_PYTHON_PREFIX}_FRAMEWORK_PATHS}
                        PATH_SUFFIXES ${_${_PYTHON_PREFIX}_PATH_SUFFIXES}
                        NO_DEFAULT_PATH)
          _python_validate_compiler (${_${_PYTHON_PREFIX}_VALIDATE_OPTIONS})
          if (_${_PYTHON_PREFIX}_COMPILER)
            break()
          endif()
        endif()
        # Windows registry
        if (CMAKE_HOST_WIN32 AND _${_PYTHON_PREFIX}_FIND_REGISTRY STREQUAL "LAST")
          find_program (_${_PYTHON_PREFIX}_COMPILER
                        NAMES ${_${_PYTHON_PREFIX}_COMPILER_NAMES}
                        NAMES_PER_DIR
                        PATHS ${_${_PYTHON_PREFIX}_REGISTRY_PATHS}
                        PATH_SUFFIXES ${_${_PYTHON_PREFIX}_PATH_SUFFIXES}
                        NO_DEFAULT_PATH)
          _python_validate_compiler (${_${_PYTHON_PREFIX}_VALIDATE_OPTIONS})
          if (_${_PYTHON_PREFIX}_COMPILER)
            break()
          endif()
        endif()

        break()
      endwhile()
    else()
      # try using root dir and registry
      set (_${_PYTHON_PREFIX}_VALIDATE_OPTIONS EXACT)
      if (${_PYTHON_PREFIX}_FIND_VERSION_RANGE)
        list (APPEND _${_PYTHON_PREFIX}_VALIDATE_OPTIONS IN_RANGE)
      endif()

      foreach (_${_PYTHON_PREFIX}_VERSION IN LISTS _${_PYTHON_PREFIX}_FIND_VERSIONS)
        _python_get_names (_${_PYTHON_PREFIX}_COMPILER_NAMES
                           IMPLEMENTATIONS IronPython
                           VERSION ${_${_PYTHON_PREFIX}_FIND_VERSIONS}
                           COMPILER)

        _python_get_path_suffixes (_${_PYTHON_PREFIX}_PATH_SUFFIXES
                                   IMPLEMENTATIONS IronPython
                                   VERSION ${_${_PYTHON_PREFIX}_FIND_VERSION}
                                   COMPILER)

        _python_get_frameworks (_${_PYTHON_PREFIX}_FRAMEWORK_PATHS
                                IMPLEMENTATIONS IronPython
                                VERSION ${_${_PYTHON_PREFIX}_VERSION})
        _python_get_registries (_${_PYTHON_PREFIX}_REGISTRY_PATHS
                                IMPLEMENTATIONS IronPython
                                VERSION ${_${_PYTHON_PREFIX}_VERSION})

        # Apple frameworks handling
        if (CMAKE_HOST_APPLE AND _${_PYTHON_PREFIX}_FIND_FRAMEWORK STREQUAL "FIRST")
          find_program (_${_PYTHON_PREFIX}_COMPILER
                        NAMES ${_${_PYTHON_PREFIX}_COMPILER_NAMES}
                        NAMES_PER_DIR
                        HINTS ${_${_PYTHON_PREFIX}_IRON_ROOT} ${_${_PYTHON_PREFIX}_HINTS}
                        PATHS ${_${_PYTHON_PREFIX}_FRAMEWORK_PATHS}
                        PATH_SUFFIXES ${_${_PYTHON_PREFIX}_PATH_SUFFIXES}
                        NO_CMAKE_PATH
                        NO_CMAKE_ENVIRONMENT_PATH
                        NO_SYSTEM_ENVIRONMENT_PATH
                        NO_CMAKE_SYSTEM_PATH)
          _python_validate_compiler (VERSION ${_${_PYTHON_PREFIX}_VERSION} ${_${_PYTHON_PREFIX}_VALIDATE_OPTIONS})
          if (_${_PYTHON_PREFIX}_COMPILER)
            break()
          endif()
        endif()
        # Windows registry
        if (CMAKE_HOST_WIN32 AND _${_PYTHON_PREFIX}_FIND_REGISTRY STREQUAL "FIRST")
          find_program (_${_PYTHON_PREFIX}_COMPILER
                        NAMES ${_${_PYTHON_PREFIX}_COMPILER_NAMES}
                        NAMES_PER_DIR
                        HINTS ${_${_PYTHON_PREFIX}_IRON_ROOT} ${_${_PYTHON_PREFIX}_HINTS}
                        PATHS ${_${_PYTHON_PREFIX}_REGISTRY_PATHS}
                        PATH_SUFFIXES ${_${_PYTHON_PREFIX}_PATH_SUFFIXES}
                        NO_SYSTEM_ENVIRONMENT_PATH
                        NO_CMAKE_SYSTEM_PATH)
          _python_validate_compiler (VERSION ${_${_PYTHON_PREFIX}_VERSION} ${_${_PYTHON_PREFIX}_VALIDATE_OPTIONS})
          if (_${_PYTHON_PREFIX}_COMPILER)
            break()
          endif()
        endif()

        # try using HINTS
        find_program (_${_PYTHON_PREFIX}_COMPILER
                      NAMES ${_${_PYTHON_PREFIX}_COMPILER_NAMES}
                      NAMES_PER_DIR
                      HINTS ${_${_PYTHON_PREFIX}_IRON_ROOT} ${_${_PYTHON_PREFIX}_HINTS}
                      PATH_SUFFIXES ${_${_PYTHON_PREFIX}_PATH_SUFFIXES}
                      NO_SYSTEM_ENVIRONMENT_PATH
                      NO_CMAKE_SYSTEM_PATH)
        _python_validate_compiler (VERSION ${_${_PYTHON_PREFIX}_VERSION} ${_${_PYTHON_PREFIX}_VALIDATE_OPTIONS})
        if (_${_PYTHON_PREFIX}_COMPILER)
          break()
        endif()

        # Apple frameworks handling
        if (CMAKE_HOST_APPLE AND _${_PYTHON_PREFIX}_FIND_FRAMEWORK STREQUAL "LAST")
          find_program (_${_PYTHON_PREFIX}_COMPILER
                        NAMES ${_${_PYTHON_PREFIX}_COMPILER_NAMES}
                        NAMES_PER_DIR
                        PATHS ${_${_PYTHON_PREFIX}_FRAMEWORK_PATHS}
                        PATH_SUFFIXES ${_${_PYTHON_PREFIX}_PATH_SUFFIXES}
                        NO_DEFAULT_PATH)
          _python_validate_compiler (VERSION ${_${_PYTHON_PREFIX}_VERSION} ${_${_PYTHON_PREFIX}_VALIDATE_OPTIONS})
          if (_${_PYTHON_PREFIX}_COMPILER)
            break()
          endif()
        endif()
        # Windows registry
        if (CMAKE_HOST_WIN32 AND _${_PYTHON_PREFIX}_FIND_REGISTRY STREQUAL "LAST")
          find_program (_${_PYTHON_PREFIX}_COMPILER
                        NAMES ${_${_PYTHON_PREFIX}_COMPILER_NAMES}
                        NAMES_PER_DIR
                        PATHS ${_${_PYTHON_PREFIX}_REGISTRY_PATHS}
                        PATH_SUFFIXES ${_${_PYTHON_PREFIX}_PATH_SUFFIXES}
                        NO_DEFAULT_PATH)
          _python_validate_compiler (VERSION ${_${_PYTHON_PREFIX}_VERSION} ${_${_PYTHON_PREFIX}_VALIDATE_OPTIONS})
          if (_${_PYTHON_PREFIX}_COMPILER)
            break()
          endif()
        endif()
      endforeach()

      # no specific version found, re-try in standard paths
      _python_get_names (_${_PYTHON_PREFIX}_COMPILER_NAMES
                         IMPLEMENTATIONS IronPython
                         VERSION ${_${_PYTHON_PREFIX}_FIND_VERSIONS}
                         COMPILER)
      _python_get_path_suffixes (_${_PYTHON_PREFIX}_PATH_SUFFIXES
                                 IMPLEMENTATIONS IronPython
                                 VERSION ${_${_PYTHON_PREFIX}_FIND_VERSIONS}
                                 COMPILER)
      find_program (_${_PYTHON_PREFIX}_COMPILER
                    NAMES ${_${_PYTHON_PREFIX}_COMPILER_NAMES}
                    HINTS ${_${_PYTHON_PREFIX}_IRON_ROOT} ${_${_PYTHON_PREFIX}_HINTS}
                    PATH_SUFFIXES ${_${_PYTHON_PREFIX}_PATH_SUFFIXES})
      _python_validate_compiler ()
    endif()
  endif()

  set (${_PYTHON_PREFIX}_COMPILER "${_${_PYTHON_PREFIX}_COMPILER}")

  if (_${_PYTHON_PREFIX}_COMPILER)
    # retrieve python environment version from compiler
    _python_get_launcher (_${_PYTHON_PREFIX}_COMPILER_LAUNCHER COMPILER)
    set (_${_PYTHON_PREFIX}_VERSION_DIR "${CMAKE_CURRENT_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/PythonCompilerVersion.dir")
    file (WRITE "${_${_PYTHON_PREFIX}_VERSION_DIR}/version.py" "import sys; sys.stdout.write('.'.join([str(x) for x in sys.version_info[:3]]))\n")
    execute_process (COMMAND ${_${_PYTHON_PREFIX}_COMPILER_LAUNCHER} "${_${_PYTHON_PREFIX}_COMPILER}"
                             ${_${_PYTHON_PREFIX}_IRON_PYTHON_COMPILER_ARCH_FLAGS}
                             /target:exe /embed "${_${_PYTHON_PREFIX}_VERSION_DIR}/version.py"
                     WORKING_DIRECTORY "${_${_PYTHON_PREFIX}_VERSION_DIR}"
                     OUTPUT_QUIET
                     ERROR_QUIET)
    get_filename_component (_${_PYTHON_PREFIX}_IR_DIR "${_${_PYTHON_PREFIX}_COMPILER}" DIRECTORY)
    execute_process (COMMAND "${CMAKE_COMMAND}" -E env "MONO_PATH=${_${_PYTHON_PREFIX}_IR_DIR}"
                             ${${_PYTHON_PREFIX}_DOTNET_LAUNCHER} "${_${_PYTHON_PREFIX}_VERSION_DIR}/version.exe"
                     WORKING_DIRECTORY "${_${_PYTHON_PREFIX}_VERSION_DIR}"
                     RESULT_VARIABLE _${_PYTHON_PREFIX}_RESULT
                     OUTPUT_VARIABLE _${_PYTHON_PREFIX}_VERSION
                     ERROR_QUIET)
    if (NOT _${_PYTHON_PREFIX}_RESULT)
      set (_${_PYTHON_PREFIX}_COMPILER_USABLE TRUE)
      string (REGEX MATCHALL "[0-9]+" _${_PYTHON_PREFIX}_VERSIONS "${_${_PYTHON_PREFIX}_VERSION}")
      list (GET _${_PYTHON_PREFIX}_VERSIONS 0 _${_PYTHON_PREFIX}_VERSION_MAJOR)
      list (GET _${_PYTHON_PREFIX}_VERSIONS 1 _${_PYTHON_PREFIX}_VERSION_MINOR)
      list (GET _${_PYTHON_PREFIX}_VERSIONS 2 _${_PYTHON_PREFIX}_VERSION_PATCH)

      if (NOT ${_PYTHON_PREFIX}_Interpreter_FOUND)
        # set public version information
        set (${_PYTHON_PREFIX}_VERSION ${_${_PYTHON_PREFIX}_VERSION})
        set (${_PYTHON_PREFIX}_VERSION_MAJOR ${_${_PYTHON_PREFIX}_VERSION_MAJOR})
        set (${_PYTHON_PREFIX}_VERSION_MINOR ${_${_PYTHON_PREFIX}_VERSION_MINOR})
        set (${_PYTHON_PREFIX}_VERSION_PATCH ${_${_PYTHON_PREFIX}_VERSION_PATCH})
      endif()
    else()
      # compiler not usable
      set (_${_PYTHON_PREFIX}_COMPILER_USABLE FALSE)
      set (_${_PYTHON_PREFIX}_Compiler_REASON_FAILURE "Cannot run the compiler \"${_${_PYTHON_PREFIX}_COMPILER}\"")
    endif()
    file (REMOVE_RECURSE "${_${_PYTHON_PREFIX}_VERSION_DIR}")
  endif()

  if (_${_PYTHON_PREFIX}_COMPILER AND _${_PYTHON_PREFIX}_COMPILER_USABLE)
    if (${_PYTHON_PREFIX}_Interpreter_FOUND)
      # Compiler must be compatible with interpreter
      if ("${_${_PYTHON_PREFIX}_VERSION_MAJOR}.${_${_PYTHON_PREFIX}_VERSION_MINOR}" VERSION_EQUAL "${${_PYTHON_PREFIX}_VERSION_MAJOR}.${${_PYTHON_PREFIX}_VERSION_MINOR}")
        set (${_PYTHON_PREFIX}_Compiler_FOUND TRUE)
      endif()
    elseif (${_PYTHON_PREFIX}_VERSION_MAJOR VERSION_EQUAL _${_PYTHON_PREFIX}_REQUIRED_VERSION_MAJOR)
      set (${_PYTHON_PREFIX}_Compiler_FOUND TRUE)
      # Use compiler version for future searches to ensure consistency
      set (_${_PYTHON_PREFIX}_FIND_VERSIONS ${${_PYTHON_PREFIX}_VERSION_MAJOR}.${${_PYTHON_PREFIX}_VERSION_MINOR})
    endif()
  endif()

  if (${_PYTHON_PREFIX}_Compiler_FOUND)
    unset (_${_PYTHON_PREFIX}_Compiler_REASON_FAILURE)

    # compute and save compiler signature
    string (MD5 __${_PYTHON_PREFIX}_COMPILER_SIGNATURE "${_${_PYTHON_PREFIX}_SIGNATURE}:${_${_PYTHON_PREFIX}_COMPILER}")
    set (_${_PYTHON_PREFIX}_COMPILER_SIGNATURE "${__${_PYTHON_PREFIX}_COMPILER_SIGNATURE}" CACHE INTERNAL "")

    set (${_PYTHON_PREFIX}_COMPILER_ID IronPython)
  else()
    unset (_${_PYTHON_PREFIX}_COMPILER_SIGNATURE CACHE)
    unset (${_PYTHON_PREFIX}_COMPILER_ID)
  endif()

  if (${_PYTHON_PREFIX}_ARTIFACTS_INTERACTIVE)
    set (${_PYTHON_PREFIX}_COMPILER "${_${_PYTHON_PREFIX}_COMPILER}" CACHE FILEPATH "${_PYTHON_PREFIX} Compiler")
  endif()

  _python_mark_as_internal (_${_PYTHON_PREFIX}_COMPILER
                            _${_PYTHON_PREFIX}_COMPILER_SIGNATURE)
endif()

# third step, search for the development artifacts
if (${_PYTHON_PREFIX}_FIND_REQUIRED_Development.Module)
  if ("LIBRARY" IN_LIST _${_PYTHON_PREFIX}_FIND_DEVELOPMENT_MODULE_ARTIFACTS)
    list (APPEND _${_PYTHON_PREFIX}_REQUIRED_VARS ${_PYTHON_PREFIX}_LIBRARIES)
  endif()
  if ("INCLUDE_DIR" IN_LIST _${_PYTHON_PREFIX}_FIND_DEVELOPMENT_MODULE_ARTIFACTS)
    list (APPEND _${_PYTHON_PREFIX}_REQUIRED_VARS ${_PYTHON_PREFIX}_INCLUDE_DIRS)
  endif()
endif()
if (${_PYTHON_PREFIX}_FIND_REQUIRED_Development.Embed)
  if ("LIBRARY" IN_LIST _${_PYTHON_PREFIX}_FIND_DEVELOPMENT_EMBED_ARTIFACTS)
    list (APPEND _${_PYTHON_PREFIX}_REQUIRED_VARS ${_PYTHON_PREFIX}_LIBRARIES)
  endif()
  if ("INCLUDE_DIR" IN_LIST _${_PYTHON_PREFIX}_FIND_DEVELOPMENT_EMBED_ARTIFACTS)
    list (APPEND _${_PYTHON_PREFIX}_REQUIRED_VARS ${_PYTHON_PREFIX}_INCLUDE_DIRS)
  endif()
endif()
list (REMOVE_DUPLICATES _${_PYTHON_PREFIX}_REQUIRED_VARS)
## Development environment is not compatible with IronPython interpreter
if (("Development.Module" IN_LIST ${_PYTHON_PREFIX}_FIND_COMPONENTS
      OR "Development.Embed" IN_LIST ${_PYTHON_PREFIX}_FIND_COMPONENTS)
    AND ((${_PYTHON_PREFIX}_Interpreter_FOUND
        AND NOT ${_PYTHON_PREFIX}_INTERPRETER_ID STREQUAL "IronPython")
      OR NOT ${_PYTHON_PREFIX}_Interpreter_FOUND))
  if (${_PYTHON_PREFIX}_Interpreter_FOUND)
    # reduce possible implementations to the interpreter one
    if (${_PYTHON_PREFIX}_INTERPRETER_ID STREQUAL "PyPy")
      set (_${_PYTHON_PREFIX}_FIND_IMPLEMENTATIONS "PyPy")
    else()
      set (_${_PYTHON_PREFIX}_FIND_IMPLEMENTATIONS "CPython")
    endif()
  else()
    list (REMOVE_ITEM _${_PYTHON_PREFIX}_FIND_IMPLEMENTATIONS "IronPython")
  endif()
  if ("LIBRARY" IN_LIST _${_PYTHON_PREFIX}_FIND_DEVELOPMENT_ARTIFACTS)
  list (APPEND _${_PYTHON_PREFIX}_CACHED_VARS _${_PYTHON_PREFIX}_LIBRARY_RELEASE
                                              _${_PYTHON_PREFIX}_RUNTIME_LIBRARY_RELEASE
                                              _${_PYTHON_PREFIX}_LIBRARY_DEBUG
                                              _${_PYTHON_PREFIX}_RUNTIME_LIBRARY_DEBUG)
  endif()
  if ("INCLUDE_DIR" IN_LIST _${_PYTHON_PREFIX}_FIND_DEVELOPMENT_ARTIFACTS)
    list (APPEND _${_PYTHON_PREFIX}_CACHED_VARS _${_PYTHON_PREFIX}_INCLUDE_DIR)
  endif()

  _python_check_development_signature (Module)
  _python_check_development_signature (Embed)

  if (DEFINED ${_PYTHON_PREFIX}_LIBRARY
      AND IS_ABSOLUTE "${${_PYTHON_PREFIX}_LIBRARY}")
    set (_${_PYTHON_PREFIX}_LIBRARY_RELEASE "${${_PYTHON_PREFIX}_LIBRARY}" CACHE INTERNAL "")
    unset (_${_PYTHON_PREFIX}_LIBRARY_DEBUG CACHE)
    unset (_${_PYTHON_PREFIX}_INCLUDE_DIR CACHE)
  endif()
  if (DEFINED ${_PYTHON_PREFIX}_INCLUDE_DIR
      AND IS_ABSOLUTE "${${_PYTHON_PREFIX}_INCLUDE_DIR}")
    set (_${_PYTHON_PREFIX}_INCLUDE_DIR "${${_PYTHON_PREFIX}_INCLUDE_DIR}" CACHE INTERNAL "")
  endif()

  # Support preference of static libs by adjusting CMAKE_FIND_LIBRARY_SUFFIXES
  unset (_${_PYTHON_PREFIX}_CMAKE_FIND_LIBRARY_SUFFIXES)
  if (DEFINED ${_PYTHON_PREFIX}_USE_STATIC_LIBS AND NOT WIN32)
    set(_${_PYTHON_PREFIX}_CMAKE_FIND_LIBRARY_SUFFIXES ${CMAKE_FIND_LIBRARY_SUFFIXES})
    if(${_PYTHON_PREFIX}_USE_STATIC_LIBS)
      set (CMAKE_FIND_LIBRARY_SUFFIXES ${CMAKE_STATIC_LIBRARY_SUFFIX})
    else()
      list (REMOVE_ITEM CMAKE_FIND_LIBRARY_SUFFIXES ${CMAKE_STATIC_LIBRARY_SUFFIX})
    endif()
  endif()

  if (NOT _${_PYTHON_PREFIX}_LIBRARY_RELEASE OR NOT _${_PYTHON_PREFIX}_INCLUDE_DIR)
    # if python interpreter is found, use it to look-up for artifacts
    # to ensure consistency between interpreter and development environments.
    # If not, try to locate a compatible config tool
    if ((NOT ${_PYTHON_PREFIX}_Interpreter_FOUND OR CMAKE_CROSSCOMPILING)
        AND "CPython" IN_LIST _${_PYTHON_PREFIX}_FIND_IMPLEMENTATIONS)
      set (_${_PYTHON_PREFIX}_HINTS "${${_PYTHON_PREFIX}_ROOT_DIR}" ENV ${_PYTHON_PREFIX}_ROOT_DIR)
      unset (_${_PYTHON_PREFIX}_VIRTUALENV_PATHS)
      if (_${_PYTHON_PREFIX}_FIND_VIRTUALENV MATCHES "^(FIRST|ONLY)$")
        set (_${_PYTHON_PREFIX}_VIRTUALENV_PATHS ENV VIRTUAL_ENV ENV CONDA_PREFIX)
      endif()

      if (_${_PYTHON_PREFIX}_FIND_STRATEGY STREQUAL "LOCATION")
        _python_get_names (_${_PYTHON_PREFIX}_CONFIG_NAMES VERSION ${_${_PYTHON_PREFIX}_FIND_VERSIONS} POSIX CONFIG)
          # Framework Paths
        _python_get_frameworks (_${_PYTHON_PREFIX}_FRAMEWORK_PATHS VERSION ${_${_PYTHON_PREFIX}_FIND_VERSIONS})

        # Apple frameworks handling
        if (CMAKE_HOST_APPLE AND _${_PYTHON_PREFIX}_FIND_FRAMEWORK STREQUAL "FIRST")
          find_program (_${_PYTHON_PREFIX}_CONFIG
                        NAMES ${_${_PYTHON_PREFIX}_CONFIG_NAMES}
                        NAMES_PER_DIR
                        HINTS ${_${_PYTHON_PREFIX}_HINTS}
                        PATHS ${_${_PYTHON_PREFIX}_VIRTUALENV_PATHS}
                              ${_${_PYTHON_PREFIX}_FRAMEWORK_PATHS}
                        PATH_SUFFIXES bin
                        NO_CMAKE_PATH
                        NO_CMAKE_ENVIRONMENT_PATH
                        NO_SYSTEM_ENVIRONMENT_PATH
                        NO_CMAKE_SYSTEM_PATH)
        endif()

        find_program (_${_PYTHON_PREFIX}_CONFIG
                      NAMES ${_${_PYTHON_PREFIX}_CONFIG_NAMES}
                      NAMES_PER_DIR
                      HINTS ${_${_PYTHON_PREFIX}_HINTS}
                      PATHS ${_${_PYTHON_PREFIX}_VIRTUALENV_PATHS}
                      PATH_SUFFIXES bin)

        # Apple frameworks handling
        if (CMAKE_HOST_APPLE AND _${_PYTHON_PREFIX}_FIND_FRAMEWORK STREQUAL "LAST")
          find_program (_${_PYTHON_PREFIX}_CONFIG
                        NAMES ${_${_PYTHON_PREFIX}_CONFIG_NAMES}
                        NAMES_PER_DIR
                        PATHS ${_${_PYTHON_PREFIX}_FRAMEWORK_PATHS}
                        PATH_SUFFIXES bin
                        NO_DEFAULT_PATH)
        endif()

        if (_${_PYTHON_PREFIX}_CONFIG)
          execute_process (COMMAND "${_${_PYTHON_PREFIX}_CONFIG}" --help
                           RESULT_VARIABLE _${_PYTHON_PREFIX}_RESULT
                           OUTPUT_VARIABLE __${_PYTHON_PREFIX}_HELP
                           ERROR_QUIET
                           OUTPUT_STRIP_TRAILING_WHITESPACE)
          if (_${_PYTHON_PREFIX}_RESULT)
            # assume config tool is not usable
            unset (_${_PYTHON_PREFIX}_CONFIG CACHE)
          endif()
        endif()

        if (_${_PYTHON_PREFIX}_CONFIG)
          execute_process (COMMAND "${_${_PYTHON_PREFIX}_CONFIG}" --abiflags
                           RESULT_VARIABLE _${_PYTHON_PREFIX}_RESULT
                           OUTPUT_VARIABLE __${_PYTHON_PREFIX}_ABIFLAGS
                           ERROR_QUIET
                           OUTPUT_STRIP_TRAILING_WHITESPACE)
          if (_${_PYTHON_PREFIX}_RESULT)
            # assume ABI is not supported
            set (__${_PYTHON_PREFIX}_ABIFLAGS "")
          endif()
          if (DEFINED _${_PYTHON_PREFIX}_FIND_ABI AND NOT __${_PYTHON_PREFIX}_ABIFLAGS IN_LIST _${_PYTHON_PREFIX}_ABIFLAGS)
            # Wrong ABI
            unset (_${_PYTHON_PREFIX}_CONFIG CACHE)
          endif()
        endif()

        if (_${_PYTHON_PREFIX}_CONFIG AND DEFINED CMAKE_LIBRARY_ARCHITECTURE)
          # check that config tool match library architecture
          execute_process (COMMAND "${_${_PYTHON_PREFIX}_CONFIG}" --configdir
                           RESULT_VARIABLE _${_PYTHON_PREFIX}_RESULT
                           OUTPUT_VARIABLE _${_PYTHON_PREFIX}_CONFIGDIR
                           ERROR_QUIET
                           OUTPUT_STRIP_TRAILING_WHITESPACE)
          if (_${_PYTHON_PREFIX}_RESULT)
            unset (_${_PYTHON_PREFIX}_CONFIG CACHE)
          else()
            string(FIND "${_${_PYTHON_PREFIX}_CONFIGDIR}" "${CMAKE_LIBRARY_ARCHITECTURE}" _${_PYTHON_PREFIX}_RESULT)
            if (_${_PYTHON_PREFIX}_RESULT EQUAL -1)
              unset (_${_PYTHON_PREFIX}_CONFIG CACHE)
            endif()
          endif()
        endif()
      else()
        foreach (_${_PYTHON_PREFIX}_VERSION IN LISTS _${_PYTHON_PREFIX}_FIND_VERSIONS)
          # try to use pythonX.Y-config tool
          _python_get_names (_${_PYTHON_PREFIX}_CONFIG_NAMES VERSION ${_${_PYTHON_PREFIX}_VERSION} POSIX CONFIG)

          # Framework Paths
          _python_get_frameworks (_${_PYTHON_PREFIX}_FRAMEWORK_PATHS VERSION ${_${_PYTHON_PREFIX}_VERSION})

          # Apple frameworks handling
          if (CMAKE_HOST_APPLE AND _${_PYTHON_PREFIX}_FIND_FRAMEWORK STREQUAL "FIRST")
            find_program (_${_PYTHON_PREFIX}_CONFIG
                          NAMES ${_${_PYTHON_PREFIX}_CONFIG_NAMES}
                          NAMES_PER_DIR
                          HINTS ${_${_PYTHON_PREFIX}_HINTS}
                          PATHS ${_${_PYTHON_PREFIX}_VIRTUALENV_PATHS}
                                ${_${_PYTHON_PREFIX}_FRAMEWORK_PATHS}
                          PATH_SUFFIXES bin
                          NO_CMAKE_PATH
                          NO_CMAKE_ENVIRONMENT_PATH
                          NO_SYSTEM_ENVIRONMENT_PATH
                          NO_CMAKE_SYSTEM_PATH)
          endif()

          find_program (_${_PYTHON_PREFIX}_CONFIG
                        NAMES ${_${_PYTHON_PREFIX}_CONFIG_NAMES}
                        NAMES_PER_DIR
                        HINTS ${_${_PYTHON_PREFIX}_HINTS}
                        PATHS ${_${_PYTHON_PREFIX}_VIRTUALENV_PATHS}
                        PATH_SUFFIXES bin)

          # Apple frameworks handling
          if (CMAKE_HOST_APPLE AND _${_PYTHON_PREFIX}_FIND_FRAMEWORK STREQUAL "LAST")
            find_program (_${_PYTHON_PREFIX}_CONFIG
                          NAMES ${_${_PYTHON_PREFIX}_CONFIG_NAMES}
                          NAMES_PER_DIR
                          PATHS ${_${_PYTHON_PREFIX}_FRAMEWORK_PATHS}
                          PATH_SUFFIXES bin
                          NO_DEFAULT_PATH)
          endif()

          unset (_${_PYTHON_PREFIX}_CONFIG_NAMES)

          if (_${_PYTHON_PREFIX}_CONFIG)
            execute_process (COMMAND "${_${_PYTHON_PREFIX}_CONFIG}" --help
                             RESULT_VARIABLE _${_PYTHON_PREFIX}_RESULT
                             OUTPUT_VARIABLE __${_PYTHON_PREFIX}_HELP
                             ERROR_QUIET
                             OUTPUT_STRIP_TRAILING_WHITESPACE)
            if (_${_PYTHON_PREFIX}_RESULT)
              # assume config tool is not usable
              unset (_${_PYTHON_PREFIX}_CONFIG CACHE)
            endif()
          endif()

          if (NOT _${_PYTHON_PREFIX}_CONFIG)
            continue()
          endif()

          execute_process (COMMAND "${_${_PYTHON_PREFIX}_CONFIG}" --abiflags
                           RESULT_VARIABLE _${_PYTHON_PREFIX}_RESULT
                           OUTPUT_VARIABLE __${_PYTHON_PREFIX}_ABIFLAGS
                           ERROR_QUIET
                           OUTPUT_STRIP_TRAILING_WHITESPACE)
          if (_${_PYTHON_PREFIX}_RESULT)
            # assume ABI is not supported
            set (__${_PYTHON_PREFIX}_ABIFLAGS "")
          endif()
          if (DEFINED _${_PYTHON_PREFIX}_FIND_ABI AND NOT __${_PYTHON_PREFIX}_ABIFLAGS IN_LIST _${_PYTHON_PREFIX}_ABIFLAGS)
            # Wrong ABI
            unset (_${_PYTHON_PREFIX}_CONFIG CACHE)
            continue()
          endif()

          if (_${_PYTHON_PREFIX}_CONFIG AND DEFINED CMAKE_LIBRARY_ARCHITECTURE)
            # check that config tool match library architecture
            execute_process (COMMAND "${_${_PYTHON_PREFIX}_CONFIG}" --configdir
                             RESULT_VARIABLE _${_PYTHON_PREFIX}_RESULT
                             OUTPUT_VARIABLE _${_PYTHON_PREFIX}_CONFIGDIR
                             ERROR_QUIET
                             OUTPUT_STRIP_TRAILING_WHITESPACE)
            if (_${_PYTHON_PREFIX}_RESULT)
              unset (_${_PYTHON_PREFIX}_CONFIG CACHE)
              continue()
            endif()
            string (FIND "${_${_PYTHON_PREFIX}_CONFIGDIR}" "${CMAKE_LIBRARY_ARCHITECTURE}" _${_PYTHON_PREFIX}_RESULT)
            if (_${_PYTHON_PREFIX}_RESULT EQUAL -1)
              unset (_${_PYTHON_PREFIX}_CONFIG CACHE)
              continue()
            endif()
          endif()

          if (_${_PYTHON_PREFIX}_CONFIG)
            break()
          endif()
        endforeach()
      endif()
    endif()
  endif()

  if ("LIBRARY" IN_LIST _${_PYTHON_PREFIX}_FIND_DEVELOPMENT_ARTIFACTS)
    if (NOT _${_PYTHON_PREFIX}_LIBRARY_RELEASE)
      if ((${_PYTHON_PREFIX}_Interpreter_FOUND AND NOT CMAKE_CROSSCOMPILING) OR _${_PYTHON_PREFIX}_CONFIG)
        # retrieve root install directory
        _python_get_config_var (_${_PYTHON_PREFIX}_PREFIX PREFIX)

        # enforce current ABI
        _python_get_config_var (_${_PYTHON_PREFIX}_ABIFLAGS ABIFLAGS)

        set (_${_PYTHON_PREFIX}_HINTS "${_${_PYTHON_PREFIX}_PREFIX}")

        # retrieve library
        ## compute some paths and artifact names
        if (_${_PYTHON_PREFIX}_CONFIG)
          string (REGEX REPLACE "^.+python([0-9.]+)[a-z]*-config" "\\1" _${_PYTHON_PREFIX}_VERSION "${_${_PYTHON_PREFIX}_CONFIG}")
        else()
          set (_${_PYTHON_PREFIX}_VERSION "${${_PYTHON_PREFIX}_VERSION_MAJOR}.${${_PYTHON_PREFIX}_VERSION_MINOR}")
        endif()
        _python_get_path_suffixes (_${_PYTHON_PREFIX}_PATH_SUFFIXES VERSION ${_${_PYTHON_PREFIX}_VERSION} LIBRARY)
        _python_get_names (_${_PYTHON_PREFIX}_LIB_NAMES VERSION ${_${_PYTHON_PREFIX}_VERSION} WIN32 POSIX LIBRARY)

        _python_get_config_var (_${_PYTHON_PREFIX}_CONFIGDIR CONFIGDIR)
        list (APPEND _${_PYTHON_PREFIX}_HINTS "${_${_PYTHON_PREFIX}_CONFIGDIR}")

        list (APPEND _${_PYTHON_PREFIX}_HINTS "${${_PYTHON_PREFIX}_ROOT_DIR}" ENV ${_PYTHON_PREFIX}_ROOT_DIR)

        find_library (_${_PYTHON_PREFIX}_LIBRARY_RELEASE
                      NAMES ${_${_PYTHON_PREFIX}_LIB_NAMES}
                      NAMES_PER_DIR
                      HINTS ${_${_PYTHON_PREFIX}_HINTS}
                      PATH_SUFFIXES ${_${_PYTHON_PREFIX}_PATH_SUFFIXES}
                      NO_SYSTEM_ENVIRONMENT_PATH
                      NO_CMAKE_SYSTEM_PATH)
      endif()

      # Rely on HINTS and standard paths if interpreter or config tool failed to locate artifacts
      if (NOT _${_PYTHON_PREFIX}_LIBRARY_RELEASE)
        set (_${_PYTHON_PREFIX}_HINTS "${${_PYTHON_PREFIX}_ROOT_DIR}" ENV ${_PYTHON_PREFIX}_ROOT_DIR)

        unset (_${_PYTHON_PREFIX}_VIRTUALENV_PATHS)
        if (_${_PYTHON_PREFIX}_FIND_VIRTUALENV MATCHES "^(FIRST|ONLY)$")
          set (_${_PYTHON_PREFIX}_VIRTUALENV_PATHS ENV VIRTUAL_ENV ENV CONDA_PREFIX)
        endif()

        if (_${_PYTHON_PREFIX}_FIND_STRATEGY STREQUAL "LOCATION")
          # library names
          _python_get_names (_${_PYTHON_PREFIX}_LIB_NAMES VERSION ${_${_PYTHON_PREFIX}_FIND_VERSIONS} WIN32 POSIX LIBRARY)
          _python_get_names (_${_PYTHON_PREFIX}_LIB_NAMES_DEBUG VERSION ${_${_PYTHON_PREFIX}_FIND_VERSIONS} WIN32 DEBUG)
          # Paths suffixes
          _python_get_path_suffixes (_${_PYTHON_PREFIX}_PATH_SUFFIXES VERSION ${_${_PYTHON_PREFIX}_FIND_VERSIONS} LIBRARY)

          # Framework Paths
          _python_get_frameworks (_${_PYTHON_PREFIX}_FRAMEWORK_PATHS VERSION ${_${_PYTHON_PREFIX}_LIB_FIND_VERSIONS})
          # Registry Paths
          _python_get_registries (_${_PYTHON_PREFIX}_REGISTRY_PATHS VERSION ${_${_PYTHON_PREFIX}_FIND_VERSIONS} )

          if (APPLE AND _${_PYTHON_PREFIX}_FIND_FRAMEWORK STREQUAL "FIRST")
            find_library (_${_PYTHON_PREFIX}_LIBRARY_RELEASE
                          NAMES ${_${_PYTHON_PREFIX}_LIB_NAMES}
                          NAMES_PER_DIR
                          HINTS ${_${_PYTHON_PREFIX}_HINTS}
                          PATHS ${_${_PYTHON_PREFIX}_VIRTUALENV_PATHS}
                                ${_${_PYTHON_PREFIX}_FRAMEWORK_PATHS}
                          PATH_SUFFIXES ${_${_PYTHON_PREFIX}_PATH_SUFFIXES}
                          NO_CMAKE_PATH
                          NO_CMAKE_ENVIRONMENT_PATH
                          NO_SYSTEM_ENVIRONMENT_PATH
                          NO_CMAKE_SYSTEM_PATH)
          endif()

          if (WIN32 AND _${_PYTHON_PREFIX}_FIND_REGISTRY STREQUAL "FIRST")
            find_library (_${_PYTHON_PREFIX}_LIBRARY_RELEASE
                          NAMES ${_${_PYTHON_PREFIX}_LIB_NAMES}
                          NAMES_PER_DIR
                          HINTS ${_${_PYTHON_PREFIX}_HINTS}
                          PATHS ${_${_PYTHON_PREFIX}_VIRTUALENV_PATHS}
                                ${_${_PYTHON_PREFIX}_REGISTRY_PATHS}
                          PATH_SUFFIXES ${_${_PYTHON_PREFIX}_PATH_SUFFIXES}
                          NO_SYSTEM_ENVIRONMENT_PATH
                          NO_CMAKE_SYSTEM_PATH)
          endif()

          # search in HINTS locations
          find_library (_${_PYTHON_PREFIX}_LIBRARY_RELEASE
                        NAMES ${_${_PYTHON_PREFIX}_LIB_NAMES}
                        NAMES_PER_DIR
                        HINTS ${_${_PYTHON_PREFIX}_HINTS}
                        PATHS ${_${_PYTHON_PREFIX}_VIRTUALENV_PATHS}
                        PATH_SUFFIXES ${_${_PYTHON_PREFIX}_PATH_SUFFIXES}
                        NO_SYSTEM_ENVIRONMENT_PATH
                        NO_CMAKE_SYSTEM_PATH)

          if (APPLE AND _${_PYTHON_PREFIX}_FIND_FRAMEWORK STREQUAL "LAST")
            set (__${_PYTHON_PREFIX}_FRAMEWORK_PATHS ${_${_PYTHON_PREFIX}_FRAMEWORK_PATHS})
          else()
            unset (__${_PYTHON_PREFIX}_FRAMEWORK_PATHS)
          endif()

          if (WIN32 AND _${_PYTHON_PREFIX}_FIND_REGISTRY STREQUAL "LAST")
            set (__${_PYTHON_PREFIX}_REGISTRY_PATHS ${_${_PYTHON_PREFIX}_REGISTRY_PATHS})
          else()
            unset (__${_PYTHON_PREFIX}_REGISTRY_PATHS)
          endif()

          # search in all default paths
          find_library (_${_PYTHON_PREFIX}_LIBRARY_RELEASE
                        NAMES ${_${_PYTHON_PREFIX}_LIB_NAMES}
                        NAMES_PER_DIR
                        PATHS ${__${_PYTHON_PREFIX}_FRAMEWORK_PATHS}
                              ${__${_PYTHON_PREFIX}_REGISTRY_PATHS}
                        PATH_SUFFIXES ${_${_PYTHON_PREFIX}_PATH_SUFFIXES})
        else()
          foreach (_${_PYTHON_PREFIX}_LIB_VERSION IN LISTS _${_PYTHON_PREFIX}_FIND_VERSIONS)
            _python_get_names (_${_PYTHON_PREFIX}_LIB_NAMES VERSION ${_${_PYTHON_PREFIX}_LIB_VERSION} WIN32 POSIX LIBRARY)
            _python_get_names (_${_PYTHON_PREFIX}_LIB_NAMES_DEBUG VERSION ${_${_PYTHON_PREFIX}_LIB_VERSION} WIN32 DEBUG)

            _python_get_frameworks (_${_PYTHON_PREFIX}_FRAMEWORK_PATHS VERSION ${_${_PYTHON_PREFIX}_LIB_VERSION})
            _python_get_registries (_${_PYTHON_PREFIX}_REGISTRY_PATHS VERSION ${_${_PYTHON_PREFIX}_LIB_VERSION})

            _python_get_path_suffixes (_${_PYTHON_PREFIX}_PATH_SUFFIXES VERSION ${_${_PYTHON_PREFIX}_LIB_VERSION} LIBRARY)

            if (APPLE AND _${_PYTHON_PREFIX}_FIND_FRAMEWORK STREQUAL "FIRST")
              find_library (_${_PYTHON_PREFIX}_LIBRARY_RELEASE
                            NAMES ${_${_PYTHON_PREFIX}_LIB_NAMES}
                            NAMES_PER_DIR
                            HINTS ${_${_PYTHON_PREFIX}_HINTS}
                            PATHS ${_${_PYTHON_PREFIX}_VIRTUALENV_PATHS}
                                  ${_${_PYTHON_PREFIX}_FRAMEWORK_PATHS}
                            PATH_SUFFIXES ${_${_PYTHON_PREFIX}_PATH_SUFFIXES}
                            NO_CMAKE_PATH
                            NO_CMAKE_ENVIRONMENT_PATH
                            NO_SYSTEM_ENVIRONMENT_PATH
                            NO_CMAKE_SYSTEM_PATH)
            endif()

            if (WIN32 AND _${_PYTHON_PREFIX}_FIND_REGISTRY STREQUAL "FIRST")
              find_library (_${_PYTHON_PREFIX}_LIBRARY_RELEASE
                            NAMES ${_${_PYTHON_PREFIX}_LIB_NAMES}
                            NAMES_PER_DIR
                            HINTS ${_${_PYTHON_PREFIX}_HINTS}
                            PATHS ${_${_PYTHON_PREFIX}_VIRTUALENV_PATHS}
                                  ${_${_PYTHON_PREFIX}_REGISTRY_PATHS}
                            PATH_SUFFIXES ${_${_PYTHON_PREFIX}_PATH_SUFFIXES}
                            NO_SYSTEM_ENVIRONMENT_PATH
                            NO_CMAKE_SYSTEM_PATH)
            endif()

            # search in HINTS locations
            find_library (_${_PYTHON_PREFIX}_LIBRARY_RELEASE
                          NAMES ${_${_PYTHON_PREFIX}_LIB_NAMES}
                          NAMES_PER_DIR
                          HINTS ${_${_PYTHON_PREFIX}_HINTS}
                          PATHS ${_${_PYTHON_PREFIX}_VIRTUALENV_PATHS}
                          PATH_SUFFIXES ${_${_PYTHON_PREFIX}_PATH_SUFFIXES}
                          NO_SYSTEM_ENVIRONMENT_PATH
                          NO_CMAKE_SYSTEM_PATH)

            if (APPLE AND _${_PYTHON_PREFIX}_FIND_FRAMEWORK STREQUAL "LAST")
              set (__${_PYTHON_PREFIX}_FRAMEWORK_PATHS ${_${_PYTHON_PREFIX}_FRAMEWORK_PATHS})
            else()
              unset (__${_PYTHON_PREFIX}_FRAMEWORK_PATHS)
            endif()

            if (WIN32 AND _${_PYTHON_PREFIX}_FIND_REGISTRY STREQUAL "LAST")
              set (__${_PYTHON_PREFIX}_REGISTRY_PATHS ${_${_PYTHON_PREFIX}_REGISTRY_PATHS})
            else()
              unset (__${_PYTHON_PREFIX}_REGISTRY_PATHS)
            endif()

            # search in all default paths
            find_library (_${_PYTHON_PREFIX}_LIBRARY_RELEASE
                          NAMES ${_${_PYTHON_PREFIX}_LIB_NAMES}
                          NAMES_PER_DIR
                          PATHS ${__${_PYTHON_PREFIX}_FRAMEWORK_PATHS}
                                ${__${_PYTHON_PREFIX}_REGISTRY_PATHS}
                          PATH_SUFFIXES ${_${_PYTHON_PREFIX}_PATH_SUFFIXES})

            if (_${_PYTHON_PREFIX}_LIBRARY_RELEASE)
              break()
            endif()
          endforeach()
        endif()
      endif()
    endif()

    # finalize library version information
    _python_get_version (LIBRARY PREFIX _${_PYTHON_PREFIX}_)
    if (_${_PYTHON_PREFIX}_VERSION EQUAL "${_${_PYTHON_PREFIX}_REQUIRED_VERSION_MAJOR}")
      # not able to extract full version from library name
      if (${_PYTHON_PREFIX}_Interpreter_FOUND)
        # update from interpreter
        set (_${_PYTHON_PREFIX}_VERSION ${${_PYTHON_PREFIX}_VERSION})
        set (_${_PYTHON_PREFIX}_VERSION_MAJOR ${${_PYTHON_PREFIX}_VERSION_MAJOR})
        set (_${_PYTHON_PREFIX}_VERSION_MINOR ${${_PYTHON_PREFIX}_VERSION_MINOR})
        set (_${_PYTHON_PREFIX}_VERSION_PATCH ${${_PYTHON_PREFIX}_VERSION_PATCH})
      endif()
    endif()

    set (${_PYTHON_PREFIX}_LIBRARY_RELEASE "${_${_PYTHON_PREFIX}_LIBRARY_RELEASE}")

    if (_${_PYTHON_PREFIX}_LIBRARY_RELEASE AND NOT EXISTS "${_${_PYTHON_PREFIX}_LIBRARY_RELEASE}")
      set (_${_PYTHON_PREFIX}_Development_REASON_FAILURE "Cannot find the library \"${_${_PYTHON_PREFIX}_LIBRARY_RELEASE}\"")
      set_property (CACHE _${_PYTHON_PREFIX}_LIBRARY_RELEASE PROPERTY VALUE "${_PYTHON_PREFIX}_LIBRARY_RELEASE-NOTFOUND")
    endif()

    set (_${_PYTHON_PREFIX}_HINTS "${${_PYTHON_PREFIX}_ROOT_DIR}" ENV ${_PYTHON_PREFIX}_ROOT_DIR)

    if (WIN32 AND _${_PYTHON_PREFIX}_LIBRARY_RELEASE)
      # search for debug library
      # use release library location as a hint
      _python_get_names (_${_PYTHON_PREFIX}_LIB_NAMES_DEBUG VERSION ${_${_PYTHON_PREFIX}_VERSION} WIN32 DEBUG)
      get_filename_component (_${_PYTHON_PREFIX}_PATH "${_${_PYTHON_PREFIX}_LIBRARY_RELEASE}" DIRECTORY)
      find_library (_${_PYTHON_PREFIX}_LIBRARY_DEBUG
                    NAMES ${_${_PYTHON_PREFIX}_LIB_NAMES_DEBUG}
                    NAMES_PER_DIR
                    HINTS "${_${_PYTHON_PREFIX}_PATH}" ${_${_PYTHON_PREFIX}_HINTS}
                    NO_DEFAULT_PATH)
      # second try including CMAKE variables to catch-up non conventional layouts
      find_library (_${_PYTHON_PREFIX}_LIBRARY_DEBUG
                    NAMES ${_${_PYTHON_PREFIX}_LIB_NAMES_DEBUG}
                    NAMES_PER_DIR
                    NO_SYSTEM_ENVIRONMENT_PATH
                    NO_CMAKE_SYSTEM_PATH)
    endif()

    # retrieve runtime libraries
    if (_${_PYTHON_PREFIX}_LIBRARY_RELEASE)
      _python_get_names (_${_PYTHON_PREFIX}_LIB_NAMES VERSION ${_${_PYTHON_PREFIX}_VERSION} WIN32 POSIX LIBRARY)
      get_filename_component (_${_PYTHON_PREFIX}_PATH "${_${_PYTHON_PREFIX}_LIBRARY_RELEASE}" DIRECTORY)
      get_filename_component (_${_PYTHON_PREFIX}_PATH2 "${_${_PYTHON_PREFIX}_PATH}" DIRECTORY)
      _python_find_runtime_library (_${_PYTHON_PREFIX}_RUNTIME_LIBRARY_RELEASE
                                    NAMES ${_${_PYTHON_PREFIX}_LIB_NAMES}
                                    NAMES_PER_DIR
                                    HINTS "${_${_PYTHON_PREFIX}_PATH}"
                                          "${_${_PYTHON_PREFIX}_PATH2}" ${_${_PYTHON_PREFIX}_HINTS}
                                    PATH_SUFFIXES bin)
    endif()
    if (_${_PYTHON_PREFIX}_LIBRARY_DEBUG)
      _python_get_names (_${_PYTHON_PREFIX}_LIB_NAMES_DEBUG VERSION ${_${_PYTHON_PREFIX}_VERSION} WIN32 DEBUG)
      get_filename_component (_${_PYTHON_PREFIX}_PATH "${_${_PYTHON_PREFIX}_LIBRARY_DEBUG}" DIRECTORY)
      get_filename_component (_${_PYTHON_PREFIX}_PATH2 "${_${_PYTHON_PREFIX}_PATH}" DIRECTORY)
      _python_find_runtime_library (_${_PYTHON_PREFIX}_RUNTIME_LIBRARY_DEBUG
                                    NAMES ${_${_PYTHON_PREFIX}_LIB_NAMES_DEBUG}
                                    NAMES_PER_DIR
                                    HINTS "${_${_PYTHON_PREFIX}_PATH}"
                                          "${_${_PYTHON_PREFIX}_PATH2}" ${_${_PYTHON_PREFIX}_HINTS}
                                    PATH_SUFFIXES bin)
    endif()
  endif()

  if ("INCLUDE_DIR" IN_LIST _${_PYTHON_PREFIX}_FIND_DEVELOPMENT_ARTIFACTS)
    while (NOT _${_PYTHON_PREFIX}_INCLUDE_DIR)
      if ("LIBRARY" IN_LIST _${_PYTHON_PREFIX}_FIND_DEVELOPMENT_ARTIFACTS
          AND NOT _${_PYTHON_PREFIX}_LIBRARY_RELEASE)
        # Don't search for include dir if no library was founded
        break()
      endif()

      if ((${_PYTHON_PREFIX}_Interpreter_FOUND AND NOT CMAKE_CROSSCOMPILING) OR _${_PYTHON_PREFIX}_CONFIG)
        _python_get_config_var (_${_PYTHON_PREFIX}_INCLUDE_DIRS INCLUDES)

        find_path (_${_PYTHON_PREFIX}_INCLUDE_DIR
                   NAMES ${_${_PYTHON_PREFIX}_INCLUDE_NAMES}
                   HINTS ${_${_PYTHON_PREFIX}_INCLUDE_DIRS}
                   NO_SYSTEM_ENVIRONMENT_PATH
                   NO_CMAKE_SYSTEM_PATH)
      endif()

      # Rely on HINTS and standard paths if interpreter or config tool failed to locate artifacts
      if (NOT _${_PYTHON_PREFIX}_INCLUDE_DIR)
        unset (_${_PYTHON_PREFIX}_VIRTUALENV_PATHS)
        if (_${_PYTHON_PREFIX}_FIND_VIRTUALENV MATCHES "^(FIRST|ONLY)$")
          set (_${_PYTHON_PREFIX}_VIRTUALENV_PATHS ENV VIRTUAL_ENV ENV CONDA_PREFIX)
        endif()
        unset (_${_PYTHON_PREFIX}_INCLUDE_HINTS)

        if (_${_PYTHON_PREFIX}_LIBRARY_RELEASE)
          # Use the library's install prefix as a hint
          if (_${_PYTHON_PREFIX}_LIBRARY_RELEASE MATCHES "^(.+/Frameworks/Python.framework/Versions/[0-9.]+)")
            list (APPEND _${_PYTHON_PREFIX}_INCLUDE_HINTS "${CMAKE_MATCH_1}")
          elseif (_${_PYTHON_PREFIX}_LIBRARY_RELEASE MATCHES "^(.+)/lib(64|32)?/python[0-9.]+/config")
            list (APPEND _${_PYTHON_PREFIX}_INCLUDE_HINTS "${CMAKE_MATCH_1}")
          elseif (DEFINED CMAKE_LIBRARY_ARCHITECTURE AND ${_${_PYTHON_PREFIX}_LIBRARY_RELEASE} MATCHES "^(.+)/lib/${CMAKE_LIBRARY_ARCHITECTURE}")
            list (APPEND _${_PYTHON_PREFIX}_INCLUDE_HINTS "${CMAKE_MATCH_1}")
          else()
            # assume library is in a directory under root
            get_filename_component (_${_PYTHON_PREFIX}_PREFIX "${_${_PYTHON_PREFIX}_LIBRARY_RELEASE}" DIRECTORY)
            get_filename_component (_${_PYTHON_PREFIX}_PREFIX "${_${_PYTHON_PREFIX}_PREFIX}" DIRECTORY)
            list (APPEND _${_PYTHON_PREFIX}_INCLUDE_HINTS "${_${_PYTHON_PREFIX}_PREFIX}")
          endif()
        endif()

        _python_get_frameworks (_${_PYTHON_PREFIX}_FRAMEWORK_PATHS VERSION ${_${_PYTHON_PREFIX}_VERSION})
        _python_get_registries (_${_PYTHON_PREFIX}_REGISTRY_PATHS VERSION ${_${_PYTHON_PREFIX}_VERSION})
        _python_get_path_suffixes (_${_PYTHON_PREFIX}_PATH_SUFFIXES VERSION ${_${_PYTHON_PREFIX}_VERSION} INCLUDE)

        if (APPLE AND _${_PYTHON_PREFIX}_FIND_FRAMEWORK STREQUAL "FIRST")
          find_path (_${_PYTHON_PREFIX}_INCLUDE_DIR
                     NAMES ${_${_PYTHON_PREFIX}_INCLUDE_NAMES}
                     HINTS ${_${_PYTHON_PREFIX}_INCLUDE_HINTS} ${_${_PYTHON_PREFIX}_HINTS}
                     PATHS ${_${_PYTHON_PREFIX}_VIRTUALENV_PATHS}
                           ${_${_PYTHON_PREFIX}_FRAMEWORK_PATHS}
                     PATH_SUFFIXES ${_${_PYTHON_PREFIX}_PATH_SUFFIXES}
                     NO_CMAKE_PATH
                     NO_CMAKE_ENVIRONMENT_PATH
                     NO_SYSTEM_ENVIRONMENT_PATH
                     NO_CMAKE_SYSTEM_PATH)
        endif()

        if (WIN32 AND _${_PYTHON_PREFIX}_FIND_REGISTRY STREQUAL "FIRST")
          find_path (_${_PYTHON_PREFIX}_INCLUDE_DIR
                     NAMES ${_${_PYTHON_PREFIX}_INCLUDE_NAMES}
                     HINTS ${_${_PYTHON_PREFIX}_INCLUDE_HINTS} ${_${_PYTHON_PREFIX}_HINTS}
                     PATHS ${_${_PYTHON_PREFIX}_VIRTUALENV_PATHS}
                           ${_${_PYTHON_PREFIX}_REGISTRY_PATHS}
                     PATH_SUFFIXES ${_${_PYTHON_PREFIX}_PATH_SUFFIXES}
                     NO_SYSTEM_ENVIRONMENT_PATH
                     NO_CMAKE_SYSTEM_PATH)
        endif()

        if (APPLE AND _${_PYTHON_PREFIX}_FIND_FRAMEWORK STREQUAL "LAST")
          set (__${_PYTHON_PREFIX}_FRAMEWORK_PATHS ${_${_PYTHON_PREFIX}_FRAMEWORK_PATHS})
        else()
          unset (__${_PYTHON_PREFIX}_FRAMEWORK_PATHS)
        endif()

        if (WIN32 AND _${_PYTHON_PREFIX}_FIND_REGISTRY STREQUAL "LAST")
          set (__${_PYTHON_PREFIX}_REGISTRY_PATHS ${_${_PYTHON_PREFIX}_REGISTRY_PATHS})
        else()
          unset (__${_PYTHON_PREFIX}_REGISTRY_PATHS)
        endif()

        find_path (_${_PYTHON_PREFIX}_INCLUDE_DIR
                   NAMES ${_${_PYTHON_PREFIX}_INCLUDE_NAMES}
                   HINTS ${_${_PYTHON_PREFIX}_INCLUDE_HINTS} ${_${_PYTHON_PREFIX}_HINTS}
                   PATHS ${_${_PYTHON_PREFIX}_VIRTUALENV_PATHS}
                         ${__${_PYTHON_PREFIX}_FRAMEWORK_PATHS}
                         ${__${_PYTHON_PREFIX}_REGISTRY_PATHS}
                   PATH_SUFFIXES ${_${_PYTHON_PREFIX}_PATH_SUFFIXES}
                   NO_SYSTEM_ENVIRONMENT_PATH
                   NO_CMAKE_SYSTEM_PATH)
      endif()

      # search header file in standard locations
      find_path (_${_PYTHON_PREFIX}_INCLUDE_DIR
                 NAMES ${_${_PYTHON_PREFIX}_INCLUDE_NAMES})

      break()
    endwhile()

    set (${_PYTHON_PREFIX}_INCLUDE_DIRS "${_${_PYTHON_PREFIX}_INCLUDE_DIR}")

    if (_${_PYTHON_PREFIX}_INCLUDE_DIR AND NOT EXISTS "${_${_PYTHON_PREFIX}_INCLUDE_DIR}")
      set (_${_PYTHON_PREFIX}_Development_REASON_FAILURE "Cannot find the directory \"${_${_PYTHON_PREFIX}_INCLUDE_DIR}\"")
      set_property (CACHE _${_PYTHON_PREFIX}_INCLUDE_DIR PROPERTY VALUE "${_PYTHON_PREFIX}_INCLUDE_DIR-NOTFOUND")
    endif()

    if (_${_PYTHON_PREFIX}_INCLUDE_DIR)
      # retrieve version from header file
      _python_get_version (INCLUDE PREFIX _${_PYTHON_PREFIX}_INC_)
      if (_${_PYTHON_PREFIX}_LIBRARY_RELEASE)
        if ("${_${_PYTHON_PREFIX}_INC_VERSION_MAJOR}.${_${_PYTHON_PREFIX}_INC_VERSION_MINOR}"
            VERSION_EQUAL _${_PYTHON_PREFIX}_VERSION)
          # update versioning
          set (_${_PYTHON_PREFIX}_VERSION ${_${_PYTHON_PREFIX}_INC_VERSION})
          set (_${_PYTHON_PREFIX}_VERSION_PATCH ${_${_PYTHON_PREFIX}_INC_VERSION_PATCH})
        endif()
      else()
        set (_${_PYTHON_PREFIX}_VERSION ${_${_PYTHON_PREFIX}_INC_VERSION})
        set (_${_PYTHON_PREFIX}_VERSION_MAJOR ${_${_PYTHON_PREFIX}_INC_VERSION_MAJOR})
        set (_${_PYTHON_PREFIX}_VERSION_MINOR ${_${_PYTHON_PREFIX}_INC_VERSION_MINOR})
        set (_${_PYTHON_PREFIX}_VERSION_PATCH ${_${_PYTHON_PREFIX}_INC_VERSION_PATCH})
      endif()
    endif()
  endif()

  if (NOT ${_PYTHON_PREFIX}_Interpreter_FOUND AND NOT ${_PYTHON_PREFIX}_Compiler_FOUND)
    # set public version information
    set (${_PYTHON_PREFIX}_VERSION ${_${_PYTHON_PREFIX}_VERSION})
    set (${_PYTHON_PREFIX}_VERSION_MAJOR ${_${_PYTHON_PREFIX}_VERSION_MAJOR})
    set (${_PYTHON_PREFIX}_VERSION_MINOR ${_${_PYTHON_PREFIX}_VERSION_MINOR})
    set (${_PYTHON_PREFIX}_VERSION_PATCH ${_${_PYTHON_PREFIX}_VERSION_PATCH})
  endif()

  # define public variables
  if ("LIBRARY" IN_LIST _${_PYTHON_PREFIX}_FIND_DEVELOPMENT_ARTIFACTS)
    set (${_PYTHON_PREFIX}_LIBRARY_DEBUG "${_${_PYTHON_PREFIX}_LIBRARY_DEBUG}")
    _python_select_library_configurations (${_PYTHON_PREFIX})

    set (${_PYTHON_PREFIX}_RUNTIME_LIBRARY_RELEASE "${_${_PYTHON_PREFIX}_RUNTIME_LIBRARY_RELEASE}")
    set (${_PYTHON_PREFIX}_RUNTIME_LIBRARY_DEBUG "${_${_PYTHON_PREFIX}_RUNTIME_LIBRARY_DEBUG}")

    if (_${_PYTHON_PREFIX}_RUNTIME_LIBRARY_RELEASE)
      set (${_PYTHON_PREFIX}_RUNTIME_LIBRARY "${_${_PYTHON_PREFIX}_RUNTIME_LIBRARY_RELEASE}")
    elseif (_${_PYTHON_PREFIX}_RUNTIME_LIBRARY_DEBUG)
      set (${_PYTHON_PREFIX}_RUNTIME_LIBRARY "${_${_PYTHON_PREFIX}_RUNTIME_LIBRARY_DEBUG}")
    else()
      set (${_PYTHON_PREFIX}_RUNTIME_LIBRARY "${_PYTHON_PREFIX}_RUNTIME_LIBRARY-NOTFOUND")
    endif()

    _python_set_library_dirs (${_PYTHON_PREFIX}_LIBRARY_DIRS
                              _${_PYTHON_PREFIX}_LIBRARY_RELEASE
                              _${_PYTHON_PREFIX}_LIBRARY_DEBUG)
    if (UNIX)
      if (_${_PYTHON_PREFIX}_LIBRARY_RELEASE MATCHES "${CMAKE_SHARED_LIBRARY_SUFFIX}$")
        set (${_PYTHON_PREFIX}_RUNTIME_LIBRARY_DIRS ${${_PYTHON_PREFIX}_LIBRARY_DIRS})
      endif()
    else()
      _python_set_library_dirs (${_PYTHON_PREFIX}_RUNTIME_LIBRARY_DIRS
                                _${_PYTHON_PREFIX}_RUNTIME_LIBRARY_RELEASE
                                _${_PYTHON_PREFIX}_RUNTIME_LIBRARY_DEBUG)
    endif()
  endif()

  if (_${_PYTHON_PREFIX}_LIBRARY_RELEASE OR _${_PYTHON_PREFIX}_INCLUDE_DIR)
    if (${_PYTHON_PREFIX}_Interpreter_FOUND OR ${_PYTHON_PREFIX}_Compiler_FOUND)
      # development environment must be compatible with interpreter/compiler
      if ("${_${_PYTHON_PREFIX}_VERSION_MAJOR}.${_${_PYTHON_PREFIX}_VERSION_MINOR}" VERSION_EQUAL "${${_PYTHON_PREFIX}_VERSION_MAJOR}.${${_PYTHON_PREFIX}_VERSION_MINOR}"
          AND "${_${_PYTHON_PREFIX}_INC_VERSION_MAJOR}.${_${_PYTHON_PREFIX}_INC_VERSION_MINOR}" VERSION_EQUAL "${_${_PYTHON_PREFIX}_VERSION_MAJOR}.${_${_PYTHON_PREFIX}_VERSION_MINOR}")
        _python_set_development_module_found (Module)
        _python_set_development_module_found (Embed)
      endif()
    elseif (${_PYTHON_PREFIX}_VERSION_MAJOR VERSION_EQUAL _${_PYTHON_PREFIX}_REQUIRED_VERSION_MAJOR
        AND "${_${_PYTHON_PREFIX}_INC_VERSION_MAJOR}.${_${_PYTHON_PREFIX}_INC_VERSION_MINOR}" VERSION_EQUAL "${_${_PYTHON_PREFIX}_VERSION_MAJOR}.${_${_PYTHON_PREFIX}_VERSION_MINOR}")
      _python_set_development_module_found (Module)
      _python_set_development_module_found (Embed)
    endif()
    if (DEFINED _${_PYTHON_PREFIX}_FIND_ABI AND
        (NOT _${_PYTHON_PREFIX}_ABI IN_LIST _${_PYTHON_PREFIX}_ABIFLAGS
          OR NOT _${_PYTHON_PREFIX}_INC_ABI IN_LIST _${_PYTHON_PREFIX}_ABIFLAGS))
      set (${_PYTHON_PREFIX}_Development.Module_FOUND FALSE)
      set (${_PYTHON_PREFIX}_Development.Embed_FOUND FALSE)
    endif()
  endif()

  if (( ${_PYTHON_PREFIX}_Development.Module_FOUND
        AND ${_PYTHON_PREFIX}_Development.Embed_FOUND)
      OR (NOT "Development.Module" IN_LIST ${_PYTHON_PREFIX}_FIND_COMPONENTS
        AND ${_PYTHON_PREFIX}_Development.Embed_FOUND)
      OR (NOT "Development.Embed" IN_LIST ${_PYTHON_PREFIX}_FIND_COMPONENTS
        AND ${_PYTHON_PREFIX}_Development.Module_FOUND))
    unset (_${_PYTHON_PREFIX}_Development_REASON_FAILURE)
  endif()

  if ("Development" IN_LIST ${_PYTHON_PREFIX}_FIND_COMPONENTS
      AND ${_PYTHON_PREFIX}_Development.Module_FOUND
      AND ${_PYTHON_PREFIX}_Development.Embed_FOUND)
    set (${_PYTHON_PREFIX}_Development_FOUND TRUE)
  endif()

  if ((${_PYTHON_PREFIX}_Development.Module_FOUND
      OR ${_PYTHON_PREFIX}_Development.Embed_FOUND)
    AND EXISTS "${_${_PYTHON_PREFIX}_INCLUDE_DIR}/PyPy.h")
  # retrieve PyPy version
  file (STRINGS "${_${_PYTHON_PREFIX}_INCLUDE_DIR}/patchlevel.h" ${_PYTHON_PREFIX}_PyPy_VERSION
                REGEX "^#define[ \t]+PYPY_VERSION[ \t]+\"[^\"]+\"")
  string (REGEX REPLACE "^#define[ \t]+PYPY_VERSION[ \t]+\"([^\"]+)\".*" "\\1"
                ${_PYTHON_PREFIX}_PyPy_VERSION "${${_PYTHON_PREFIX}_PyPy_VERSION}")
  endif()

  unset(${_PYTHON_PREFIX}_LINK_OPTIONS)
  if (${_PYTHON_PREFIX}_Development.Embed_FOUND AND APPLE
      AND ${_PYTHON_PREFIX}_LIBRARY_RELEASE MATCHES "${CMAKE_SHARED_LIBRARY_SUFFIX}$")
    # rpath must be specified if python is part of a framework
    unset(_${_PYTHON_PREFIX}_is_prefix)
    foreach (_${_PYTHON_PREFIX}_implementation IN LISTS _${_PYTHON_PREFIX}_FIND_IMPLEMENTATIONS)
      foreach (_${_PYTHON_PREFIX}_framework IN LISTS _${_PYTHON_PREFIX}_${_${_PYTHON_PREFIX}_implementation}_FRAMEWORKS)
        if (${_PYTHON_PREFIX}_LIBRARY_RELEASE MATCHES "^${_${_PYTHON_PREFIX}_framework}")
          get_filename_component (_${_PYTHON_PREFIX}_framework "${_${_PYTHON_PREFIX}_framework}" DIRECTORY)
          set (${_PYTHON_PREFIX}_LINK_OPTIONS "LINKER:-rpath,${_${_PYTHON_PREFIX}_framework}")
          break()
        endif()
      endforeach()
      if (_${_PYTHON_PREFIX}_is_prefix)
        break()
      endif()
    endforeach()
    unset(_${_PYTHON_PREFIX}_implementation)
    unset(_${_PYTHON_PREFIX}_framework)
    unset(_${_PYTHON_PREFIX}_is_prefix)
  endif()

  if (NOT DEFINED ${_PYTHON_PREFIX}_SOABI)
    _python_get_config_var (${_PYTHON_PREFIX}_SOABI SOABI)
  endif()

  _python_compute_development_signature (Module)
  _python_compute_development_signature (Embed)

  # Restore the original find library ordering
  if (DEFINED _${_PYTHON_PREFIX}_CMAKE_FIND_LIBRARY_SUFFIXES)
    set (CMAKE_FIND_LIBRARY_SUFFIXES ${_${_PYTHON_PREFIX}_CMAKE_FIND_LIBRARY_SUFFIXES})
  endif()

  if (${_PYTHON_PREFIX}_ARTIFACTS_INTERACTIVE)
    if ("LIBRARY" IN_LIST _${_PYTHON_PREFIX}_FIND_DEVELOPMENT_ARTIFACTS)
      set (${_PYTHON_PREFIX}_LIBRARY "${_${_PYTHON_PREFIX}_LIBRARY_RELEASE}" CACHE FILEPATH "${_PYTHON_PREFIX} Library")
    endif()
    if ("INCLUDE_DIR" IN_LIST _${_PYTHON_PREFIX}_FIND_DEVELOPMENT_ARTIFACTS)
      set (${_PYTHON_PREFIX}_INCLUDE_DIR "${_${_PYTHON_PREFIX}_INCLUDE_DIR}" CACHE FILEPATH "${_PYTHON_PREFIX} Include Directory")
    endif()
  endif()

  _python_mark_as_internal (_${_PYTHON_PREFIX}_LIBRARY_RELEASE
                            _${_PYTHON_PREFIX}_LIBRARY_DEBUG
                            _${_PYTHON_PREFIX}_RUNTIME_LIBRARY_RELEASE
                            _${_PYTHON_PREFIX}_RUNTIME_LIBRARY_DEBUG
                            _${_PYTHON_PREFIX}_INCLUDE_DIR
                            _${_PYTHON_PREFIX}_CONFIG
                            _${_PYTHON_PREFIX}_DEVELOPMENT_MODULE_SIGNATURE
                            _${_PYTHON_PREFIX}_DEVELOPMENT_EMBED_SIGNATURE)
endif()

if (${_PYTHON_PREFIX}_FIND_REQUIRED_NumPy)
  list (APPEND _${_PYTHON_PREFIX}_REQUIRED_VARS ${_PYTHON_PREFIX}_NumPy_INCLUDE_DIRS)
endif()
if ("NumPy" IN_LIST ${_PYTHON_PREFIX}_FIND_COMPONENTS AND ${_PYTHON_PREFIX}_Interpreter_FOUND)
  list (APPEND _${_PYTHON_PREFIX}_CACHED_VARS _${_PYTHON_PREFIX}_NumPy_INCLUDE_DIR)

  if (DEFINED ${_PYTHON_PREFIX}_NumPy_INCLUDE_DIR
      AND IS_ABSOLUTE "${${_PYTHON_PREFIX}_NumPy_INCLUDE_DIR}")
    set (_${_PYTHON_PREFIX}_NumPy_INCLUDE_DIR "${${_PYTHON_PREFIX}_NumPy_INCLUDE_DIR}" CACHE INTERNAL "")
  elseif (DEFINED _${_PYTHON_PREFIX}_NumPy_INCLUDE_DIR)
    # compute numpy signature. Depends on interpreter and development signatures
    string (MD5 __${_PYTHON_PREFIX}_NUMPY_SIGNATURE "${_${_PYTHON_PREFIX}_INTERPRETER_SIGNATURE}:${_${_PYTHON_PREFIX}_DEVELOPMENT_MODULE_SIGNATURE}:${_${_PYTHON_PREFIX}_NumPy_INCLUDE_DIR}")
    if (NOT __${_PYTHON_PREFIX}_NUMPY_SIGNATURE STREQUAL _${_PYTHON_PREFIX}_NUMPY_SIGNATURE
        OR NOT EXISTS "${_${_PYTHON_PREFIX}_NumPy_INCLUDE_DIR}")
      unset (_${_PYTHON_PREFIX}_NumPy_INCLUDE_DIR CACHE)
      unset (_${_PYTHON_PREFIX}_NUMPY_SIGNATURE CACHE)
    endif()
  endif()

  if (NOT _${_PYTHON_PREFIX}_NumPy_INCLUDE_DIR)
    execute_process(COMMAND ${${_PYTHON_PREFIX}_INTERPRETER_LAUNCHER} "${_${_PYTHON_PREFIX}_EXECUTABLE}" -c
                            "import sys\ntry: import numpy; sys.stdout.write(numpy.get_include())\nexcept:pass\n"
                    RESULT_VARIABLE _${_PYTHON_PREFIX}_RESULT
                    OUTPUT_VARIABLE _${_PYTHON_PREFIX}_NumPy_PATH
                    ERROR_QUIET
                    OUTPUT_STRIP_TRAILING_WHITESPACE)

    if (NOT _${_PYTHON_PREFIX}_RESULT)
      find_path (_${_PYTHON_PREFIX}_NumPy_INCLUDE_DIR
                 NAMES "numpy/arrayobject.h" "numpy/numpyconfig.h"
                 HINTS "${_${_PYTHON_PREFIX}_NumPy_PATH}"
                 NO_DEFAULT_PATH)
    endif()
  endif()

  set (${_PYTHON_PREFIX}_NumPy_INCLUDE_DIRS "${_${_PYTHON_PREFIX}_NumPy_INCLUDE_DIR}")

  if(_${_PYTHON_PREFIX}_NumPy_INCLUDE_DIR AND NOT EXISTS "${_${_PYTHON_PREFIX}_NumPy_INCLUDE_DIR}")
    set (_${_PYTHON_PREFIX}_NumPy_REASON_FAILURE "Cannot find the directory \"${_${_PYTHON_PREFIX}_NumPy_INCLUDE_DIR}\"")
    set_property (CACHE _${_PYTHON_PREFIX}_NumPy_INCLUDE_DIR PROPERTY VALUE "${_PYTHON_PREFIX}_NumPy_INCLUDE_DIR-NOTFOUND")
  endif()

  if (_${_PYTHON_PREFIX}_NumPy_INCLUDE_DIR)
    execute_process (COMMAND ${${_PYTHON_PREFIX}_INTERPRETER_LAUNCHER} "${_${_PYTHON_PREFIX}_EXECUTABLE}" -c
                             "import sys\ntry: import numpy; sys.stdout.write(numpy.__version__)\nexcept:pass\n"
                     RESULT_VARIABLE _${_PYTHON_PREFIX}_RESULT
                     OUTPUT_VARIABLE _${_PYTHON_PREFIX}_NumPy_VERSION)
    if (NOT _${_PYTHON_PREFIX}_RESULT)
      set (${_PYTHON_PREFIX}_NumPy_VERSION "${_${_PYTHON_PREFIX}_NumPy_VERSION}")
    else()
      unset (${_PYTHON_PREFIX}_NumPy_VERSION)
    endif()

    # final step: set NumPy founded only if Development.Module component is founded as well
    set(${_PYTHON_PREFIX}_NumPy_FOUND ${${_PYTHON_PREFIX}_Development.Module_FOUND})
  else()
    set (${_PYTHON_PREFIX}_NumPy_FOUND FALSE)
  endif()

  if (${_PYTHON_PREFIX}_NumPy_FOUND)
    unset (_${_PYTHON_PREFIX}_NumPy_REASON_FAILURE)

    # compute and save numpy signature
    string (MD5 __${_PYTHON_PREFIX}_NUMPY_SIGNATURE "${_${_PYTHON_PREFIX}_INTERPRETER_SIGNATURE}:${_${_PYTHON_PREFIX}_DEVELOPMENT_MODULE_SIGNATURE}:${${_PYTHON_PREFIX}_NumPyINCLUDE_DIR}")
    set (_${_PYTHON_PREFIX}_NUMPY_SIGNATURE "${__${_PYTHON_PREFIX}_NUMPY_SIGNATURE}" CACHE INTERNAL "")
  else()
    unset (_${_PYTHON_PREFIX}_NUMPY_SIGNATURE CACHE)
  endif()

  if (${_PYTHON_PREFIX}_ARTIFACTS_INTERACTIVE)
    set (${_PYTHON_PREFIX}_NumPy_INCLUDE_DIR "${_${_PYTHON_PREFIX}_NumPy_INCLUDE_DIR}" CACHE FILEPATH "${_PYTHON_PREFIX} NumPy Include Directory")
  endif()

  _python_mark_as_internal (_${_PYTHON_PREFIX}_NumPy_INCLUDE_DIR
                            _${_PYTHON_PREFIX}_NUMPY_SIGNATURE)
endif()

# final validation
if (${_PYTHON_PREFIX}_VERSION_MAJOR AND
    NOT ${_PYTHON_PREFIX}_VERSION_MAJOR VERSION_EQUAL _${_PYTHON_PREFIX}_REQUIRED_VERSION_MAJOR)
  _python_display_failure ("Could NOT find ${_PYTHON_PREFIX}: Found unsuitable major version \"${${_PYTHON_PREFIX}_VERSION_MAJOR}\", but required major version is exact version \"${_${_PYTHON_PREFIX}_REQUIRED_VERSION_MAJOR}\"")

  cmake_policy(POP)
  return()
endif()

unset (_${_PYTHON_PREFIX}_REASON_FAILURE)
foreach (_${_PYTHON_PREFIX}_COMPONENT IN ITEMS Interpreter Compiler Development NumPy)
  if (_${_PYTHON_PREFIX}_${_${_PYTHON_PREFIX}_COMPONENT}_REASON_FAILURE)
    string (APPEND _${_PYTHON_PREFIX}_REASON_FAILURE "\n        ${_${_PYTHON_PREFIX}_COMPONENT}: ${_${_PYTHON_PREFIX}_${_${_PYTHON_PREFIX}_COMPONENT}_REASON_FAILURE}")
    unset (_${_PYTHON_PREFIX}_${_${_PYTHON_PREFIX}_COMPONENT}_REASON_FAILURE)
  endif()
endforeach()

find_package_handle_standard_args (${_PYTHON_PREFIX}
                                   REQUIRED_VARS ${_${_PYTHON_PREFIX}_REQUIRED_VARS}
                                   VERSION_VAR ${_PYTHON_PREFIX}_VERSION
                                   HANDLE_VERSION_RANGE
                                   HANDLE_COMPONENTS
                                   REASON_FAILURE_MESSAGE "${_${_PYTHON_PREFIX}_REASON_FAILURE}")

# Create imported targets and helper functions
if(_${_PYTHON_PREFIX}_CMAKE_ROLE STREQUAL "PROJECT")
  if ("Interpreter" IN_LIST ${_PYTHON_PREFIX}_FIND_COMPONENTS
      AND ${_PYTHON_PREFIX}_Interpreter_FOUND
      AND NOT TARGET ${_PYTHON_PREFIX}::Interpreter)
    add_executable (${_PYTHON_PREFIX}::Interpreter IMPORTED)
    set_property (TARGET ${_PYTHON_PREFIX}::Interpreter
                  PROPERTY IMPORTED_LOCATION "${${_PYTHON_PREFIX}_EXECUTABLE}")
  endif()

  if ("Compiler" IN_LIST ${_PYTHON_PREFIX}_FIND_COMPONENTS
      AND ${_PYTHON_PREFIX}_Compiler_FOUND
      AND NOT TARGET ${_PYTHON_PREFIX}::Compiler)
    add_executable (${_PYTHON_PREFIX}::Compiler IMPORTED)
    set_property (TARGET ${_PYTHON_PREFIX}::Compiler
                  PROPERTY IMPORTED_LOCATION "${${_PYTHON_PREFIX}_COMPILER}")
  endif()

  if (("Development.Module" IN_LIST ${_PYTHON_PREFIX}_FIND_COMPONENTS
        AND ${_PYTHON_PREFIX}_Development.Module_FOUND)
      OR ("Development.Embed" IN_LIST ${_PYTHON_PREFIX}_FIND_COMPONENTS
        AND ${_PYTHON_PREFIX}_Development.Embed_FOUND))

    macro (__PYTHON_IMPORT_LIBRARY __name)
      if (${_PYTHON_PREFIX}_LIBRARY_RELEASE MATCHES "${CMAKE_SHARED_LIBRARY_SUFFIX}$"
          OR ${_PYTHON_PREFIX}_RUNTIME_LIBRARY_RELEASE)
        set (_${_PYTHON_PREFIX}_LIBRARY_TYPE SHARED)
      else()
        set (_${_PYTHON_PREFIX}_LIBRARY_TYPE STATIC)
      endif()

      if (NOT TARGET ${__name})
        add_library (${__name} ${_${_PYTHON_PREFIX}_LIBRARY_TYPE} IMPORTED)
      endif()

      set_property (TARGET ${__name}
                    PROPERTY INTERFACE_INCLUDE_DIRECTORIES "${${_PYTHON_PREFIX}_INCLUDE_DIRS}")

      if (${_PYTHON_PREFIX}_LIBRARY_RELEASE AND ${_PYTHON_PREFIX}_RUNTIME_LIBRARY_RELEASE)
        # System manage shared libraries in two parts: import and runtime
        if (${_PYTHON_PREFIX}_LIBRARY_RELEASE AND ${_PYTHON_PREFIX}_LIBRARY_DEBUG)
          set_property (TARGET ${__name} PROPERTY IMPORTED_CONFIGURATIONS RELEASE DEBUG)
          set_target_properties (${__name}
                                 PROPERTIES IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "C"
                                            IMPORTED_IMPLIB_RELEASE "${${_PYTHON_PREFIX}_LIBRARY_RELEASE}"
                                            IMPORTED_LOCATION_RELEASE "${${_PYTHON_PREFIX}_RUNTIME_LIBRARY_RELEASE}")
          set_target_properties (${__name}
                                 PROPERTIES IMPORTED_LINK_INTERFACE_LANGUAGES_DEBUG "C"
                                            IMPORTED_IMPLIB_DEBUG "${${_PYTHON_PREFIX}_LIBRARY_DEBUG}"
                                            IMPORTED_LOCATION_DEBUG "${${_PYTHON_PREFIX}_RUNTIME_LIBRARY_DEBUG}")
        else()
          set_target_properties (${__name}
                                 PROPERTIES IMPORTED_LINK_INTERFACE_LANGUAGES "C"
                                            IMPORTED_IMPLIB "${${_PYTHON_PREFIX}_LIBRARIES}"
                                            IMPORTED_LOCATION "${${_PYTHON_PREFIX}_RUNTIME_LIBRARY_RELEASE}")
        endif()
      else()
        if (${_PYTHON_PREFIX}_LIBRARY_RELEASE AND ${_PYTHON_PREFIX}_LIBRARY_DEBUG)
          set_property (TARGET ${__name} PROPERTY IMPORTED_CONFIGURATIONS RELEASE DEBUG)
          set_target_properties (${__name}
                                 PROPERTIES IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "C"
                                            IMPORTED_LOCATION_RELEASE "${${_PYTHON_PREFIX}_LIBRARY_RELEASE}")
          set_target_properties (${__name}
                                 PROPERTIES IMPORTED_LINK_INTERFACE_LANGUAGES_DEBUG "C"
                                            IMPORTED_LOCATION_DEBUG "${${_PYTHON_PREFIX}_LIBRARY_DEBUG}")
        else()
          set_target_properties (${__name}
                                 PROPERTIES IMPORTED_LINK_INTERFACE_LANGUAGES "C"
                                            IMPORTED_LOCATION "${${_PYTHON_PREFIX}_LIBRARY_RELEASE}")
        endif()
      endif()

      if (_${_PYTHON_PREFIX}_LIBRARY_TYPE STREQUAL "STATIC")
        # extend link information with dependent libraries
        _python_get_config_var (_${_PYTHON_PREFIX}_LINK_LIBRARIES LIBS)
        if (_${_PYTHON_PREFIX}_LINK_LIBRARIES)
          set_property (TARGET ${__name}
                        PROPERTY INTERFACE_LINK_LIBRARIES ${_${_PYTHON_PREFIX}_LINK_LIBRARIES})
        endif()
      endif()

      if (${_PYTHON_PREFIX}_LINK_OPTIONS
          AND _${_PYTHON_PREFIX}_LIBRARY_TYPE STREQUAL "SHARED")
        set_property (TARGET ${__name} PROPERTY INTERFACE_LINK_OPTIONS "${${_PYTHON_PREFIX}_LINK_OPTIONS}")
      endif()
    endmacro()

    if (${_PYTHON_PREFIX}_Development.Embed_FOUND)
      __python_import_library (${_PYTHON_PREFIX}::Python)
    endif()

    if (${_PYTHON_PREFIX}_Development.Module_FOUND)
      if ("LIBRARY" IN_LIST _${_PYTHON_PREFIX}_FIND_DEVELOPMENT_MODULE_ARTIFACTS)
        # On Windows/CYGWIN/MSYS, Python::Module is the same as Python::Python
        # but ALIAS cannot be used because the imported library is not GLOBAL.
        __python_import_library (${_PYTHON_PREFIX}::Module)
      else()
        if (NOT TARGET ${_PYTHON_PREFIX}::Module)
          add_library (${_PYTHON_PREFIX}::Module INTERFACE IMPORTED)
        endif()
        set_property (TARGET ${_PYTHON_PREFIX}::Module
                      PROPERTY INTERFACE_INCLUDE_DIRECTORIES "${${_PYTHON_PREFIX}_INCLUDE_DIRS}")

        # When available, enforce shared library generation with undefined symbols
        if (APPLE)
          set_property (TARGET ${_PYTHON_PREFIX}::Module
                        PROPERTY INTERFACE_LINK_OPTIONS "LINKER:-undefined,dynamic_lookup")
        endif()
        if (CMAKE_SYSTEM_NAME STREQUAL "SunOS")
          set_property (TARGET ${_PYTHON_PREFIX}::Module
                        PROPERTY INTERFACE_LINK_OPTIONS "LINKER:-z,nodefs")
        endif()
        if (CMAKE_SYSTEM_NAME STREQUAL "AIX")
          set_property (TARGET ${_PYTHON_PREFIX}::Module
                        PROPERTY INTERFACE_LINK_OPTIONS "LINKER:-b,erok")
        endif()
      endif()
    endif()

    #
    # PYTHON_ADD_LIBRARY (<name> [STATIC|SHARED|MODULE] src1 src2 ... srcN)
    # It is used to build modules for python.
    #
    function (__${_PYTHON_PREFIX}_ADD_LIBRARY prefix name)
      cmake_parse_arguments (PARSE_ARGV 2 PYTHON_ADD_LIBRARY "STATIC;SHARED;MODULE;WITH_SOABI" "" "")

      if (PYTHON_ADD_LIBRARY_STATIC)
        set (type STATIC)
      elseif (PYTHON_ADD_LIBRARY_SHARED)
        set (type SHARED)
      else()
        set (type MODULE)
      endif()

      if (type STREQUAL "MODULE" AND NOT TARGET ${prefix}::Module)
        message (SEND_ERROR "${prefix}_ADD_LIBRARY: dependent target '${prefix}::Module' is not defined.\n   Did you miss to request COMPONENT 'Development.Module'?")
        return()
      endif()
      if (NOT type STREQUAL "MODULE" AND NOT TARGET ${prefix}::Python)
        message (SEND_ERROR "${prefix}_ADD_LIBRARY: dependent target '${prefix}::Python' is not defined.\n   Did you miss to request COMPONENT 'Development.Embed'?")
        return()
      endif()

      add_library (${name} ${type} ${PYTHON_ADD_LIBRARY_UNPARSED_ARGUMENTS})

      get_property (type TARGET ${name} PROPERTY TYPE)

      if (type STREQUAL "MODULE_LIBRARY")
        target_link_libraries (${name} PRIVATE ${prefix}::Module)
        # customize library name to follow module name rules
        set_property (TARGET ${name} PROPERTY PREFIX "")
        if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
          set_property (TARGET ${name} PROPERTY SUFFIX ".pyd")
        endif()

        if (PYTHON_ADD_LIBRARY_WITH_SOABI AND ${prefix}_SOABI)
          get_property (suffix TARGET ${name} PROPERTY SUFFIX)
          if (NOT suffix)
            set (suffix "${CMAKE_SHARED_MODULE_SUFFIX}")
          endif()
          set_property (TARGET ${name} PROPERTY SUFFIX ".${${prefix}_SOABI}${suffix}")
        endif()
      else()
        if (PYTHON_ADD_LIBRARY_WITH_SOABI)
          message (AUTHOR_WARNING "Find${prefix}: Option `WITH_SOABI` is only supported for `MODULE` library type.")
        endif()
        target_link_libraries (${name} PRIVATE ${prefix}::Python)
      endif()
    endfunction()
  endif()

  if ("NumPy" IN_LIST ${_PYTHON_PREFIX}_FIND_COMPONENTS AND ${_PYTHON_PREFIX}_NumPy_FOUND
      AND NOT TARGET ${_PYTHON_PREFIX}::NumPy AND TARGET ${_PYTHON_PREFIX}::Module)
    add_library (${_PYTHON_PREFIX}::NumPy INTERFACE IMPORTED)
    set_property (TARGET ${_PYTHON_PREFIX}::NumPy
                  PROPERTY INTERFACE_INCLUDE_DIRECTORIES "${${_PYTHON_PREFIX}_NumPy_INCLUDE_DIRS}")
    target_link_libraries (${_PYTHON_PREFIX}::NumPy INTERFACE ${_PYTHON_PREFIX}::Module)
  endif()
endif()

# final clean-up

# Restore CMAKE_FIND_APPBUNDLE
if (DEFINED _${_PYTHON_PREFIX}_CMAKE_FIND_APPBUNDLE)
  set (CMAKE_FIND_APPBUNDLE ${_${_PYTHON_PREFIX}_CMAKE_FIND_APPBUNDLE})
  unset (_${_PYTHON_PREFIX}_CMAKE_FIND_APPBUNDLE)
else()
  unset (CMAKE_FIND_APPBUNDLE)
endif()
# Restore CMAKE_FIND_FRAMEWORK
if (DEFINED _${_PYTHON_PREFIX}_CMAKE_FIND_FRAMEWORK)
  set (CMAKE_FIND_FRAMEWORK ${_${_PYTHON_PREFIX}_CMAKE_FIND_FRAMEWORK})
  unset (_${_PYTHON_PREFIX}_CMAKE_FIND_FRAMEWORK)
else()
  unset (CMAKE_FIND_FRAMEWORK)
endif()

cmake_policy(POP)
