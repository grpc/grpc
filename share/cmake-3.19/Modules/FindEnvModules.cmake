# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
FindEnvModules
--------------

.. versionadded:: 3.15

Locate an environment module implementation and make commands available to
CMake scripts to use them.  This is compatible with both Lua-based Lmod
and TCL-based EnvironmentModules.

This module is intended for the use case of setting up the compiler and library
environment within a :ref:`CTest Script <CTest Script>` (``ctest -S``).  It can
also be used in a :ref:`CMake Script <Script Processing Mode>` (``cmake -P``).

.. note::

  The loaded environment will not survive past the end of the calling process.
  Do not use this module in project code (``CMakeLists.txt`` files) to load
  a compiler environment; it will not be available during the build.  Instead
  load the environment manually before running CMake or using the generated
  build system.

Example Usage
^^^^^^^^^^^^^

.. code-block:: cmake

  set(CTEST_BUILD_NAME "CrayLinux-CrayPE-Cray-dynamic")
  set(CTEST_BUILD_CONFIGURATION Release)
  set(CTEST_BUILD_FLAGS "-k -j8")
  set(CTEST_CMAKE_GENERATOR "Unix Makefiles")

  ...

  find_package(EnvModules REQUIRED)

  env_module(purge)
  env_module(load modules)
  env_module(load craype)
  env_module(load PrgEnv-cray)
  env_module(load craype-knl)
  env_module(load cray-mpich)
  env_module(load cray-libsci)

  set(ENV{CRAYPE_LINK_TYPE} dynamic)

  ...

Result Variables
^^^^^^^^^^^^^^^^

This module will set the following variables in your project:

``EnvModules_FOUND``
  True if a compatible environment modules framework was found.

Cache Variables
^^^^^^^^^^^^^^^

The following cache variable will be set:

``EnvModules_COMMAND``
  The low level module command to use.  Currently supported
  implementations are the Lua based Lmod and TCL based EnvironmentModules.

Environment Variables
^^^^^^^^^^^^^^^^^^^^^

``ENV{MODULESHOME}``
  Usually set by the module environment implementation, used as a hint to
  locate the module command to execute.

Provided Functions
^^^^^^^^^^^^^^^^^^

This defines the following CMake functions for interacting with environment
modules:

.. command:: env_module

  Execute an aribitrary module command:

  .. code-block:: cmake

    env_module(cmd arg1 ... argN)
    env_module(
      COMMAND cmd arg1 ... argN
      [OUTPUT_VARIABLE <out-var>]
      [RESULT_VARIABLE <ret-var>]
    )

  The options are:

  ``cmd arg1 ... argN``
    The module sub-command and arguments to execute as if they were
    passed directly to the module command in your shell environment.

  ``OUTPUT_VARIABLE <out-var>``
    The standard output from executing the module command.

  ``RESULT_VARIABLE <ret-var>``
    The return code from executing the module command.

.. command:: env_module_swap

  Swap one module for another:

  .. code-block:: cmake

    env_module_swap(out_mod in_mod
      [OUTPUT_VARIABLE <out-var>]
      [RESULT_VARIABLE <ret-var>]
    )

  This is functionally equivalent to the ``module swap out_mod in_mod`` shell
  command.  The options are:

  ``OUTPUT_VARIABLE <out-var>``
    The standard output from executing the module command.

  ``RESULT_VARIABLE <ret-var>``
    The return code from executing the module command.

.. command:: env_module_list

  Retrieve the list of currently loaded modules:

  .. code-block:: cmake

    env_module_list(<out-var>)

  This is functionally equivalent to the ``module list`` shell command.
  The result is stored in ``<out-var>`` as a properly formatted CMake
  :ref:`semicolon-separated list <CMake Language Lists>` variable.

.. command:: env_module_avail

  Retrieve the list of available modules:

  .. code-block:: cmake

    env_module_avail([<mod-prefix>] <out-var>)

  This is functionally equivalent to the ``module avail <mod-prefix>`` shell
  command.  The result is stored in ``<out-var>`` as a properly formatted
  CMake :ref:`semicolon-separated list <CMake Language Lists>` variable.

#]=======================================================================]

function(env_module)
  if(NOT EnvModules_COMMAND)
    message(FATAL_ERROR "Failed to process module command.  EnvModules_COMMAND not found")
    return()
  endif()

  set(options)
  set(oneValueArgs OUTPUT_VARIABLE RESULT_VARIABLE)
  set(multiValueArgs COMMAND)
  cmake_parse_arguments(MOD_ARGS
    "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGV}
  )
  if(NOT MOD_ARGS_COMMAND)
    # If no explicit command argument was given, then treat the calling syntax
    # as: module(cmd args...)
    set(exec_cmd ${ARGV})
  else()
    set(exec_cmd ${MOD_ARGS_COMMAND})
  endif()

  if(MOD_ARGS_OUTPUT_VARIABLE)
    set(err_var_args ERROR_VARIABLE err_var)
  endif()

  execute_process(
    COMMAND mktemp -t module.cmake.XXXXXXXXXXXX
    OUTPUT_VARIABLE tempfile_name
  )
  string(STRIP "${tempfile_name}" tempfile_name)

  # If the $MODULESHOME/init/cmake file exists then assume that the CMake
  # "shell" functionality exits
  if(EXISTS "$ENV{MODULESHOME}/init/cmake")
    execute_process(
      COMMAND ${EnvModules_COMMAND} cmake ${exec_cmd}
      OUTPUT_FILE ${tempfile_name}
      ${err_var_args}
      RESULT_VARIABLE ret_var
    )

  else() # fallback to the sh shell and manually convert to CMake
    execute_process(
      COMMAND ${EnvModules_COMMAND} sh ${exec_cmd}
      OUTPUT_VARIABLE out_var
      ${err_var_args}
      RESULT_VARIABLE ret_var
    )
  endif()

  # If we executed successfully then process and cleanup the temp file
  if(ret_var EQUAL 0)
    # No CMake shell so we need to process the sh output into CMake code
    if(NOT EXISTS "$ENV{MODULESHOME}/init/cmake")
      file(WRITE ${tempfile_name} "")
      string(REPLACE "\n" ";" out_var "${out_var}")
      foreach(sh_cmd IN LISTS out_var)
        if(sh_cmd MATCHES "^ *unset *([^ ]*)")
          set(cmake_cmd "unset(ENV{${CMAKE_MATCH_1}})")
        elseif(sh_cmd MATCHES "^ *export *([^ ]*)")
          set(cmake_cmd "set(ENV{${CMAKE_MATCH_1}} \"\${${CMAKE_MATCH_1}}\")")
        elseif(sh_cmd MATCHES " *([^ =]*) *= *(.*)")
          set(var_name "${CMAKE_MATCH_1}")
          set(var_value "${CMAKE_MATCH_2}")
          if(var_value MATCHES "^\"(.*[^\\])\"")
            # If it's in quotes, take the value as is
            set(var_value "${CMAKE_MATCH_1}")
          else()
            # Otherwise, strip trailing spaces
            string(REGEX REPLACE "([^\\])? +$" "\\1" var_value "${var_value}")
          endif()
          string(REPLACE "\\ " " " var_value "${var_value}")
          set(cmake_cmd "set(${var_name} \"${var_value}\")")
        else()
          continue()
        endif()
        file(APPEND ${tempfile_name} "${cmake_cmd}\n")
      endforeach()
    endif()

    # Process the change in environment variables
    include(${tempfile_name})
    file(REMOVE ${tempfile_name})
  endif()

  # Push the output back out to the calling scope
  if(MOD_ARGS_OUTPUT_VARIABLE)
    set(${MOD_ARGS_OUTPUT_VARIABLE} "${err_var}" PARENT_SCOPE)
  endif()
  if(MOD_ARGS_RESULT_VARIABLE)
    set(${MOD_ARGS_RESULT_VARIABLE} ${ret_var} PARENT_SCOPE)
  endif()
endfunction(env_module)

#------------------------------------------------------------------------------
function(env_module_swap out_mod in_mod)
  set(options)
  set(oneValueArgs OUTPUT_VARIABLE RESULT_VARIABLE)
  set(multiValueArgs)

  cmake_parse_arguments(MOD_ARGS
    "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGV}
  )

  env_module(COMMAND -t swap ${out_mod} ${in_mod}
    OUTPUT_VARIABLE tmp_out
    RETURN_VARIABLE tmp_ret
  )

  if(MOD_ARGS_OUTPUT_VARIABLE)
    set(${MOD_ARGS_OUTPUT_VARIABLE} "${err_var}" PARENT_SCOPE)
  endif()
  if(MOD_ARGS_RESULT_VARIABLE)
    set(${MOD_ARGS_RESULT_VARIABLE} ${tmp_ret} PARENT_SCOPE)
  endif()
endfunction()

#------------------------------------------------------------------------------
function(env_module_list out_var)
  cmake_policy(SET CMP0007 NEW)
  env_module(COMMAND -t list OUTPUT_VARIABLE tmp_out)

  # Convert output into a CMake list
  string(REPLACE "\n" ";" ${out_var} "${tmp_out}")

  # Remove title headers and empty entries
  list(REMOVE_ITEM ${out_var} "No modules loaded")
  if(${out_var})
    list(FILTER ${out_var} EXCLUDE REGEX "^(.*:)?$")
  endif()
  list(FILTER ${out_var} EXCLUDE REGEX "^(.*:)?$")

  set(${out_var} ${${out_var}} PARENT_SCOPE)
endfunction()

#------------------------------------------------------------------------------
function(env_module_avail)
  cmake_policy(SET CMP0007 NEW)

  if(ARGC EQUAL 1)
    set(mod_prefix)
    set(out_var ${ARGV0})
  elseif(ARGC EQUAL 2)
    set(mod_prefix ${ARGV0})
    set(out_var ${ARGV1})
  else()
    message(FATAL_ERROR "Usage: env_module_avail([mod_prefix] out_var)")
  endif()
  env_module(COMMAND -t avail ${mod_prefix} OUTPUT_VARIABLE tmp_out)

  # Convert output into a CMake list
  string(REPLACE "\n" ";" tmp_out "${tmp_out}")

  set(${out_var})
  foreach(MOD IN LISTS tmp_out)
    # Remove directory entries and empty values
    if(MOD MATCHES "^(.*:)?$")
      continue()
    endif()

    # Convert default modules
    if(MOD MATCHES "^(.*)/$" ) # "foo/"
      list(APPEND ${out_var} ${CMAKE_MATCH_1})
    elseif(MOD MATCHES "^((.*)/.*)\\(default\\)$") # "foo/1.2.3(default)"
      list(APPEND ${out_var} ${CMAKE_MATCH_2})
      list(APPEND ${out_var} ${CMAKE_MATCH_1})
    else()
      list(APPEND ${out_var} ${MOD})
    endif()
  endforeach()

  set(${out_var} ${${out_var}} PARENT_SCOPE)
endfunction()

#------------------------------------------------------------------------------
# Make sure we know where the underlying module command is
find_program(EnvModules_COMMAND
  NAMES lmod modulecmd
  HINTS ENV MODULESHOME
  PATH_SUFFIXES libexec
)

include(${CMAKE_CURRENT_LIST_DIR}/FindPackageHandleStandardArgs.cmake)
find_package_handle_standard_args(EnvModules DEFAULT_MSG EnvModules_COMMAND)
