# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
FindMatlab
----------

Finds Matlab or Matlab Compiler Runtime (MCR) and provides Matlab tools,
libraries and compilers to CMake.

This package primary purpose is to find the libraries associated with Matlab
or the MCR in order to be able to build Matlab extensions (mex files). It
can also be used:

* to run specific commands in Matlab in case Matlab is available
* for declaring Matlab unit test
* to retrieve various information from Matlab (mex extensions, versions and
  release queries, ...)

The module supports the following components:

* ``ENG_LIBRARY`` and ``MAT_LIBRARY``: respectively the ``ENG`` and ``MAT``
  libraries of Matlab
* ``MAIN_PROGRAM`` the Matlab binary program. Note that this component is not
  available on the MCR version, and will yield an error if the MCR is found
  instead of the regular Matlab installation.
* ``MEX_COMPILER`` the MEX compiler.
* ``MCC_COMPILER`` the MCC compiler, included with the Matlab Compiler add-on.
* ``SIMULINK`` the Simulink environment.

.. note::

  The version given to the :command:`find_package` directive is the Matlab
  **version**, which should not be confused with the Matlab *release* name
  (eg. `R2014`).
  The :command:`matlab_get_version_from_release_name` and
  :command:`matlab_get_release_name_from_version` provide a mapping
  between the release name and the version.

The variable :variable:`Matlab_ROOT_DIR` may be specified in order to give
the path of the desired Matlab version. Otherwise, the behaviour is platform
specific:

* Windows: The installed versions of Matlab/MCR are retrieved from the
  Windows registry
* OS X: The installed versions of Matlab/MCR are given by the MATLAB
  default installation paths in ``/Application``. If no such application is
  found, it falls back to the one that might be accessible from the ``PATH``.
* Unix: The desired Matlab should be accessible from the ``PATH``. This does
  not work for MCR installation and :variable:`Matlab_ROOT_DIR` should be
  specified on this platform.

Additional information is provided when :variable:`MATLAB_FIND_DEBUG` is set.
When a Matlab/MCR installation is found automatically and the ``MATLAB_VERSION``
is not given, the version is queried from Matlab directly (on Windows this
may pop up a Matlab window) or from the MCR installation.

The mapping of the release names and the version of Matlab is performed by
defining pairs (name, version).  The variable
:variable:`MATLAB_ADDITIONAL_VERSIONS` may be provided before the call to
the :command:`find_package` in order to handle additional versions.

A Matlab scripts can be added to the set of tests using the
:command:`matlab_add_unit_test`. By default, the Matlab unit test framework
will be used (>= 2013a) to run this script, but regular ``.m`` files
returning an exit code can be used as well (0 indicating a success).

Module Input Variables
^^^^^^^^^^^^^^^^^^^^^^

Users or projects may set the following variables to configure the module
behaviour:

:variable:`Matlab_ROOT_DIR`
  the root of the Matlab installation.
:variable:`MATLAB_FIND_DEBUG`
  outputs debug information
:variable:`MATLAB_ADDITIONAL_VERSIONS`
  additional versions of Matlab for the automatic retrieval of the installed
  versions.

Variables defined by the module
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Result variables
""""""""""""""""

``Matlab_FOUND``
  ``TRUE`` if the Matlab installation is found, ``FALSE``
  otherwise. All variable below are defined if Matlab is found.
``Matlab_ROOT_DIR``
  the final root of the Matlab installation determined by the FindMatlab
  module.
``Matlab_MAIN_PROGRAM``
  the Matlab binary program. Available only if the component ``MAIN_PROGRAM``
  is given in the :command:`find_package` directive.
``Matlab_INCLUDE_DIRS``
 the path of the Matlab libraries headers
``Matlab_MEX_LIBRARY``
  library for mex, always available.
``Matlab_MX_LIBRARY``
  mx library of Matlab (arrays), always available.
``Matlab_ENG_LIBRARY``
  Matlab engine library. Available only if the component ``ENG_LIBRARY``
  is requested.
``Matlab_MAT_LIBRARY``
  Matlab matrix library. Available only if the component ``MAT_LIBRARY``
  is requested.
``Matlab_ENGINE_LIBRARY``
  Matlab C++ engine library, always available for R2018a and newer.
``Matlab_DATAARRAY_LIBRARY``
  Matlab C++ data array library, always available for R2018a and newer.
``Matlab_LIBRARIES``
  the whole set of libraries of Matlab
``Matlab_MEX_COMPILER``
  the mex compiler of Matlab. Currently not used.
  Available only if the component ``MEX_COMPILER`` is requested.
``Matlab_MCC_COMPILER``
  the mcc compiler of Matlab. Included with the Matlab Compiler add-on.
  Available only if the component ``MCC_COMPILER`` is requested.

Cached variables
""""""""""""""""

``Matlab_MEX_EXTENSION``
  the extension of the mex files for the current platform (given by Matlab).
``Matlab_ROOT_DIR``
  the location of the root of the Matlab installation found. If this value
  is changed by the user, the result variables are recomputed.

Provided macros
^^^^^^^^^^^^^^^

:command:`matlab_get_version_from_release_name`
  returns the version from the release name
:command:`matlab_get_release_name_from_version`
  returns the release name from the Matlab version

Provided functions
^^^^^^^^^^^^^^^^^^

:command:`matlab_add_mex`
  adds a target compiling a MEX file.
:command:`matlab_add_unit_test`
  adds a Matlab unit test file as a test to the project.
:command:`matlab_extract_all_installed_versions_from_registry`
  parses the registry for all Matlab versions. Available on Windows only.
  The part of the registry parsed is dependent on the host processor
:command:`matlab_get_all_valid_matlab_roots_from_registry`
  returns all the possible Matlab or MCR paths, according to a previously
  given list. Only the existing/accessible paths are kept. This is mainly
  useful for the searching all possible Matlab installation.
:command:`matlab_get_mex_suffix`
  returns the suffix to be used for the mex files
  (platform/architecture dependent)
:command:`matlab_get_version_from_matlab_run`
  returns the version of Matlab/MCR, given the full directory of the Matlab/MCR
  installation path.


Known issues
^^^^^^^^^^^^

**Symbol clash in a MEX target**
  By default, every symbols inside a MEX
  file defined with the command :command:`matlab_add_mex` have hidden
  visibility, except for the entry point. This is the default behaviour of
  the MEX compiler, which lowers the risk of symbol collision between the
  libraries shipped with Matlab, and the libraries to which the MEX file is
  linking to. This is also the default on Windows platforms.

  However, this is not sufficient in certain case, where for instance your
  MEX file is linking against libraries that are already loaded by Matlab,
  even if those libraries have different SONAMES.
  A possible solution is to hide the symbols of the libraries to which the
  MEX target is linking to. This can be achieved in GNU GCC compilers with
  the linker option ``-Wl,--exclude-libs,ALL``.

**Tests using GPU resources**
  in case your MEX file is using the GPU and
  in order to be able to run unit tests on this MEX file, the GPU resources
  should be properly released by Matlab. A possible solution is to make
  Matlab aware of the use of the GPU resources in the session, which can be
  performed by a command such as ``D = gpuDevice()`` at the beginning of
  the test script (or via a fixture).


Reference
^^^^^^^^^

.. variable:: Matlab_ROOT_DIR

   The root folder of the Matlab installation. If set before the call to
   :command:`find_package`, the module will look for the components in that
   path. If not set, then an automatic search of Matlab
   will be performed. If set, it should point to a valid version of Matlab.

.. variable:: MATLAB_FIND_DEBUG

   If set, the lookup of Matlab and the intermediate configuration steps are
   outputted to the console.

.. variable:: MATLAB_ADDITIONAL_VERSIONS

  If set, specifies additional versions of Matlab that may be looked for.
  The variable should be a list of strings, organised by pairs of release
  name and versions, such as follows::

    set(MATLAB_ADDITIONAL_VERSIONS
        "release_name1=corresponding_version1"
        "release_name2=corresponding_version2"
        ...
        )

  Example::

    set(MATLAB_ADDITIONAL_VERSIONS
        "R2013b=8.2"
        "R2013a=8.1"
        "R2012b=8.0")

  The order of entries in this list matters when several versions of
  Matlab are installed. The priority is set according to the ordering in
  this list.
#]=======================================================================]

cmake_policy(PUSH)
cmake_policy(SET CMP0057 NEW) # if IN_LIST

set(_FindMatlab_SELF_DIR "${CMAKE_CURRENT_LIST_DIR}")

include(${CMAKE_CURRENT_LIST_DIR}/FindPackageHandleStandardArgs.cmake)
include(CheckCXXCompilerFlag)
include(CheckCCompilerFlag)


# The currently supported versions. Other version can be added by the user by
# providing MATLAB_ADDITIONAL_VERSIONS
if(NOT MATLAB_ADDITIONAL_VERSIONS)
  set(MATLAB_ADDITIONAL_VERSIONS)
endif()

set(MATLAB_VERSIONS_MAPPING
  "R2020b=9.9"
  "R2020a=9.8"
  "R2019b=9.7"
  "R2019a=9.6"
  "R2018b=9.5"
  "R2018a=9.4"
  "R2017b=9.3"
  "R2017a=9.2"
  "R2016b=9.1"
  "R2016a=9.0"
  "R2015b=8.6"
  "R2015a=8.5"
  "R2014b=8.4"
  "R2014a=8.3"
  "R2013b=8.2"
  "R2013a=8.1"
  "R2012b=8.0"
  "R2012a=7.14"
  "R2011b=7.13"
  "R2011a=7.12"
  "R2010b=7.11"

  ${MATLAB_ADDITIONAL_VERSIONS}
  )


# temporary folder for all Matlab runs
set(_matlab_temporary_folder ${CMAKE_BINARY_DIR}/Matlab)

if(NOT EXISTS "${_matlab_temporary_folder}")
  file(MAKE_DIRECTORY "${_matlab_temporary_folder}")
endif()

#[=======================================================================[.rst:
.. command:: matlab_get_version_from_release_name

  Returns the version of Matlab (17.58) from a release name (R2017k)
#]=======================================================================]
macro(matlab_get_version_from_release_name release_name version_name)

  string(REGEX MATCHALL "${release_name}=([0-9]+\\.?[0-9]*)" _matched ${MATLAB_VERSIONS_MAPPING})

  set(${version_name} "")
  if(NOT _matched STREQUAL "")
    set(${version_name} ${CMAKE_MATCH_1})
  else()
    message(WARNING "[MATLAB] The release name ${release_name} is not registered")
  endif()
  unset(_matched)

endmacro()





#[=======================================================================[.rst:
.. command:: matlab_get_release_name_from_version

  Returns the release name (R2017k) from the version of Matlab (17.58)
#]=======================================================================]
macro(matlab_get_release_name_from_version version release_name)

  set(${release_name} "")
  foreach(_var IN LISTS MATLAB_VERSIONS_MAPPING)
    string(REGEX MATCHALL "(.+)=${version}" _matched ${_var})
    if(NOT _matched STREQUAL "")
      set(${release_name} ${CMAKE_MATCH_1})
      break()
    endif()
  endforeach(_var)

  unset(_var)
  unset(_matched)
  if(${release_name} STREQUAL "")
    message(WARNING "[MATLAB] The version ${version} is not registered")
  endif()

endmacro()





# extracts all the supported release names (R2017k...) of Matlab
# internal use
macro(matlab_get_supported_releases list_releases)
  set(${list_releases})
  foreach(_var IN LISTS MATLAB_VERSIONS_MAPPING)
    string(REGEX MATCHALL "(.+)=([0-9]+\\.?[0-9]*)" _matched ${_var})
    if(NOT _matched STREQUAL "")
      list(APPEND ${list_releases} ${CMAKE_MATCH_1})
    endif()
    unset(_matched)
    unset(CMAKE_MATCH_1)
  endforeach(_var)
  unset(_var)
endmacro()



# extracts all the supported versions of Matlab
# internal use
macro(matlab_get_supported_versions list_versions)
  set(${list_versions})
  foreach(_var IN LISTS MATLAB_VERSIONS_MAPPING)
    string(REGEX MATCHALL "(.+)=([0-9]+\\.?[0-9]*)" _matched ${_var})
    if(NOT _matched STREQUAL "")
      list(APPEND ${list_versions} ${CMAKE_MATCH_2})
    endif()
    unset(_matched)
    unset(CMAKE_MATCH_1)
  endforeach(_var)
  unset(_var)
endmacro()


#[=======================================================================[.rst:
.. command:: matlab_extract_all_installed_versions_from_registry

  This function parses the registry and founds the Matlab versions that are
  installed. The found versions are returned in `matlab_versions`.
  Set `win64` to `TRUE` if the 64 bit version of Matlab should be looked for
  The returned list contains all versions under
  ``HKLM\\SOFTWARE\\Mathworks\\MATLAB`` and
  ``HKLM\\SOFTWARE\\Mathworks\\MATLAB Runtime`` or an empty list in case an
  error occurred (or nothing found).

  .. note::

    Only the versions are provided. No check is made over the existence of the
    installation referenced in the registry,

#]=======================================================================]
function(matlab_extract_all_installed_versions_from_registry win64 matlab_versions)

  if(NOT CMAKE_HOST_WIN32)
    message(FATAL_ERROR "[MATLAB] This macro can only be called by a windows host (call to reg.exe)")
  endif()

  if(${win64} AND CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "64")
    set(APPEND_REG "/reg:64")
  else()
    set(APPEND_REG "/reg:32")
  endif()

  set(matlabs_from_registry)

  foreach(_installation_type IN ITEMS "MATLAB" "MATLAB Runtime")

    # /reg:64 should be added on 64 bits capable OSs in order to enable the
    # redirection of 64 bits applications
    execute_process(
      COMMAND reg query HKEY_LOCAL_MACHINE\\SOFTWARE\\Mathworks\\${_installation_type} /f * /k ${APPEND_REG}
      RESULT_VARIABLE resultMatlab
      OUTPUT_VARIABLE varMatlab
      ERROR_VARIABLE errMatlab
      INPUT_FILE NUL
      )


    if(resultMatlab EQUAL 0)

      string(
        REGEX MATCHALL "MATLAB\\\\([0-9]+(\\.[0-9]+)?)"
        matlab_versions_regex ${varMatlab})

      foreach(match IN LISTS matlab_versions_regex)
        string(
          REGEX MATCH "MATLAB\\\\(([0-9]+)(\\.([0-9]+))?)"
          current_match ${match})

        set(_matlab_current_version ${CMAKE_MATCH_1})
        set(current_matlab_version_major ${CMAKE_MATCH_2})
        set(current_matlab_version_minor ${CMAKE_MATCH_4})
        if(NOT current_matlab_version_minor)
          set(current_matlab_version_minor "0")
        endif()

        list(APPEND matlabs_from_registry ${_matlab_current_version})
        unset(_matlab_current_version)
      endforeach()

    endif()
  endforeach()

  if(matlabs_from_registry)
    list(REMOVE_DUPLICATES matlabs_from_registry)
    list(SORT matlabs_from_registry)
    list(REVERSE matlabs_from_registry)
  endif()

  set(${matlab_versions} ${matlabs_from_registry} PARENT_SCOPE)

endfunction()



# (internal)
macro(extract_matlab_versions_from_registry_brute_force matlab_versions)
  # get the supported versions
  set(matlab_supported_versions)
  matlab_get_supported_versions(matlab_supported_versions)


  # this is a manual population of the versions we want to look for
  # this can be done as is, but preferably with the call to
  # matlab_get_supported_versions and variable

  # populating the versions we want to look for
  # set(matlab_supported_versions)

  # # Matlab 7
  # set(matlab_major 7)
  # foreach(current_matlab_minor RANGE 4 20)
    # list(APPEND matlab_supported_versions "${matlab_major}.${current_matlab_minor}")
  # endforeach(current_matlab_minor)

  # # Matlab 8
  # set(matlab_major 8)
  # foreach(current_matlab_minor RANGE 0 5)
    # list(APPEND matlab_supported_versions "${matlab_major}.${current_matlab_minor}")
  # endforeach(current_matlab_minor)

  # # taking into account the possible additional versions provided by the user
  # if(DEFINED MATLAB_ADDITIONAL_VERSIONS)
    # list(APPEND matlab_supported_versions MATLAB_ADDITIONAL_VERSIONS)
  # endif()

  # we order from more recent to older
  if(matlab_supported_versions)
    list(REMOVE_DUPLICATES matlab_supported_versions)
    list(SORT matlab_supported_versions)
    list(REVERSE matlab_supported_versions)
  endif()

  set(${matlab_versions} ${matlab_supported_versions})
endmacro()




#[=======================================================================[.rst:
.. command:: matlab_get_all_valid_matlab_roots_from_registry

  Populates the Matlab root with valid versions of Matlab or
  Matlab Runtime (MCR).
  The returned matlab_roots is organized in triplets
  ``(type,version_number,matlab_root_path)``, where ``type``
  indicates either ``MATLAB`` or ``MCR``.

  ::

    matlab_get_all_valid_matlab_roots_from_registry(
        matlab_versions
        matlab_roots)

  ``matlab_versions``
    the versions of each of the Matlab or MCR installations
  ``matlab_roots``
    the location of each of the Matlab or MCR installations
#]=======================================================================]
function(matlab_get_all_valid_matlab_roots_from_registry matlab_versions matlab_roots)

  # The matlab_versions comes either from
  # extract_matlab_versions_from_registry_brute_force or
  # matlab_extract_all_installed_versions_from_registry.

  set(_matlab_roots_list )
  # check for Matlab installations
  foreach(_matlab_current_version ${matlab_versions})
    get_filename_component(
      current_MATLAB_ROOT
      "[HKEY_LOCAL_MACHINE\\SOFTWARE\\MathWorks\\MATLAB\\${_matlab_current_version};MATLABROOT]"
      ABSOLUTE)

    if(EXISTS ${current_MATLAB_ROOT})
      list(APPEND _matlab_roots_list "MATLAB" ${_matlab_current_version} ${current_MATLAB_ROOT})
    endif()

  endforeach()

  # Check for MCR installations
  foreach(_matlab_current_version ${matlab_versions})
    get_filename_component(
      current_MATLAB_ROOT
      "[HKEY_LOCAL_MACHINE\\SOFTWARE\\MathWorks\\MATLAB Runtime\\${_matlab_current_version};MATLABROOT]"
      ABSOLUTE)

    # remove the dot
    string(REPLACE "." "" _matlab_current_version_without_dot "${_matlab_current_version}")

    if(EXISTS ${current_MATLAB_ROOT})
      list(APPEND _matlab_roots_list "MCR" ${_matlab_current_version} "${current_MATLAB_ROOT}/v${_matlab_current_version_without_dot}")
    endif()

  endforeach()
  set(${matlab_roots} ${_matlab_roots_list} PARENT_SCOPE)
endfunction()

#[=======================================================================[.rst:
.. command:: matlab_get_mex_suffix

  Returns the extension of the mex files (the suffixes).
  This function should not be called before the appropriate Matlab root has
  been found.

  ::

    matlab_get_mex_suffix(
        matlab_root
        mex_suffix)

  ``matlab_root``
    the root of the Matlab/MCR installation
  ``mex_suffix``
    the variable name in which the suffix will be returned.
#]=======================================================================]
function(matlab_get_mex_suffix matlab_root mex_suffix)

  # todo setup the extension properly. Currently I do not know if this is
  # sufficient for all win32 distributions.
  # there is also CMAKE_EXECUTABLE_SUFFIX that could be tweaked
  set(mexext_suffix "")
  if(WIN32)
    list(APPEND mexext_suffix ".bat")
  endif()

  # we first try without suffix, since cmake does not understand a list with
  # one empty string element
  find_program(
    Matlab_MEXEXTENSIONS_PROG
    NAMES mexext
    PATHS ${matlab_root}/bin
    DOC "Matlab MEX extension provider"
    NO_DEFAULT_PATH
  )

  foreach(current_mexext_suffix IN LISTS mexext_suffix)
    if(NOT DEFINED Matlab_MEXEXTENSIONS_PROG OR NOT Matlab_MEXEXTENSIONS_PROG)
      # this call should populate the cache automatically
      find_program(
        Matlab_MEXEXTENSIONS_PROG
        "mexext${current_mexext_suffix}"
        PATHS ${matlab_root}/bin
        DOC "Matlab MEX extension provider"
        NO_DEFAULT_PATH
      )
    endif()
  endforeach(current_mexext_suffix)
  if(MATLAB_FIND_DEBUG)
    message(STATUS "[MATLAB] Determining mex files extensions from '${matlab_root}/bin' with program '${Matlab_MEXEXTENSIONS_PROG}'")
  endif()

  # the program has been found?
  if((NOT Matlab_MEXEXTENSIONS_PROG) OR (NOT EXISTS ${Matlab_MEXEXTENSIONS_PROG}))
    if(MATLAB_FIND_DEBUG)
      message(WARNING "[MATLAB] Cannot found mexext program. Matlab root is ${matlab_root}")
    endif()
    unset(Matlab_MEXEXTENSIONS_PROG CACHE)
    return()
  endif()

  set(_matlab_mex_extension)

  set(devnull)
  if(UNIX)
    set(devnull INPUT_FILE /dev/null)
  elseif(WIN32)
    set(devnull INPUT_FILE NUL)
  endif()

  if(WIN32)
    # this environment variable is used to determine the arch on Windows
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
      set(ENV{MATLAB_ARCH} "win64")
    else()
      set(ENV{MATLAB_ARCH} "win32")
    endif()
  endif()

  # this is the preferred way. If this does not work properly (eg. MCR on Windows), then we use our own knowledge
  execute_process(
    COMMAND ${Matlab_MEXEXTENSIONS_PROG}
    OUTPUT_VARIABLE _matlab_mex_extension
    ERROR_VARIABLE _matlab_mex_extension_error
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ${devnull})
  unset(ENV{MATLAB_ARCH})

  if(_matlab_mex_extension_error)
    if(WIN32)
      # this is only for intel architecture
      if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(_matlab_mex_extension "mexw64")
      else()
        set(_matlab_mex_extension "mexw32")
      endif()
    endif()
  endif()

  string(STRIP "${_matlab_mex_extension}"  _matlab_mex_extension)
  if(MATLAB_FIND_DEBUG)
    message(STATUS "[MATLAB] '${Matlab_MEXEXTENSIONS_PROG}' : determined extension '${_matlab_mex_extension}' and error string is '${_matlab_mex_extension_error}'")
  endif()

  unset(Matlab_MEXEXTENSIONS_PROG CACHE)
  set(${mex_suffix} ${_matlab_mex_extension} PARENT_SCOPE)
endfunction()




#[=======================================================================[.rst:
.. command:: matlab_get_version_from_matlab_run

  This function runs Matlab program specified on arguments and extracts its
  version. If the path provided for the Matlab installation points to an MCR
  installation, the version is extracted from the installed files.

  ::

    matlab_get_version_from_matlab_run(
        matlab_binary_path
        matlab_list_versions)

  ``matlab_binary_path``
    the location of the `matlab` binary executable
  ``matlab_list_versions``
    the version extracted from Matlab
#]=======================================================================]
function(matlab_get_version_from_matlab_run matlab_binary_program matlab_list_versions)

  set(${matlab_list_versions} "" PARENT_SCOPE)

  if(MATLAB_FIND_DEBUG)
    message(STATUS "[MATLAB] Determining the version of Matlab from ${matlab_binary_program}")
  endif()

  if(EXISTS "${_matlab_temporary_folder}/matlabVersionLog.cmaketmp")
    if(MATLAB_FIND_DEBUG)
      message(STATUS "[MATLAB] Removing previous ${_matlab_temporary_folder}/matlabVersionLog.cmaketmp file")
    endif()
    file(REMOVE "${_matlab_temporary_folder}/matlabVersionLog.cmaketmp")
  endif()


  # the log file is needed since on windows the command executes in a new
  # window and it is not possible to get back the answer of Matlab
  # the -wait command is needed on windows, otherwise the call returns
  # immediately after the program launches itself.
  if(WIN32)
    set(_matlab_additional_commands "-wait")
  endif()

  set(devnull)
  if(UNIX)
    set(devnull INPUT_FILE /dev/null)
  elseif(WIN32)
    set(devnull INPUT_FILE NUL)
  endif()

  # timeout set to 120 seconds, in case it does not start
  # note as said before OUTPUT_VARIABLE cannot be used in a platform
  # independent manner however, not setting it would flush the output of Matlab
  # in the current console (unix variant)
  execute_process(
    COMMAND "${matlab_binary_program}" -nosplash -nojvm ${_matlab_additional_commands} -logfile "matlabVersionLog.cmaketmp" -nodesktop -nodisplay -r "version, exit"
    OUTPUT_VARIABLE _matlab_version_from_cmd_dummy
    RESULT_VARIABLE _matlab_result_version_call
    ERROR_VARIABLE _matlab_result_version_call_error
    TIMEOUT 120
    WORKING_DIRECTORY "${_matlab_temporary_folder}"
    ${devnull}
    )

  if(_matlab_result_version_call MATCHES "timeout")
    if(MATLAB_FIND_DEBUG)
      message(WARNING "[MATLAB] Unable to determine the version of Matlab."
        " Matlab call timed out after 120 seconds.")
    endif()
    return()
  endif()

  if(${_matlab_result_version_call})
    if(MATLAB_FIND_DEBUG)
      message(WARNING "[MATLAB] Unable to determine the version of Matlab. Matlab call returned with error ${_matlab_result_version_call}.")
    endif()
    return()
  elseif(NOT EXISTS "${_matlab_temporary_folder}/matlabVersionLog.cmaketmp")
    if(MATLAB_FIND_DEBUG)
      message(WARNING "[MATLAB] Unable to determine the version of Matlab. The log file does not exist.")
    endif()
    return()
  endif()

  # if successful, read back the log
  file(READ "${_matlab_temporary_folder}/matlabVersionLog.cmaketmp" _matlab_version_from_cmd)
  file(REMOVE "${_matlab_temporary_folder}/matlabVersionLog.cmaketmp")

  set(index -1)
  string(FIND "${_matlab_version_from_cmd}" "ans" index)
  if(index EQUAL -1)

    if(MATLAB_FIND_DEBUG)
      message(WARNING "[MATLAB] Cannot find the version of Matlab returned by the run.")
    endif()

  else()
    set(matlab_list_of_all_versions_tmp)

    string(SUBSTRING "${_matlab_version_from_cmd}" ${index} -1 substring_ans)
    string(
      REGEX MATCHALL "ans[\r\n\t ]*=[\r\n\t ]*'?([0-9]+(\\.[0-9]+)?)"
      matlab_versions_regex
      ${substring_ans})
    foreach(match IN LISTS matlab_versions_regex)
      string(
        REGEX MATCH "ans[\r\n\t ]*=[\r\n\t ]*'?(([0-9]+)(\\.([0-9]+))?)"
        current_match ${match})

      list(APPEND matlab_list_of_all_versions_tmp ${CMAKE_MATCH_1})
    endforeach()
    if(matlab_list_of_all_versions_tmp)
      list(REMOVE_DUPLICATES matlab_list_of_all_versions_tmp)
    endif()
    set(${matlab_list_versions} ${matlab_list_of_all_versions_tmp} PARENT_SCOPE)

  endif()

endfunction()

#[=======================================================================[.rst:
.. command:: matlab_add_unit_test

  Adds a Matlab unit test to the test set of cmake/ctest.
  This command requires the component ``MAIN_PROGRAM`` and hence is not
  available for an MCR installation.

  The unit test uses the Matlab unittest framework (default, available
  starting Matlab 2013b+) except if the option ``NO_UNITTEST_FRAMEWORK``
  is given.

  The function expects one Matlab test script file to be given.
  In the case ``NO_UNITTEST_FRAMEWORK`` is given, the unittest script file
  should contain the script to be run, plus an exit command with the exit
  value. This exit value will be passed to the ctest framework (0 success,
  non 0 failure). Additional arguments accepted by :command:`add_test` can be
  passed through ``TEST_ARGS`` (eg. ``CONFIGURATION <config> ...``).

  ::

    matlab_add_unit_test(
        NAME <name>
        UNITTEST_FILE matlab_file_containing_unittest.m
        [CUSTOM_TEST_COMMAND matlab_command_to_run_as_test]
        [UNITTEST_PRECOMMAND matlab_command_to_run]
        [TIMEOUT timeout]
        [ADDITIONAL_PATH path1 [path2 ...]]
        [MATLAB_ADDITIONAL_STARTUP_OPTIONS option1 [option2 ...]]
        [TEST_ARGS arg1 [arg2 ...]]
        [NO_UNITTEST_FRAMEWORK]
        )

  The function arguments are:

  ``NAME``
    name of the unittest in ctest.
  ``UNITTEST_FILE``
    the matlab unittest file. Its path will be automatically
    added to the Matlab path.
  ``CUSTOM_TEST_COMMAND``
    Matlab script command to run as the test.
    If this is not set, then the following is run:
    ``runtests('matlab_file_name'), exit(max([ans(1,:).Failed]))``
    where ``matlab_file_name`` is the ``UNITTEST_FILE`` without the extension.
  ``UNITTEST_PRECOMMAND``
    Matlab script command to be ran before the file
    containing the test (eg. GPU device initialisation based on CMake
    variables).
  ``TIMEOUT``
    the test timeout in seconds. Defaults to 180 seconds as the
    Matlab unit test may hang.
  ``ADDITIONAL_PATH``
    a list of paths to add to the Matlab path prior to
    running the unit test.
  ``MATLAB_ADDITIONAL_STARTUP_OPTIONS``
    a list of additional option in order
    to run Matlab from the command line.
    ``-nosplash -nodesktop -nodisplay`` are always added.
  ``TEST_ARGS``
    Additional options provided to the add_test command. These
    options are added to the default options (eg. "CONFIGURATIONS Release")
  ``NO_UNITTEST_FRAMEWORK``
    when set, indicates that the test should not
    use the unittest framework of Matlab (available for versions >= R2013a).
  ``WORKING_DIRECTORY``
    This will be the working directory for the test. If specified it will
    also be the output directory used for the log file of the test run.
    If not specified the temporary directory ``${CMAKE_BINARY_DIR}/Matlab`` will
    be used as the working directory and the log location.

#]=======================================================================]
function(matlab_add_unit_test)

  if(NOT Matlab_MAIN_PROGRAM)
    message(FATAL_ERROR "[MATLAB] This functionality needs the MAIN_PROGRAM component (not default)")
  endif()

  set(options NO_UNITTEST_FRAMEWORK)
  set(oneValueArgs NAME UNITTEST_FILE TIMEOUT WORKING_DIRECTORY
    UNITTEST_PRECOMMAND CUSTOM_TEST_COMMAND)
  set(multiValueArgs ADDITIONAL_PATH MATLAB_ADDITIONAL_STARTUP_OPTIONS TEST_ARGS)

  set(prefix _matlab_unittest_prefix)
  cmake_parse_arguments(PARSE_ARGV 0 ${prefix} "${options}" "${oneValueArgs}" "${multiValueArgs}" )

  if(NOT ${prefix}_NAME)
    message(FATAL_ERROR "[MATLAB] The Matlab test name cannot be empty")
  endif()

  add_test(NAME ${${prefix}_NAME}
           COMMAND ${CMAKE_COMMAND}
            "-Dtest_name=${${prefix}_NAME}"
            "-Dadditional_paths=${${prefix}_ADDITIONAL_PATH}"
            "-Dtest_timeout=${${prefix}_TIMEOUT}"
            "-Doutput_directory=${_matlab_temporary_folder}"
            "-Dworking_directory=${${prefix}_WORKING_DIRECTORY}"
            "-DMatlab_PROGRAM=${Matlab_MAIN_PROGRAM}"
            "-Dno_unittest_framework=${${prefix}_NO_UNITTEST_FRAMEWORK}"
            "-DMatlab_ADDITIONAL_STARTUP_OPTIONS=${${prefix}_MATLAB_ADDITIONAL_STARTUP_OPTIONS}"
            "-Dunittest_file_to_run=${${prefix}_UNITTEST_FILE}"
            "-Dcustom_Matlab_test_command=${${prefix}_CUSTOM_TEST_COMMAND}"
            "-Dcmd_to_run_before_test=${${prefix}_UNITTEST_PRECOMMAND}"
            -P ${_FindMatlab_SELF_DIR}/MatlabTestsRedirect.cmake
           ${${prefix}_TEST_ARGS}
           ${${prefix}_UNPARSED_ARGUMENTS}
           )
endfunction()


#[=======================================================================[.rst:
.. command:: matlab_add_mex

  Adds a Matlab MEX target.
  This commands compiles the given sources with the current tool-chain in
  order to produce a MEX file. The final name of the produced output may be
  specified, as well as additional link libraries, and a documentation entry
  for the MEX file. Remaining arguments of the call are passed to the
  :command:`add_library` or :command:`add_executable` command.

  ::

     matlab_add_mex(
         NAME <name>
         [EXECUTABLE | MODULE | SHARED]
         SRC src1 [src2 ...]
         [OUTPUT_NAME output_name]
         [DOCUMENTATION file.txt]
         [LINK_TO target1 target2 ...]
         [R2017b | R2018a]
         [EXCLUDE_FROM_ALL]
         [...]
     )

  ``NAME``
    name of the target.
  ``SRC``
    list of source files.
  ``LINK_TO``
    a list of additional link dependencies.  The target links to ``libmex``
    and ``libmx`` by default.
  ``OUTPUT_NAME``
    if given, overrides the default name. The default name is
    the name of the target without any prefix and
    with ``Matlab_MEX_EXTENSION`` suffix.
  ``DOCUMENTATION``
    if given, the file ``file.txt`` will be considered as
    being the documentation file for the MEX file. This file is copied into
    the same folder without any processing, with the same name as the final
    mex file, and with extension `.m`. In that case, typing ``help <name>``
    in Matlab prints the documentation contained in this file.
  ``R2017b`` or ``R2018a`` may be given to specify the version of the C API
    to use: ``R2017b`` specifies the traditional (separate complex) C API,
    and corresponds to the ``-R2017b`` flag for the `mex` command. ``R2018a``
    specifies the new interleaved complex C API, and corresponds to the
    ``-R2018a`` flag for the `mex` command. Ignored if MATLAB version prior
    to R2018a. Defaults to ``R2017b``.
  ``MODULE`` or ``SHARED`` may be given to specify the type of library to be
    created. ``EXECUTABLE`` may be given to create an executable instead of
    a library. If no type is given explicitly, the type is ``SHARED``.
  ``EXCLUDE_FROM_ALL``
    This option has the same meaning as for :prop_tgt:`EXCLUDE_FROM_ALL` and
    is forwarded to :command:`add_library` or :command:`add_executable`
    commands.

  The documentation file is not processed and should be in the following
  format:

  ::

    % This is the documentation
    function ret = mex_target_output_name(input1)

#]=======================================================================]
function(matlab_add_mex)

  if(NOT WIN32)
    # we do not need all this on Windows
    # pthread options
    if(CMAKE_CXX_COMPILER_LOADED)
      check_cxx_compiler_flag(-pthread HAS_MINUS_PTHREAD)
    elseif(CMAKE_C_COMPILER_LOADED)
      check_c_compiler_flag(-pthread HAS_MINUS_PTHREAD)
    endif()
    # we should use try_compile instead, the link flags are discarded from
    # this compiler_flag function.
    #check_cxx_compiler_flag(-Wl,--exclude-libs,ALL HAS_SYMBOL_HIDING_CAPABILITY)

  endif()

  set(options EXECUTABLE MODULE SHARED R2017b R2018a EXCLUDE_FROM_ALL)
  set(oneValueArgs NAME DOCUMENTATION OUTPUT_NAME)
  set(multiValueArgs LINK_TO SRC)

  set(prefix _matlab_addmex_prefix)
  cmake_parse_arguments(${prefix} "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN} )

  if(NOT ${prefix}_NAME)
    message(FATAL_ERROR "[MATLAB] The MEX target name cannot be empty")
  endif()

  if(NOT ${prefix}_OUTPUT_NAME)
    set(${prefix}_OUTPUT_NAME ${${prefix}_NAME})
  endif()

  if(NOT Matlab_VERSION_STRING VERSION_LESS "9.1") # For 9.1 (R2016b) and newer, add version source file
    # Add the correct version file depending on which languages are enabled in the project
    if(CMAKE_C_COMPILER_LOADED)
      # If C is enabled, use the .c file as it will work fine also with C++
      set(MEX_VERSION_FILE "${Matlab_ROOT_DIR}/extern/version/c_mexapi_version.c")
    elseif(CMAKE_CXX_COMPILER_LOADED)
      # If C is not enabled, check if CXX is enabled and use the .cpp file
      # to avoid that the .c file is silently ignored
      set(MEX_VERSION_FILE "${Matlab_ROOT_DIR}/extern/version/cpp_mexapi_version.cpp")
    else()
      # If neither C or CXX is enabled, warn because we cannot add the source.
      # TODO: add support for fortran mex files
      message(WARNING "[MATLAB] matlab_add_mex requires that at least C or CXX are enabled languages")
    endif()
  endif()

  # For 9.4 (R2018a) and newer, add API macro.
  # Add it for unknown versions too, just in case.
  if(NOT Matlab_VERSION_STRING VERSION_LESS "9.4"
      OR Matlab_VERSION_STRING STREQUAL "unknown")
    if(${${prefix}_R2018a})
      set(MEX_API_MACRO "MATLAB_DEFAULT_RELEASE=R2018a")
    else()
      set(MEX_API_MACRO "MATLAB_DEFAULT_RELEASE=R2017b")
    endif()
  endif()

  set(_option_EXCLUDE_FROM_ALL)
  if(${prefix}_EXCLUDE_FROM_ALL)
    set(_option_EXCLUDE_FROM_ALL "EXCLUDE_FROM_ALL")
  endif()

  if(${prefix}_EXECUTABLE)
    add_executable(${${prefix}_NAME}
      ${_option_EXCLUDE_FROM_ALL}
      ${${prefix}_SRC}
      ${MEX_VERSION_FILE}
      ${${prefix}_DOCUMENTATION}
      ${${prefix}_UNPARSED_ARGUMENTS})
  else()
    if(${prefix}_MODULE)
      set(type MODULE)
    else()
      set(type SHARED)
    endif()

    add_library(${${prefix}_NAME}
      ${type}
      ${_option_EXCLUDE_FROM_ALL}
      ${${prefix}_SRC}
      ${MEX_VERSION_FILE}
      ${${prefix}_DOCUMENTATION}
      ${${prefix}_UNPARSED_ARGUMENTS})
  endif()

  target_include_directories(${${prefix}_NAME} PRIVATE ${Matlab_INCLUDE_DIRS})

  if(Matlab_HAS_CPP_API)
    target_link_libraries(${${prefix}_NAME} ${Matlab_ENGINE_LIBRARY} ${Matlab_DATAARRAY_LIBRARY})
  endif()

  target_link_libraries(${${prefix}_NAME} ${Matlab_MEX_LIBRARY} ${Matlab_MX_LIBRARY} ${${prefix}_LINK_TO})
  set_target_properties(${${prefix}_NAME}
      PROPERTIES
        PREFIX ""
        OUTPUT_NAME ${${prefix}_OUTPUT_NAME}
        SUFFIX ".${Matlab_MEX_EXTENSION}")

  target_compile_definitions(${${prefix}_NAME} PRIVATE ${MEX_API_MACRO} MATLAB_MEX_FILE)

  # documentation
  if(NOT ${${prefix}_DOCUMENTATION} STREQUAL "")
    get_target_property(output_name ${${prefix}_NAME} OUTPUT_NAME)
    add_custom_command(
      TARGET ${${prefix}_NAME}
      PRE_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different ${${prefix}_DOCUMENTATION} $<TARGET_FILE_DIR:${${prefix}_NAME}>/${output_name}.m
      COMMENT "[MATLAB] Copy ${${prefix}_NAME} documentation file into the output folder"
    )
  endif() # documentation

  # entry point in the mex file + taking care of visibility and symbol clashes.
  if(WIN32)

    if (MSVC)

      set(_link_flags "${_link_flags} /EXPORT:mexFunction")
      if(NOT Matlab_VERSION_STRING VERSION_LESS "9.1") # For 9.1 (R2016b) and newer, export version
        set(_link_flags "${_link_flags} /EXPORT:mexfilerequiredapiversion")
      endif()

      set_property(TARGET ${${prefix}_NAME} APPEND PROPERTY LINK_FLAGS ${_link_flags})

    endif() # No other compiler currently supported on Windows.

    set_target_properties(${${prefix}_NAME}
      PROPERTIES
        DEFINE_SYMBOL "DLL_EXPORT_SYM=__declspec(dllexport)")

  else()

    if(Matlab_VERSION_STRING VERSION_LESS "9.1") # For versions prior to 9.1 (R2016b)
      set(_ver_map_files ${Matlab_EXTERN_LIBRARY_DIR}/mexFunction.map)
    else()                                          # For 9.1 (R2016b) and newer
      set(_ver_map_files ${Matlab_EXTERN_LIBRARY_DIR}/c_exportsmexfileversion.map)
    endif()

    if(NOT Matlab_VERSION_STRING VERSION_LESS "9.5") # For 9.5 (R2018b) (and newer?)
      target_compile_options(${${prefix}_NAME} PRIVATE "-fvisibility=default")
      # This one is weird, it might be a bug in <mex.h> for R2018b. When compiling with
      # -fvisibility=hidden, the symbol `mexFunction` cannot be exported. Reading the
      # source code for <mex.h>, it seems that the preprocessor macro `MW_NEEDS_VERSION_H`
      # needs to be defined for `__attribute__((visibility("default")))` to be added
      # in front of the declaration of `mexFunction`. In previous versions of MATLAB this
      # was not the case, there `DLL_EXPORT_SYM` needed to be defined.
      # Adding `-fvisibility=hidden` to the `mex` command causes the build to fail.
      # TODO: Check that this is still necessary in R2019a when it comes out.
    endif()

    if(APPLE)

      if(Matlab_HAS_CPP_API)
        list(APPEND _ver_map_files ${Matlab_EXTERN_LIBRARY_DIR}/cppMexFunction.map) # This one doesn't exist on Linux
        set(_link_flags "${_link_flags} -Wl,-U,_mexCreateMexFunction -Wl,-U,_mexDestroyMexFunction -Wl,-U,_mexFunctionAdapter")
        # On MacOS, the MEX command adds the above, without it the link breaks
        # because we indiscriminately use "cppMexFunction.map" even for C API MEX-files.
      endif()

      set(_export_flag_name -exported_symbols_list)

    else() # Linux

      if(HAS_MINUS_PTHREAD)
        # Apparently, compiling with -pthread generated the proper link flags
        # and some defines at compilation
        target_compile_options(${${prefix}_NAME} PRIVATE "-pthread")
      endif()

      set(_link_flags "${_link_flags} -Wl,--as-needed")

      set(_export_flag_name --version-script)

    endif()

    foreach(_file ${_ver_map_files})
      set(_link_flags "${_link_flags} -Wl,${_export_flag_name},${_file}")
    endforeach()

    # The `mex` command doesn't add this define. It is specified here in order
    # to export the symbol in case the client code decides to hide its symbols
    set_target_properties(${${prefix}_NAME}
      PROPERTIES
        DEFINE_SYMBOL "DLL_EXPORT_SYM=__attribute__((visibility(\"default\")))"
        LINK_FLAGS "${_link_flags}"
    )

  endif()

endfunction()


# (internal)
# Used to get the version of matlab, using caching. This basically transforms the
# output of the root list, with possible unknown version, to a version
# This can possibly run Matlab for extracting the version.
function(_Matlab_get_version_from_root matlab_root matlab_or_mcr matlab_known_version matlab_final_version)

  # if the version is not trivial, we query matlab (if not MCR) for that
  # we keep track of the location of matlab that induced this version
  #if(NOT DEFINED Matlab_PROG_VERSION_STRING_AUTO_DETECT)
  #  set(Matlab_PROG_VERSION_STRING_AUTO_DETECT "" CACHE INTERNAL "internal matlab location for the discovered version")
  #endif()

  if(NOT matlab_known_version STREQUAL "NOTFOUND")
    # the version is known, we just return it
    set(${matlab_final_version} ${matlab_known_version} PARENT_SCOPE)
    set(Matlab_VERSION_STRING_INTERNAL ${matlab_known_version} CACHE INTERNAL "Matlab version (automatically determined)" FORCE)
    return()
  endif()

  if(matlab_or_mcr STREQUAL "UNKNOWN")
    if(MATLAB_FIND_DEBUG)
      message(WARNING "[MATLAB] Determining Matlab or MCR")
    endif()

    if(EXISTS "${matlab_root}/appdata/version.xml")
      # we inspect the application version.xml file that contains the product information
      file(STRINGS "${matlab_root}/appdata/version.xml" productinfo_string NEWLINE_CONSUME)
      string(REGEX MATCH "<installedProductData.*displayedString=\"([a-zA-Z ]+)\".*/>"
             product_reg_match
             ${productinfo_string}
            )

      # default fallback to Matlab
      set(matlab_or_mcr "MATLAB")
      if(NOT CMAKE_MATCH_1 STREQUAL "")
        string(TOLOWER "${CMAKE_MATCH_1}" product_reg_match)

        if(product_reg_match STREQUAL "matlab runtime")
          set(matlab_or_mcr "MCR")
        endif()
      endif()
    endif()

    if(MATLAB_FIND_DEBUG)
      message(WARNING "[MATLAB] '${matlab_root}' contains the '${matlab_or_mcr}'")
    endif()
  endif()

  # UNKNOWN is the default behaviour in case we
  # - have an erroneous matlab_root
  # - have an initial 'UNKNOWN'
  if(matlab_or_mcr STREQUAL "MATLAB" OR matlab_or_mcr STREQUAL "UNKNOWN")
    # MATLAB versions
    set(_matlab_current_program ${Matlab_MAIN_PROGRAM})

    # do we already have a matlab program?
    if(NOT _matlab_current_program)

      set(_find_matlab_options)
      if(matlab_root AND EXISTS ${matlab_root})
        set(_find_matlab_options PATHS ${matlab_root} ${matlab_root}/bin NO_DEFAULT_PATH)
      endif()

      find_program(
          _matlab_current_program
          matlab
          ${_find_matlab_options}
          DOC "Matlab main program"
        )
    endif()

    if(NOT _matlab_current_program OR NOT EXISTS ${_matlab_current_program})
      # if not found, clear the dependent variables
      if(MATLAB_FIND_DEBUG)
        message(WARNING "[MATLAB] Cannot find the main matlab program under ${matlab_root}")
      endif()
      set(Matlab_PROG_VERSION_STRING_AUTO_DETECT "" CACHE INTERNAL "internal matlab location for the discovered version" FORCE)
      set(Matlab_VERSION_STRING_INTERNAL "" CACHE INTERNAL "internal matlab location for the discovered version" FORCE)
      unset(_matlab_current_program)
      unset(_matlab_current_program CACHE)
      return()
    endif()

    # full real path for path comparison
    get_filename_component(_matlab_main_real_path_tmp "${_matlab_current_program}" REALPATH)
    unset(_matlab_current_program)
    unset(_matlab_current_program CACHE)

    # is it the same as the previous one?
    if(_matlab_main_real_path_tmp STREQUAL Matlab_PROG_VERSION_STRING_AUTO_DETECT)
      set(${matlab_final_version} ${Matlab_VERSION_STRING_INTERNAL} PARENT_SCOPE)
      return()
    endif()

    # update the location of the program
    set(Matlab_PROG_VERSION_STRING_AUTO_DETECT
        ${_matlab_main_real_path_tmp}
        CACHE INTERNAL "internal matlab location for the discovered version" FORCE)

    set(matlab_list_of_all_versions)
    matlab_get_version_from_matlab_run("${Matlab_PROG_VERSION_STRING_AUTO_DETECT}" matlab_list_of_all_versions)

    list(LENGTH matlab_list_of_all_versions list_of_all_versions_length)
    if(list_of_all_versions_length GREATER 0)
      list(GET matlab_list_of_all_versions 0 _matlab_version_tmp)
    else()
      set(_matlab_version_tmp "unknown")
    endif()

    # set the version into the cache
    set(Matlab_VERSION_STRING_INTERNAL ${_matlab_version_tmp} CACHE INTERNAL "Matlab version (automatically determined)" FORCE)

    # warning, just in case several versions found (should not happen)
    if((list_of_all_versions_length GREATER 1) AND MATLAB_FIND_DEBUG)
      message(WARNING "[MATLAB] Found several versions, taking the first one (versions found ${matlab_list_of_all_versions})")
    endif()

    # return the updated value
    set(${matlab_final_version} ${Matlab_VERSION_STRING_INTERNAL} PARENT_SCOPE)
  elseif(EXISTS "${matlab_root}/VersionInfo.xml")
    # MCR
    # we cannot run anything in order to extract the version. We assume that the file
    # VersionInfo.xml exists under the MatlabRoot, we look for it and extract the version from there
    set(_matlab_version_tmp "unknown")
    file(STRINGS "${matlab_root}/VersionInfo.xml" versioninfo_string NEWLINE_CONSUME)

    if(versioninfo_string)
      # parses "<version>9.2.0.538062</version>"
      string(REGEX MATCH "<version>(.*)</version>"
             version_reg_match
             ${versioninfo_string}
            )

      if(CMAKE_MATCH_1 MATCHES "(([0-9])\\.([0-9]))[\\.0-9]*")
        set(_matlab_version_tmp "${CMAKE_MATCH_1}")
      endif()
    endif()
    set(${matlab_final_version} "${_matlab_version_tmp}" PARENT_SCOPE)
    set(Matlab_VERSION_STRING_INTERNAL
        "${_matlab_version_tmp}"
        CACHE INTERNAL "Matlab (MCR) version (automatically determined)"
        FORCE)
  endif() # Matlab or MCR

endfunction()


# Utility function for finding Matlab or MCR on Win32
function(_Matlab_find_instances_win32 matlab_roots)
  # On WIN32, we look for Matlab installation in the registry
  # if unsuccessful, we look for all known revision and filter the existing
  # ones.

  # testing if we are able to extract the needed information from the registry
  set(_matlab_versions_from_registry)

  if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(_matlab_win64 ON)
  else()
    set(_matlab_win64 OFF)
  endif()

  matlab_extract_all_installed_versions_from_registry(_matlab_win64 _matlab_versions_from_registry)

  # the returned list is empty, doing the search on all known versions
  if(NOT _matlab_versions_from_registry)
    if(MATLAB_FIND_DEBUG)
      message(STATUS "[MATLAB] Search for Matlab from the registry unsuccessful, testing all supported versions")
    endif()
    extract_matlab_versions_from_registry_brute_force(_matlab_versions_from_registry)
  endif()

  # filtering the results with the registry keys
  matlab_get_all_valid_matlab_roots_from_registry("${_matlab_versions_from_registry}" _matlab_possible_roots)
  set(${matlab_roots} ${_matlab_possible_roots} PARENT_SCOPE)

endfunction()

# Utility function for finding Matlab or MCR on OSX
function(_Matlab_find_instances_osx matlab_roots)

  set(_matlab_possible_roots)
  # on mac, we look for the /Application paths
  # this corresponds to the behaviour on Windows. On Linux, we do not have
  # any other guess.
  matlab_get_supported_releases(_matlab_releases)
  if(MATLAB_FIND_DEBUG)
    message(STATUS "[MATLAB] Matlab supported versions ${_matlab_releases}. If more version should be supported "
                 "the variable MATLAB_ADDITIONAL_VERSIONS can be set according to the documentation")
  endif()

  foreach(_matlab_current_release IN LISTS _matlab_releases)
    matlab_get_version_from_release_name("${_matlab_current_release}" _matlab_current_version)
    string(REPLACE "." "" _matlab_current_version_without_dot "${_matlab_current_version}")
    set(_matlab_base_path "/Applications/MATLAB_${_matlab_current_release}.app")

    # Check Matlab, has precedence over MCR
    if(EXISTS ${_matlab_base_path})
      if(MATLAB_FIND_DEBUG)
        message(STATUS "[MATLAB] Found version ${_matlab_current_release} (${_matlab_current_version}) in ${_matlab_base_path}")
      endif()
      list(APPEND _matlab_possible_roots "MATLAB" ${_matlab_current_version} ${_matlab_base_path})
    endif()

    # Checks MCR
    set(_mcr_path "/Applications/MATLAB/MATLAB_Runtime/v${_matlab_current_version_without_dot}")
    if(EXISTS "${_mcr_path}")
      if(MATLAB_FIND_DEBUG)
        message(STATUS "[MATLAB] Found MCR version ${_matlab_current_release} (${_matlab_current_version}) in ${_mcr_path}")
      endif()
      list(APPEND _matlab_possible_roots "MCR" ${_matlab_current_version} ${_mcr_path})
    endif()

  endforeach()
  set(${matlab_roots} ${_matlab_possible_roots} PARENT_SCOPE)

endfunction()

# Utility function for finding Matlab or MCR from the PATH
function(_Matlab_find_instances_from_path matlab_roots)

  set(_matlab_possible_roots)

  # At this point, we have no other choice than trying to find it from PATH.
  # If set by the user, this wont change
  find_program(
    _matlab_main_tmp
    NAMES matlab)

  if(_matlab_main_tmp)
    # we then populate the list of roots, with empty version
    if(MATLAB_FIND_DEBUG)
      message(STATUS "[MATLAB] matlab found from PATH: ${_matlab_main_tmp}")
    endif()

    # resolve symlinks
    get_filename_component(_matlab_current_location "${_matlab_main_tmp}" REALPATH)

    # get the directory (the command below has to be run twice)
    # this will be the matlab root
    get_filename_component(_matlab_current_location "${_matlab_current_location}" DIRECTORY)
    get_filename_component(_matlab_current_location "${_matlab_current_location}" DIRECTORY) # Matlab should be in bin

    # We found the Matlab program
    list(APPEND _matlab_possible_roots "MATLAB" "NOTFOUND" ${_matlab_current_location})

    # we remove this from the CACHE
    unset(_matlab_main_tmp CACHE)
  else()
    find_program(
      _matlab_mex_tmp
      NAMES mex)
    if(_matlab_mex_tmp)
      # we then populate the list of roots, with empty version
      if(MATLAB_FIND_DEBUG)
        message(STATUS "[MATLAB] mex compiler found from PATH: ${_matlab_mex_tmp}")
      endif()

      # resolve symlinks
      get_filename_component(_mex_current_location "${_matlab_mex_tmp}" REALPATH)

      # get the directory (the command below has to be run twice)
      # this will be the matlab root
      get_filename_component(_mex_current_location "${_mex_current_location}" DIRECTORY)
      get_filename_component(_mex_current_location "${_mex_current_location}" DIRECTORY) # Matlab Runtime mex compiler should be in bin

      # We found the Matlab program
      list(APPEND _matlab_possible_roots "MCR" "NOTFOUND" ${_mex_current_location})

      unset(_matlab_mex_tmp CACHE)
    else()
      if(MATLAB_FIND_DEBUG)
        message(STATUS "[MATLAB] mex compiler not found")
      endif()
    endif()


  endif()

  set(${matlab_roots} ${_matlab_possible_roots} PARENT_SCOPE)
endfunction()


# ###################################
# Exploring the possible Matlab_ROOTS

# this variable will get all Matlab installations found in the current system.
set(_matlab_possible_roots)

if(Matlab_ROOT_DIR)
  # if the user specifies a possible root, we keep this one

  if(NOT EXISTS "${Matlab_ROOT_DIR}")
    # if Matlab_ROOT_DIR specified but erroneous
    if(MATLAB_FIND_DEBUG)
      message(WARNING "[MATLAB] the specified path for Matlab_ROOT_DIR does not exist (${Matlab_ROOT_DIR})")
    endif()
  else()
    # NOTFOUND indicates the code below to search for the version automatically
    if("${Matlab_VERSION_STRING_INTERNAL}" STREQUAL "")
      list(APPEND _matlab_possible_roots "UNKNOWN" "NOTFOUND" ${Matlab_ROOT_DIR}) # empty version, empty MCR/Matlab indication
    else()
      list(APPEND _matlab_possible_roots "UNKNOWN" ${Matlab_VERSION_STRING_INTERNAL} ${Matlab_ROOT_DIR}) # cached version
    endif()
  endif()
else()

  # if the user does not specify the possible installation root, we look for
  # one installation using the appropriate heuristics.
  # There is apparently no standard way on Linux.
  if(CMAKE_HOST_WIN32)
    _Matlab_find_instances_win32(_matlab_possible_roots_win32)
    list(APPEND _matlab_possible_roots ${_matlab_possible_roots_win32})
  elseif(APPLE)
    _Matlab_find_instances_osx(_matlab_possible_roots_osx)
    list(APPEND _matlab_possible_roots ${_matlab_possible_roots_osx})
  endif()
endif()


list(LENGTH _matlab_possible_roots _numbers_of_matlab_roots)
if(_numbers_of_matlab_roots EQUAL 0)
  # if we have not found anything, we fall back on the PATH
  _Matlab_find_instances_from_path(_matlab_possible_roots)
endif()


if(MATLAB_FIND_DEBUG)
  message(STATUS "[MATLAB] Matlab root folders are ${_matlab_possible_roots}")
endif()





# take the first possible Matlab root
list(LENGTH _matlab_possible_roots _numbers_of_matlab_roots)
set(Matlab_VERSION_STRING "NOTFOUND")
set(Matlab_Or_MCR "UNKNOWN")
if(_numbers_of_matlab_roots GREATER 0)
  if(Matlab_FIND_VERSION_EXACT)
    list(FIND _matlab_possible_roots ${Matlab_FIND_VERSION} _list_index)
    if(_list_index LESS 0)
      set(_list_index 1)
    endif()

    math(EXPR _matlab_or_mcr_index "${_list_index} - 1")
    math(EXPR _matlab_root_dir_index "${_list_index} + 1")

    list(GET _matlab_possible_roots ${_matlab_or_mcr_index} Matlab_Or_MCR)
    list(GET _matlab_possible_roots ${_list_index} Matlab_VERSION_STRING)
    list(GET _matlab_possible_roots ${_matlab_root_dir_index} Matlab_ROOT_DIR)
  else()
    list(GET _matlab_possible_roots 0 Matlab_Or_MCR)
    list(GET _matlab_possible_roots 1 Matlab_VERSION_STRING)
    list(GET _matlab_possible_roots 2 Matlab_ROOT_DIR)

    # adding a warning in case of ambiguity
    if(_numbers_of_matlab_roots GREATER 3 AND MATLAB_FIND_DEBUG)
      message(WARNING "[MATLAB] Found several distributions of Matlab. Setting the current version to ${Matlab_VERSION_STRING} (located ${Matlab_ROOT_DIR})."
                      " If this is not the desired behaviour, use the EXACT keyword or provide the -DMatlab_ROOT_DIR=... on the command line")
    endif()
  endif()
endif()


# check if the root changed wrt. the previous defined one, if so
# clear all the cached variables for being able to reconfigure properly
if(DEFINED Matlab_ROOT_DIR_LAST_CACHED)

  if(NOT Matlab_ROOT_DIR_LAST_CACHED STREQUAL Matlab_ROOT_DIR)
    set(_Matlab_cached_vars
        Matlab_VERSION_STRING
        Matlab_INCLUDE_DIRS
        Matlab_MEX_LIBRARY
        Matlab_MEX_COMPILER
        Matlab_MCC_COMPILER
        Matlab_MAIN_PROGRAM
        Matlab_MX_LIBRARY
        Matlab_ENG_LIBRARY
        Matlab_MAT_LIBRARY
        Matlab_ENGINE_LIBRARY
        Matlab_DATAARRAY_LIBRARY
        Matlab_MEX_EXTENSION
        Matlab_SIMULINK_INCLUDE_DIR

        # internal
        Matlab_MEXEXTENSIONS_PROG
        Matlab_ROOT_DIR_LAST_CACHED
        #Matlab_PROG_VERSION_STRING_AUTO_DETECT
        #Matlab_VERSION_STRING_INTERNAL
        )
    foreach(_var IN LISTS _Matlab_cached_vars)
      if(DEFINED ${_var})
        unset(${_var} CACHE)
      endif()
    endforeach()
  endif()
endif()

set(Matlab_ROOT_DIR_LAST_CACHED ${Matlab_ROOT_DIR} CACHE INTERNAL "last Matlab root dir location")
set(Matlab_ROOT_DIR ${Matlab_ROOT_DIR} CACHE PATH "Matlab installation root path" FORCE)

# Fix the version, in case this one is NOTFOUND
_Matlab_get_version_from_root(
  "${Matlab_ROOT_DIR}"
  "${Matlab_Or_MCR}"
  ${Matlab_VERSION_STRING}
  Matlab_VERSION_STRING
)

if(MATLAB_FIND_DEBUG)
  message(STATUS "[MATLAB] Current version is ${Matlab_VERSION_STRING} located ${Matlab_ROOT_DIR}")
endif()

# MATLAB 9.4 (R2018a) and newer have a new C++ API
# This API pulls additional required libraries.
if(NOT ${Matlab_VERSION_STRING} VERSION_LESS "9.4")
  set(Matlab_HAS_CPP_API 1)
endif()

if(Matlab_ROOT_DIR)
  file(TO_CMAKE_PATH ${Matlab_ROOT_DIR} Matlab_ROOT_DIR)
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 4)
  set(_matlab_64Build FALSE)
else()
  set(_matlab_64Build TRUE)
endif()

if(APPLE)
  set(_matlab_bin_prefix "mac") # i should be for intel
  set(_matlab_bin_suffix_32bits "i")
  set(_matlab_bin_suffix_64bits "i64")
elseif(UNIX)
  set(_matlab_bin_prefix "gln")
  set(_matlab_bin_suffix_32bits "x86")
  set(_matlab_bin_suffix_64bits "xa64")
else()
  set(_matlab_bin_prefix "win")
  set(_matlab_bin_suffix_32bits "32")
  set(_matlab_bin_suffix_64bits "64")
endif()



set(MATLAB_INCLUDE_DIR_TO_LOOK ${Matlab_ROOT_DIR}/extern/include)
if(_matlab_64Build)
  set(_matlab_current_suffix ${_matlab_bin_suffix_64bits})
else()
  set(_matlab_current_suffix ${_matlab_bin_suffix_32bits})
endif()

set(Matlab_BINARIES_DIR
    ${Matlab_ROOT_DIR}/bin/${_matlab_bin_prefix}${_matlab_current_suffix})
set(Matlab_EXTERN_LIBRARY_DIR
    ${Matlab_ROOT_DIR}/extern/lib/${_matlab_bin_prefix}${_matlab_current_suffix})
set(Matlab_EXTERN_BINARIES_DIR
    ${Matlab_ROOT_DIR}/extern/bin/${_matlab_bin_prefix}${_matlab_current_suffix})

if(WIN32)
  if(MINGW)
    set(_matlab_lib_dir_for_search ${Matlab_EXTERN_LIBRARY_DIR}/mingw64)
  else()
    set(_matlab_lib_dir_for_search ${Matlab_EXTERN_LIBRARY_DIR}/microsoft)
  endif()
  set(_matlab_lib_prefix_for_search "lib")
else()
  set(_matlab_lib_dir_for_search ${Matlab_BINARIES_DIR} ${Matlab_EXTERN_BINARIES_DIR})
  set(_matlab_lib_prefix_for_search "lib")
endif()

unset(_matlab_64Build)


if(NOT DEFINED Matlab_MEX_EXTENSION)
  set(_matlab_mex_extension "")
  matlab_get_mex_suffix("${Matlab_ROOT_DIR}" _matlab_mex_extension)

  # This variable goes to the cache.
  set(Matlab_MEX_EXTENSION ${_matlab_mex_extension} CACHE STRING "Extensions for the mex targets (automatically given by Matlab)")
  unset(_matlab_mex_extension)
endif()


if(MATLAB_FIND_DEBUG)
  message(STATUS "[MATLAB] [DEBUG]_matlab_lib_prefix_for_search = ${_matlab_lib_prefix_for_search} | _matlab_lib_dir_for_search = ${_matlab_lib_dir_for_search}")
endif()



# internal
# This small stub around find_library is to prevent any pollution of CMAKE_FIND_LIBRARY_PREFIXES in the global scope.
# This is the function to be used below instead of the find_library directives.
function(_Matlab_find_library _matlab_library_prefix)
  set(CMAKE_FIND_LIBRARY_PREFIXES ${CMAKE_FIND_LIBRARY_PREFIXES} ${_matlab_library_prefix})
  find_library(${ARGN})
endfunction()


set(_matlab_required_variables)

# Order is as follow:
# - unconditionally required libraries/headers first
# - then library components
# - then program components

# the MEX library/header are required
find_path(
  Matlab_INCLUDE_DIRS
  mex.h
  PATHS ${MATLAB_INCLUDE_DIR_TO_LOOK}
  NO_DEFAULT_PATH
  )
list(APPEND _matlab_required_variables Matlab_INCLUDE_DIRS)

_Matlab_find_library(
  ${_matlab_lib_prefix_for_search}
  Matlab_MEX_LIBRARY
  mex
  PATHS ${_matlab_lib_dir_for_search}
  NO_DEFAULT_PATH
)
list(APPEND _matlab_required_variables Matlab_MEX_LIBRARY)

# the MEX extension is required
list(APPEND _matlab_required_variables Matlab_MEX_EXTENSION)

# the matlab root is required
list(APPEND _matlab_required_variables Matlab_ROOT_DIR)

# The MX library is required
_Matlab_find_library(
  ${_matlab_lib_prefix_for_search}
  Matlab_MX_LIBRARY
  mx
  PATHS ${_matlab_lib_dir_for_search}
  NO_DEFAULT_PATH
)
list(APPEND _matlab_required_variables Matlab_MX_LIBRARY)
if(Matlab_MX_LIBRARY)
  set(Matlab_MX_LIBRARY_FOUND TRUE)
endif()

if(Matlab_HAS_CPP_API)

  # The MatlabEngine library is required for R2018a+
  _Matlab_find_library(
    ${_matlab_lib_prefix_for_search}
    Matlab_ENGINE_LIBRARY
    MatlabEngine
    PATHS ${_matlab_lib_dir_for_search}
    DOC "MatlabEngine Library"
    NO_DEFAULT_PATH
  )
  list(APPEND _matlab_required_variables Matlab_ENGINE_LIBRARY)
  if(Matlab_ENGINE_LIBRARY)
    set(Matlab_ENGINE_LIBRARY_FOUND TRUE)
  endif()

  # The MatlabDataArray library is required for R2018a+
  _Matlab_find_library(
    ${_matlab_lib_prefix_for_search}
    Matlab_DATAARRAY_LIBRARY
    MatlabDataArray
    PATHS ${_matlab_lib_dir_for_search}
    DOC "MatlabDataArray Library"
    NO_DEFAULT_PATH
  )
  list(APPEND _matlab_required_variables Matlab_DATAARRAY_LIBRARY)
  if(Matlab_DATAARRAY_LIBRARY)
    set(Matlab_DATAARRAY_LIBRARY_FOUND TRUE)
  endif()

endif()

# Component ENG library
if("ENG_LIBRARY" IN_LIST Matlab_FIND_COMPONENTS)
  _Matlab_find_library(
    ${_matlab_lib_prefix_for_search}
    Matlab_ENG_LIBRARY
    eng
    PATHS ${_matlab_lib_dir_for_search}
    NO_DEFAULT_PATH
  )
  if(Matlab_ENG_LIBRARY)
    set(Matlab_ENG_LIBRARY_FOUND TRUE)
  endif()
endif()

# Component MAT library
if("MAT_LIBRARY" IN_LIST Matlab_FIND_COMPONENTS)
  _Matlab_find_library(
    ${_matlab_lib_prefix_for_search}
    Matlab_MAT_LIBRARY
    mat
    PATHS ${_matlab_lib_dir_for_search}
    NO_DEFAULT_PATH
  )
  if(Matlab_MAT_LIBRARY)
    set(Matlab_MAT_LIBRARY_FOUND TRUE)
  endif()
endif()

# Component Simulink
if("SIMULINK" IN_LIST Matlab_FIND_COMPONENTS)
  find_path(
    Matlab_SIMULINK_INCLUDE_DIR
    simstruc.h
    PATHS "${Matlab_ROOT_DIR}/simulink/include"
    NO_DEFAULT_PATH
    )
  if(Matlab_SIMULINK_INCLUDE_DIR)
    set(Matlab_SIMULINK_FOUND TRUE)
    list(APPEND Matlab_INCLUDE_DIRS "${Matlab_SIMULINK_INCLUDE_DIR}")
  endif()
endif()

# component Matlab program
if("MAIN_PROGRAM" IN_LIST Matlab_FIND_COMPONENTS)
  find_program(
    Matlab_MAIN_PROGRAM
    matlab
    PATHS ${Matlab_ROOT_DIR} ${Matlab_ROOT_DIR}/bin
    DOC "Matlab main program"
    NO_DEFAULT_PATH
  )
  if(Matlab_MAIN_PROGRAM)
    set(Matlab_MAIN_PROGRAM_FOUND TRUE)
  endif()
endif()

# component Mex Compiler
if("MEX_COMPILER" IN_LIST Matlab_FIND_COMPONENTS)
  find_program(
    Matlab_MEX_COMPILER
    "mex"
    PATHS ${Matlab_BINARIES_DIR}
    DOC "Matlab MEX compiler"
    NO_DEFAULT_PATH
  )
  if(Matlab_MEX_COMPILER)
    set(Matlab_MEX_COMPILER_FOUND TRUE)
  endif()
endif()

# component MCC Compiler
if("MCC_COMPILER" IN_LIST Matlab_FIND_COMPONENTS)
  find_program(
    Matlab_MCC_COMPILER
    "mcc"
    PATHS ${Matlab_BINARIES_DIR}
    DOC "Matlab MCC compiler"
    NO_DEFAULT_PATH
  )
  if(Matlab_MCC_COMPILER)
    set(Matlab_MCC_COMPILER_FOUND TRUE)
  endif()
endif()

set(Matlab_LIBRARIES
  ${Matlab_MEX_LIBRARY} ${Matlab_MX_LIBRARY}
  ${Matlab_ENG_LIBRARY} ${Matlab_MAT_LIBRARY}
  ${Matlab_DATAARRAY_LIBRARY} ${Matlab_ENGINE_LIBRARY})

find_package_handle_standard_args(
  Matlab
  FOUND_VAR Matlab_FOUND
  REQUIRED_VARS ${_matlab_required_variables}
  VERSION_VAR Matlab_VERSION_STRING
  HANDLE_COMPONENTS)

unset(_matlab_required_variables)
unset(_matlab_bin_prefix)
unset(_matlab_bin_suffix_32bits)
unset(_matlab_bin_suffix_64bits)
unset(_matlab_current_suffix)
unset(_matlab_lib_dir_for_search)
unset(_matlab_lib_prefix_for_search)

if(Matlab_INCLUDE_DIRS AND Matlab_LIBRARIES)
  mark_as_advanced(
    Matlab_MEX_LIBRARY
    Matlab_MX_LIBRARY
    Matlab_ENG_LIBRARY
    Matlab_ENGINE_LIBRARY
    Matlab_DATAARRAY_LIBRARY
    Matlab_MAT_LIBRARY
    Matlab_INCLUDE_DIRS
    Matlab_FOUND
    Matlab_MAIN_PROGRAM
    Matlab_MEXEXTENSIONS_PROG
    Matlab_MEX_EXTENSION
  )
endif()

cmake_policy(POP)
