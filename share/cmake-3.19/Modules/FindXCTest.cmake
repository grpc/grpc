# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
FindXCTest
----------

.. versionadded:: 3.3

Functions to help creating and executing XCTest bundles.

An XCTest bundle is a CFBundle with a special product-type
and bundle extension. The Mac Developer Library provides more
information in the `Testing with Xcode`_ document.

.. _Testing with Xcode: http://developer.apple.com/library/mac/documentation/DeveloperTools/Conceptual/testing_with_xcode/

Module Functions
^^^^^^^^^^^^^^^^

.. command:: xctest_add_bundle

  The ``xctest_add_bundle`` function creates a XCTest bundle named
  <target> which will test the target <testee>. Supported target types
  for testee are Frameworks and App Bundles::

    xctest_add_bundle(
      <target>  # Name of the XCTest bundle
      <testee>  # Target name of the testee
      )

.. command:: xctest_add_test

  The ``xctest_add_test`` function adds an XCTest bundle to the
  project to be run by :manual:`ctest(1)`. The test will be named
  <name> and tests <bundle>::

    xctest_add_test(
      <name>    # Test name
      <bundle>  # Target name of XCTest bundle
      )

Module Variables
^^^^^^^^^^^^^^^^

The following variables are set by including this module:

.. variable:: XCTest_FOUND

  True if the XCTest Framework and executable were found.

.. variable:: XCTest_EXECUTABLE

  The path to the xctest command line tool used to execute XCTest bundles.

.. variable:: XCTest_INCLUDE_DIRS

  The directory containing the XCTest Framework headers.

.. variable:: XCTest_LIBRARIES

  The location of the XCTest Framework.

#]=======================================================================]

set(_PRESERVED_CMAKE_FIND_ROOT_PATH "${CMAKE_FIND_ROOT_PATH}")

if(CMAKE_EFFECTIVE_SYSTEM_NAME STREQUAL "Apple"
   AND NOT CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  # Non-macos systems set the CMAKE_FIND_ROOT_PATH_MODE to "ONLY" which
  # restricts the search paths too much to find XCTest.framework. In
  # contrast to the regular system frameworks which reside within the
  # SDK direectory the XCTest framework is located in the respective
  # platform directory which is not added to the CMAKE_FIND_ROOT_PATH
  # (only to CMAKE_SYSTEM_FRAMEWORK_PATH) and therefore not searched.
  #
  # Until this is properly addressed, temporaily add the platform
  # directory to CMAKE_FIND_ROOT_PATH.
  list(APPEND CMAKE_FIND_ROOT_PATH "${_CMAKE_OSX_SYSROOT_PATH}/../..")
endif()

find_path(XCTest_INCLUDE_DIR
  NAMES "XCTest/XCTest.h"
  DOC "XCTest include directory")
mark_as_advanced(XCTest_INCLUDE_DIR)

find_library(XCTest_LIBRARY
  NAMES XCTest
  DOC "XCTest Framework library")
mark_as_advanced(XCTest_LIBRARY)

set(CMAKE_FIND_ROOT_PATH "${_PRESERVED_CMAKE_FIND_ROOT_PATH}")
unset(_PRESERVED_CMAKE_FIND_ROOT_PATH)

execute_process(
  COMMAND xcrun --find xctest
  OUTPUT_VARIABLE _xcrun_out OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_VARIABLE _xcrun_err)
if(_xcrun_out)
  set(XCTest_EXECUTABLE "${_xcrun_out}" CACHE FILEPATH "XCTest executable")
  mark_as_advanced(XCTest_EXECUTABLE)
endif()

include(${CMAKE_CURRENT_LIST_DIR}/FindPackageHandleStandardArgs.cmake)
find_package_handle_standard_args(XCTest
  FOUND_VAR XCTest_FOUND
  REQUIRED_VARS XCTest_LIBRARY XCTest_INCLUDE_DIR XCTest_EXECUTABLE)

if(XCTest_FOUND)
  set(XCTest_INCLUDE_DIRS "${XCTest_INCLUDE_DIR}")
  set(XCTest_LIBRARIES "${XCTest_LIBRARY}")
endif(XCTest_FOUND)


function(xctest_add_bundle target testee)
  if(NOT XCTest_FOUND)
    message(FATAL_ERROR "XCTest is required to create a XCTest Bundle.")
  endif(NOT XCTest_FOUND)

  if(NOT CMAKE_OSX_SYSROOT)
    message(FATAL_ERROR "Adding XCTest bundles requires CMAKE_OSX_SYSROOT to be set.")
  endif()

  add_library(${target} MODULE ${ARGN})

  set_target_properties(${target} PROPERTIES
    BUNDLE TRUE
    XCTEST TRUE
    XCTEST_TESTEE ${testee})

  target_link_libraries(${target} PRIVATE "-framework Foundation")
  target_link_libraries(${target} PRIVATE ${XCTest_LIBRARIES})
  target_include_directories(${target} PRIVATE ${XCTest_INCLUDE_DIRS})

  # retrieve testee target type
  if(NOT TARGET ${testee})
    message(FATAL_ERROR "${testee} is not a target.")
  endif()
  get_property(_testee_type TARGET ${testee} PROPERTY TYPE)
  get_property(_testee_framework TARGET ${testee} PROPERTY FRAMEWORK)
  get_property(_testee_macosx_bundle TARGET ${testee} PROPERTY MACOSX_BUNDLE)

  if(_testee_type STREQUAL "SHARED_LIBRARY" AND _testee_framework)
    # testee is a Framework
    target_link_libraries(${target} PRIVATE ${testee})

  elseif(_testee_type STREQUAL "STATIC_LIBRARY")
    # testee is a static library
    target_link_libraries(${target} PRIVATE ${testee})

  elseif(_testee_type STREQUAL "EXECUTABLE" AND _testee_macosx_bundle)
    # testee is an App Bundle
    add_dependencies(${target} ${testee})
    if(XCODE)
      set_target_properties(${target} PROPERTIES
        XCODE_ATTRIBUTE_BUNDLE_LOADER "$(TEST_HOST)"
        XCODE_ATTRIBUTE_TEST_HOST "$<TARGET_FILE:${testee}>")
      if(XCODE_VERSION VERSION_GREATER_EQUAL 7.3)
        # CMAKE_XCODE_BUILD_SYSTEM equals 12 means that at least Xcode 11.x is used.
        if(CMAKE_XCODE_BUILD_SYSTEM EQUAL 12 AND
           NOT CMAKE_SYSTEM_NAME STREQUAL "Darwin")
          set(_output_directory "$<TARGET_BUNDLE_CONTENT_DIR:${testee}>")
        else()
          set(_output_directory "$<TARGET_BUNDLE_CONTENT_DIR:${testee}>/PlugIns")
        endif()
        set_target_properties(${target} PROPERTIES
          LIBRARY_OUTPUT_DIRECTORY "${_output_directory}")
      endif()
    else(XCODE)
      target_link_libraries(${target}
        PRIVATE -bundle_loader $<TARGET_FILE:${testee}>)
    endif(XCODE)

  else()
    message(FATAL_ERROR "Testee ${testee} is of unsupported type.")
  endif()
endfunction(xctest_add_bundle)


function(xctest_add_test name bundle)
  if(NOT XCTest_EXECUTABLE)
    message(FATAL_ERROR "XCTest executable is required to register a test.")
  endif()

  # check that bundle is a XCTest Bundle

  if(NOT TARGET ${bundle})
    message(FATAL_ERROR "${bundle} is not a target.")
  endif(NOT TARGET ${bundle})

  get_property(_test_type TARGET ${bundle} PROPERTY TYPE)
  get_property(_test_bundle TARGET ${bundle} PROPERTY BUNDLE)
  get_property(_test_xctest TARGET ${bundle} PROPERTY XCTEST)

  if(NOT _test_type STREQUAL "MODULE_LIBRARY"
       OR NOT _test_xctest OR NOT _test_bundle)
    message(FATAL_ERROR "Test ${bundle} is not an XCTest Bundle")
  endif()

  # get and check testee properties

  get_property(_testee TARGET ${bundle} PROPERTY XCTEST_TESTEE)
  if(NOT TARGET ${_testee})
    message(FATAL_ERROR "${_testee} is not a target.")
  endif()

  get_property(_testee_type TARGET ${_testee} PROPERTY TYPE)
  get_property(_testee_framework TARGET ${_testee} PROPERTY FRAMEWORK)

  # register test

  add_test(
    NAME ${name}
    COMMAND ${XCTest_EXECUTABLE} $<TARGET_BUNDLE_DIR:${bundle}>)

  # point loader to testee in case rpath is disabled

  if(_testee_type STREQUAL "SHARED_LIBRARY" AND _testee_framework)
    set_property(TEST ${name} APPEND PROPERTY
      ENVIRONMENT DYLD_FRAMEWORK_PATH=$<TARGET_LINKER_FILE_DIR:${_testee}>/..)
  endif()
endfunction(xctest_add_test)
