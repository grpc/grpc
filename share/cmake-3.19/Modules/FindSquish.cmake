# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
FindSquish
----------

-- Typical Use



This module can be used to find Squish.

::

  SQUISH_FOUND                    If false, don't try to use Squish
  SQUISH_VERSION                  The full version of Squish found
  SQUISH_VERSION_MAJOR            The major version of Squish found
  SQUISH_VERSION_MINOR            The minor version of Squish found
  SQUISH_VERSION_PATCH            The patch version of Squish found



::

  SQUISH_INSTALL_DIR              The Squish installation directory
                                  (containing bin, lib, etc)
  SQUISH_SERVER_EXECUTABLE        The squishserver executable
  SQUISH_CLIENT_EXECUTABLE        The squishrunner executable



::

  SQUISH_INSTALL_DIR_FOUND        Was the install directory found?
  SQUISH_SERVER_EXECUTABLE_FOUND  Was the server executable found?
  SQUISH_CLIENT_EXECUTABLE_FOUND  Was the client executable found?



It provides the function squish_add_test() for adding a squish test
to cmake using Squish >= 4.x:

::

   squish_add_test(cmakeTestName
     AUT targetName SUITE suiteName TEST squishTestName
     [SETTINGSGROUP group] [PRE_COMMAND command] [POST_COMMAND command] )



The arguments have the following meaning:

``cmakeTestName``
  this will be used as the first argument for add_test()
``AUT targetName``
  the name of the cmake target which will be used as AUT, i.e. the
  executable which will be tested.
``SUITE suiteName``
  this is either the full path to the squish suite, or just the
  last directory of the suite, i.e. the suite name. In this case
  the CMakeLists.txt which calls squish_add_test() must be located
  in the parent directory of the suite directory.
``TEST squishTestName``
  the name of the squish test, i.e. the name of the subdirectory
  of the test inside the suite directory.
``SETTINGSGROUP group``
  deprecated, this argument will be ignored.
``PRE_COMMAND command``
  if specified, the given command will be executed before starting the squish test.
``POST_COMMAND command``
  same as PRE_COMMAND, but after the squish test has been executed.



::

   enable_testing()
   find_package(Squish 6.5)
   if (SQUISH_FOUND)
      squish_add_test(myTestName
        AUT myApp
        SUITE ${CMAKE_SOURCE_DIR}/tests/mySuite
        TEST someSquishTest
        )
   endif ()





For users of Squish version 3.x the macro squish_v3_add_test() is
provided:

::

   squish_v3_add_test(testName applicationUnderTest testCase envVars testWrapper)
   Use this macro to add a test using Squish 3.x.



::

  enable_testing()
  find_package(Squish 3.0)
  if (SQUISH_FOUND)
    squish_v3_add_test(myTestName myApplication testCase envVars testWrapper)
  endif ()


#]=======================================================================]

set(SQUISH_INSTALL_DIR_STRING "Directory containing the bin, doc, and lib directories for Squish; this should be the root of the installation directory.")
set(SQUISH_SERVER_EXECUTABLE_STRING "The squishserver executable program.")
set(SQUISH_CLIENT_EXECUTABLE_STRING "The squishclient executable program.")

# Search only if the location is not already known.
if(NOT SQUISH_INSTALL_DIR)
  # Get the system search path as a list.
  file(TO_CMAKE_PATH "$ENV{PATH}" SQUISH_INSTALL_DIR_SEARCH2)

  # Construct a set of paths relative to the system search path.
  set(SQUISH_INSTALL_DIR_SEARCH "")
  foreach(dir ${SQUISH_INSTALL_DIR_SEARCH2})
    set(SQUISH_INSTALL_DIR_SEARCH ${SQUISH_INSTALL_DIR_SEARCH} "${dir}/../lib/fltk")
  endforeach()
  string(REPLACE "//" "/" SQUISH_INSTALL_DIR_SEARCH "${SQUISH_INSTALL_DIR_SEARCH}")

  # Look for an installation
  find_path(SQUISH_INSTALL_DIR
    NAMES bin/squishrunner bin/squishrunner.exe
    HINTS
    # Look for an environment variable SQUISH_INSTALL_DIR.
      ENV SQUISH_INSTALL_DIR

    # Look in places relative to the system executable search path.
    ${SQUISH_INSTALL_DIR_SEARCH}

    DOC "The ${SQUISH_INSTALL_DIR_STRING}"
    )
endif()

# search for the executables
if(SQUISH_INSTALL_DIR)
  set(SQUISH_INSTALL_DIR_FOUND 1)

  # find the client program
  if(NOT SQUISH_CLIENT_EXECUTABLE)
    find_program(SQUISH_CLIENT_EXECUTABLE ${SQUISH_INSTALL_DIR}/bin/squishrunner${CMAKE_EXECUTABLE_SUFFIX} DOC "The ${SQUISH_CLIENT_EXECUTABLE_STRING}")
  endif()

  # find the server program
  if(NOT SQUISH_SERVER_EXECUTABLE)
    find_program(SQUISH_SERVER_EXECUTABLE ${SQUISH_INSTALL_DIR}/bin/squishserver${CMAKE_EXECUTABLE_SUFFIX} DOC "The ${SQUISH_SERVER_EXECUTABLE_STRING}")
  endif()

else()
  set(SQUISH_INSTALL_DIR_FOUND 0)
endif()


set(SQUISH_VERSION)
set(SQUISH_VERSION_MAJOR)
set(SQUISH_VERSION_MINOR)
set(SQUISH_VERSION_PATCH)

# record if executables are set
if(SQUISH_CLIENT_EXECUTABLE)
  set(SQUISH_CLIENT_EXECUTABLE_FOUND 1)
  execute_process(COMMAND "${SQUISH_CLIENT_EXECUTABLE}" --version
                  OUTPUT_VARIABLE _squishVersionOutput
                  ERROR_QUIET )
  if("${_squishVersionOutput}" MATCHES "([0-9]+)\\.([0-9]+)\\.([0-9]+)")
    set(SQUISH_VERSION_MAJOR "${CMAKE_MATCH_1}")
    set(SQUISH_VERSION_MINOR "${CMAKE_MATCH_2}")
    set(SQUISH_VERSION_PATCH "${CMAKE_MATCH_3}")
    set(SQUISH_VERSION "${SQUISH_VERSION_MAJOR}.${SQUISH_VERSION_MINOR}.${SQUISH_VERSION_PATCH}" )
  endif()
else()
  set(SQUISH_CLIENT_EXECUTABLE_FOUND 0)
endif()

if(SQUISH_SERVER_EXECUTABLE)
  set(SQUISH_SERVER_EXECUTABLE_FOUND 1)
else()
  set(SQUISH_SERVER_EXECUTABLE_FOUND 0)
endif()

# record if Squish was found
include(${CMAKE_CURRENT_LIST_DIR}/FindPackageHandleStandardArgs.cmake)
find_package_handle_standard_args(Squish  REQUIRED_VARS  SQUISH_INSTALL_DIR SQUISH_CLIENT_EXECUTABLE SQUISH_SERVER_EXECUTABLE
                                          VERSION_VAR  SQUISH_VERSION )


set(_SQUISH_MODULE_DIR "${CMAKE_CURRENT_LIST_DIR}")

macro(squish_v3_add_test testName testAUT testCase envVars testWraper)
  if("${SQUISH_VERSION_MAJOR}" STRGREATER "3")
    message(STATUS "Using squish_v3_add_test(), but SQUISH_VERSION_MAJOR is ${SQUISH_VERSION_MAJOR}.\nThis may not work.")
  endif()

  add_test(${testName}
    ${CMAKE_COMMAND} -V -VV
    "-Dsquish_version:STRING=3"
    "-Dsquish_aut:STRING=${testAUT}"
    "-Dsquish_server_executable:STRING=${SQUISH_SERVER_EXECUTABLE}"
    "-Dsquish_client_executable:STRING=${SQUISH_CLIENT_EXECUTABLE}"
    "-Dsquish_libqtdir:STRING=${QT_LIBRARY_DIR}"
    "-Dsquish_test_case:STRING=${testCase}"
    "-Dsquish_env_vars:STRING=${envVars}"
    "-Dsquish_wrapper:STRING=${testWraper}"
    "-Dsquish_module_dir:STRING=${_SQUISH_MODULE_DIR}"
    -P "${_SQUISH_MODULE_DIR}/SquishTestScript.cmake"
    )
  set_tests_properties(${testName}
    PROPERTIES FAIL_REGULAR_EXPRESSION "FAILED;ERROR;FATAL"
    )
endmacro()


function(squish_v4_add_test testName)
  if(NOT "${SQUISH_VERSION_MAJOR}" STRGREATER "3")
    message(STATUS "Using squish_add_test(), but SQUISH_VERSION_MAJOR is ${SQUISH_VERSION_MAJOR}.\nThis may not work.")
  endif()

  set(oneValueArgs AUT SUITE TEST SETTINGSGROUP PRE_COMMAND POST_COMMAND)

  cmake_parse_arguments(_SQUISH "" "${oneValueArgs}" "" ${ARGN} )

  if(_SQUISH_UNPARSED_ARGUMENTS)
    message(FATAL_ERROR "Unknown keywords given to SQUISH_ADD_TEST(): \"${_SQUISH_UNPARSED_ARGUMENTS}\"")
  endif()

  if(NOT _SQUISH_AUT)
    message(FATAL_ERROR "Required argument AUT not given for SQUISH_ADD_TEST()")
  endif()

  if(NOT _SQUISH_SUITE)
    message(FATAL_ERROR "Required argument SUITE not given for SQUISH_ADD_TEST()")
  endif()

  if(NOT _SQUISH_TEST)
    message(FATAL_ERROR "Required argument TEST not given for SQUISH_ADD_TEST()")
  endif()

  get_filename_component(absTestSuite "${_SQUISH_SUITE}" ABSOLUTE)
  if(NOT EXISTS "${absTestSuite}")
    message(FATAL_ERROR "Could not find squish test suite ${_SQUISH_SUITE} (checked ${absTestSuite})")
  endif()

  set(absTestCase "${absTestSuite}/${_SQUISH_TEST}")
  if(NOT EXISTS "${absTestCase}")
    message(FATAL_ERROR "Could not find squish testcase ${_SQUISH_TEST} (checked ${absTestCase})")
  endif()

  if(_SQUISH_SETTINGSGROUP)
    message("SETTINGSGROUP is deprecated and will be ignored.")
  endif()

  add_test(NAME ${testName}
    COMMAND ${CMAKE_COMMAND} -V -VV
    "-Dsquish_version:STRING=4"
    "-Dsquish_aut:STRING=$<TARGET_FILE_BASE_NAME:${_SQUISH_AUT}>"
    "-Dsquish_aut_dir:STRING=$<TARGET_FILE_DIR:${_SQUISH_AUT}>"
    "-Dsquish_server_executable:STRING=${SQUISH_SERVER_EXECUTABLE}"
    "-Dsquish_client_executable:STRING=${SQUISH_CLIENT_EXECUTABLE}"
    "-Dsquish_libqtdir:STRING=${QT_LIBRARY_DIR}"
    "-Dsquish_test_suite:STRING=${absTestSuite}"
    "-Dsquish_test_case:STRING=${_SQUISH_TEST}"
    "-Dsquish_env_vars:STRING=${envVars}"
    "-Dsquish_wrapper:STRING=${testWraper}"
    "-Dsquish_module_dir:STRING=${_SQUISH_MODULE_DIR}"
    "-Dsquish_pre_command:STRING=${_SQUISH_PRE_COMMAND}"
    "-Dsquish_post_command:STRING=${_SQUISH_POST_COMMAND}"
    -P "${_SQUISH_MODULE_DIR}/SquishTestScript.cmake"
    )
  set_tests_properties(${testName}
    PROPERTIES FAIL_REGULAR_EXPRESSION "FAIL;FAILED;ERROR;FATAL"
    )
endfunction()

macro(squish_add_test)
  if("${SQUISH_VERSION_MAJOR}" STRGREATER "3")
    squish_v4_add_test(${ARGV})
  else()
    squish_v3_add_test(${ARGV})
  endif()
endmacro()
