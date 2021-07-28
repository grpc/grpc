# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
Dart
----

Configure a project for testing with CTest or old Dart Tcl Client

This file is the backwards-compatibility version of the CTest module.
It supports using the old Dart 1 Tcl client for driving dashboard
submissions as well as testing with CTest.  This module should be
included in the CMakeLists.txt file at the top of a project.  Typical
usage:

::

  include(Dart)
  if(BUILD_TESTING)
    # ... testing related CMake code ...
  endif()

The BUILD_TESTING option is created by the Dart module to determine
whether testing support should be enabled.  The default is ON.
#]=======================================================================]

# This file configures a project to use the Dart testing/dashboard process.
# It is broken into 3 sections.
#
#  Section #1: Locate programs on the client and determine site and build name
#  Section #2: Configure or copy Tcl scripts from the source tree to build tree
#  Section #3: Custom targets for performing dashboard builds.
#
#

option(BUILD_TESTING "Build the testing tree." ON)

if(BUILD_TESTING)
  find_package(Dart QUIET)

  #
  # Section #1:
  #
  # CMake commands that will not vary from project to project. Locates programs
  # on the client and configure site name and build name.
  #

  set(RUN_FROM_DART 1)
  include(CTest)
  set(RUN_FROM_DART)

  find_program(COMPRESSIONCOMMAND NAMES gzip compress zip
    DOC "Path to program used to compress files for transfer to the dart server")
  find_program(GUNZIPCOMMAND gunzip DOC "Path to gunzip executable")
  find_program(JAVACOMMAND java DOC "Path to java command, used by the Dart server to create html.")
  option(DART_VERBOSE_BUILD "Show the actual output of the build, or if off show a . for each 1024 bytes."
    OFF)
  option(DART_BUILD_ERROR_REPORT_LIMIT "Limit of reported errors, -1 reports all." -1 )
  option(DART_BUILD_WARNING_REPORT_LIMIT "Limit of reported warnings, -1 reports all." -1 )

  set(VERBOSE_BUILD ${DART_VERBOSE_BUILD})
  set(BUILD_ERROR_REPORT_LIMIT ${DART_BUILD_ERROR_REPORT_LIMIT})
  set(BUILD_WARNING_REPORT_LIMIT ${DART_BUILD_WARNING_REPORT_LIMIT})
  set (DELIVER_CONTINUOUS_EMAIL "Off" CACHE BOOL "Should Dart server send email when build errors are found in Continuous builds?")

  mark_as_advanced(
    COMPRESSIONCOMMAND
    DART_BUILD_ERROR_REPORT_LIMIT
    DART_BUILD_WARNING_REPORT_LIMIT
    DART_TESTING_TIMEOUT
    DART_VERBOSE_BUILD
    DELIVER_CONTINUOUS_EMAIL
    GUNZIPCOMMAND
    JAVACOMMAND
    )

  set(HAVE_DART)
  if(EXISTS "${DART_ROOT}/Source/Client/Dart.conf.in")
    set(HAVE_DART 1)
  endif()

  #
  # Section #2:
  #
  # Make necessary directories and configure testing scripts
  #
  # find a tcl shell command
  if(HAVE_DART)
    find_package(Tclsh)
  endif()


  if (HAVE_DART)
    # make directories in the binary tree
    file(MAKE_DIRECTORY "${PROJECT_BINARY_DIR}/Testing/HTML/TestingResults/Dashboard"
      "${PROJECT_BINARY_DIR}/Testing/HTML/TestingResults/Sites/${SITE}/${BUILDNAME}")

    # configure files
    configure_file(
      "${DART_ROOT}/Source/Client/Dart.conf.in"
      "${PROJECT_BINARY_DIR}/DartConfiguration.tcl" )

    #
    # Section 3:
    #
    # Custom targets to perform dashboard builds and submissions.
    # These should NOT need to be modified from project to project.
    #

    # add testing targets
    set(DART_EXPERIMENTAL_NAME Experimental)
    if(DART_EXPERIMENTAL_USE_PROJECT_NAME)
      string(APPEND DART_EXPERIMENTAL_NAME "${PROJECT_NAME}")
    endif()
  endif ()

  set(RUN_FROM_CTEST_OR_DART 1)
  include(CTestTargets)
  set(RUN_FROM_CTEST_OR_DART)
endif()

#
# End of Dart.cmake
#

