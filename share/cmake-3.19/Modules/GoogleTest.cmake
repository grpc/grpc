# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
GoogleTest
----------

.. versionadded:: 3.9

This module defines functions to help use the Google Test infrastructure.  Two
mechanisms for adding tests are provided. :command:`gtest_add_tests` has been
around for some time, originally via ``find_package(GTest)``.
:command:`gtest_discover_tests` was introduced in CMake 3.10.

The (older) :command:`gtest_add_tests` scans source files to identify tests.
This is usually effective, with some caveats, including in cross-compiling
environments, and makes setting additional properties on tests more convenient.
However, its handling of parameterized tests is less comprehensive, and it
requires re-running CMake to detect changes to the list of tests.

The (newer) :command:`gtest_discover_tests` discovers tests by asking the
compiled test executable to enumerate its tests.  This is more robust and
provides better handling of parameterized tests, and does not require CMake
to be re-run when tests change.  However, it may not work in a cross-compiling
environment, and setting test properties is less convenient.

More details can be found in the documentation of the respective functions.

Both commands are intended to replace use of :command:`add_test` to register
tests, and will create a separate CTest test for each Google Test test case.
Note that this is in some cases less efficient, as common set-up and tear-down
logic cannot be shared by multiple test cases executing in the same instance.
However, it provides more fine-grained pass/fail information to CTest, which is
usually considered as more beneficial.  By default, the CTest test name is the
same as the Google Test name (i.e. ``suite.testcase``); see also
``TEST_PREFIX`` and ``TEST_SUFFIX``.

.. command:: gtest_add_tests

  Automatically add tests with CTest by scanning source code for Google Test
  macros::

    gtest_add_tests(TARGET target
                    [SOURCES src1...]
                    [EXTRA_ARGS arg1...]
                    [WORKING_DIRECTORY dir]
                    [TEST_PREFIX prefix]
                    [TEST_SUFFIX suffix]
                    [SKIP_DEPENDENCY]
                    [TEST_LIST outVar]
    )

  ``gtest_add_tests`` attempts to identify tests by scanning source files.
  Although this is generally effective, it uses only a basic regular expression
  match, which can be defeated by atypical test declarations, and is unable to
  fully "split" parameterized tests.  Additionally, it requires that CMake be
  re-run to discover any newly added, removed or renamed tests (by default,
  this means that CMake is re-run when any test source file is changed, but see
  ``SKIP_DEPENDENCY``).  However, it has the advantage of declaring tests at
  CMake time, which somewhat simplifies setting additional properties on tests,
  and always works in a cross-compiling environment.

  The options are:

  ``TARGET target``
    Specifies the Google Test executable, which must be a known CMake
    executable target.  CMake will substitute the location of the built
    executable when running the test.

  ``SOURCES src1...``
    When provided, only the listed files will be scanned for test cases.  If
    this option is not given, the :prop_tgt:`SOURCES` property of the
    specified ``target`` will be used to obtain the list of sources.

  ``EXTRA_ARGS arg1...``
    Any extra arguments to pass on the command line to each test case.

  ``WORKING_DIRECTORY dir``
    Specifies the directory in which to run the discovered test cases.  If this
    option is not provided, the current binary directory is used.

  ``TEST_PREFIX prefix``
    Specifies a ``prefix`` to be prepended to the name of each discovered test
    case.  This can be useful when the same source files are being used in
    multiple calls to ``gtest_add_test()`` but with different ``EXTRA_ARGS``.

  ``TEST_SUFFIX suffix``
    Similar to ``TEST_PREFIX`` except the ``suffix`` is appended to the name of
    every discovered test case.  Both ``TEST_PREFIX`` and ``TEST_SUFFIX`` may
    be specified.

  ``SKIP_DEPENDENCY``
    Normally, the function creates a dependency which will cause CMake to be
    re-run if any of the sources being scanned are changed.  This is to ensure
    that the list of discovered tests is updated.  If this behavior is not
    desired (as may be the case while actually writing the test cases), this
    option can be used to prevent the dependency from being added.

  ``TEST_LIST outVar``
    The variable named by ``outVar`` will be populated in the calling scope
    with the list of discovered test cases.  This allows the caller to do
    things like manipulate test properties of the discovered tests.

  .. code-block:: cmake

    include(GoogleTest)
    add_executable(FooTest FooUnitTest.cxx)
    gtest_add_tests(TARGET      FooTest
                    TEST_SUFFIX .noArgs
                    TEST_LIST   noArgsTests
    )
    gtest_add_tests(TARGET      FooTest
                    EXTRA_ARGS  --someArg someValue
                    TEST_SUFFIX .withArgs
                    TEST_LIST   withArgsTests
    )
    set_tests_properties(${noArgsTests}   PROPERTIES TIMEOUT 10)
    set_tests_properties(${withArgsTests} PROPERTIES TIMEOUT 20)

  For backward compatibility, the following form is also supported::

    gtest_add_tests(exe args files...)

  ``exe``
    The path to the test executable or the name of a CMake target.
  ``args``
    A ;-list of extra arguments to be passed to executable.  The entire
    list must be passed as a single argument.  Enclose it in quotes,
    or pass ``""`` for no arguments.
  ``files...``
    A list of source files to search for tests and test fixtures.
    Alternatively, use ``AUTO`` to specify that ``exe`` is the name
    of a CMake executable target whose sources should be scanned.

  .. code-block:: cmake

    include(GoogleTest)
    set(FooTestArgs --foo 1 --bar 2)
    add_executable(FooTest FooUnitTest.cxx)
    gtest_add_tests(FooTest "${FooTestArgs}" AUTO)

.. command:: gtest_discover_tests

  Automatically add tests with CTest by querying the compiled test executable
  for available tests::

    gtest_discover_tests(target
                         [EXTRA_ARGS arg1...]
                         [WORKING_DIRECTORY dir]
                         [TEST_PREFIX prefix]
                         [TEST_SUFFIX suffix]
                         [NO_PRETTY_TYPES] [NO_PRETTY_VALUES]
                         [PROPERTIES name1 value1...]
                         [TEST_LIST var]
                         [DISCOVERY_TIMEOUT seconds]
                         [XML_OUTPUT_DIR dir]
                         [DISCOVERY_MODE <POST_BUILD|PRE_TEST>]
    )

  ``gtest_discover_tests()`` sets up a post-build command on the test executable
  that generates the list of tests by parsing the output from running the test
  with the ``--gtest_list_tests`` argument.  Compared to the source parsing
  approach of :command:`gtest_add_tests`, this ensures that the full list of
  tests, including instantiations of parameterized tests, is obtained.  Since
  test discovery occurs at build time, it is not necessary to re-run CMake when
  the list of tests changes.
  However, it requires that :prop_tgt:`CROSSCOMPILING_EMULATOR` is properly set
  in order to function in a cross-compiling environment.

  Additionally, setting properties on tests is somewhat less convenient, since
  the tests are not available at CMake time.  Additional test properties may be
  assigned to the set of tests as a whole using the ``PROPERTIES`` option.  If
  more fine-grained test control is needed, custom content may be provided
  through an external CTest script using the :prop_dir:`TEST_INCLUDE_FILES`
  directory property.  The set of discovered tests is made accessible to such a
  script via the ``<target>_TESTS`` variable.

  The options are:

  ``target``
    Specifies the Google Test executable, which must be a known CMake
    executable target.  CMake will substitute the location of the built
    executable when running the test.

  ``EXTRA_ARGS arg1...``
    Any extra arguments to pass on the command line to each test case.

  ``WORKING_DIRECTORY dir``
    Specifies the directory in which to run the discovered test cases.  If this
    option is not provided, the current binary directory is used.

  ``TEST_PREFIX prefix``
    Specifies a ``prefix`` to be prepended to the name of each discovered test
    case.  This can be useful when the same test executable is being used in
    multiple calls to ``gtest_discover_tests()`` but with different
    ``EXTRA_ARGS``.

  ``TEST_SUFFIX suffix``
    Similar to ``TEST_PREFIX`` except the ``suffix`` is appended to the name of
    every discovered test case.  Both ``TEST_PREFIX`` and ``TEST_SUFFIX`` may
    be specified.

  ``NO_PRETTY_TYPES``
    By default, the type index of type-parameterized tests is replaced by the
    actual type name in the CTest test name.  If this behavior is undesirable
    (e.g. because the type names are unwieldy), this option will suppress this
    behavior.

  ``NO_PRETTY_VALUES``
    By default, the value index of value-parameterized tests is replaced by the
    actual value in the CTest test name.  If this behavior is undesirable
    (e.g. because the value strings are unwieldy), this option will suppress
    this behavior.

  ``PROPERTIES name1 value1...``
    Specifies additional properties to be set on all tests discovered by this
    invocation of ``gtest_discover_tests()``.

  ``TEST_LIST var``
    Make the list of tests available in the variable ``var``, rather than the
    default ``<target>_TESTS``.  This can be useful when the same test
    executable is being used in multiple calls to ``gtest_discover_tests()``.
    Note that this variable is only available in CTest.

  ``DISCOVERY_TIMEOUT num``
    Specifies how long (in seconds) CMake will wait for the test to enumerate
    available tests.  If the test takes longer than this, discovery (and your
    build) will fail.  Most test executables will enumerate their tests very
    quickly, but under some exceptional circumstances, a test may require a
    longer timeout.  The default is 5.  See also the ``TIMEOUT`` option of
    :command:`execute_process`.

    .. note::

      In CMake versions 3.10.1 and 3.10.2, this option was called ``TIMEOUT``.
      This clashed with the ``TIMEOUT`` test property, which is one of the
      common properties that would be set with the ``PROPERTIES`` keyword,
      usually leading to legal but unintended behavior.  The keyword was
      changed to ``DISCOVERY_TIMEOUT`` in CMake 3.10.3 to address this
      problem.  The ambiguous behavior of the ``TIMEOUT`` keyword in 3.10.1
      and 3.10.2 has not been preserved.

  ``XML_OUTPUT_DIR dir``
    If specified, the parameter is passed along with ``--gtest_output=xml:``
    to test executable. The actual file name is the same as the test target,
    including prefix and suffix. This should be used instead of
    ``EXTRA_ARGS --gtest_output=xml`` to avoid race conditions writing the
    XML result output when using parallel test execution.

  ``DISCOVERY_MODE``
    Provides greater control over when ``gtest_discover_tests()`` performs test
    discovery. By default, ``POST_BUILD`` sets up a post-build command
    to perform test discovery at build time. In certain scenarios, like
    cross-compiling, this ``POST_BUILD`` behavior is not desirable.
    By contrast, ``PRE_TEST`` delays test discovery until just prior to test
    execution. This way test discovery occurs in the target environment
    where the test has a better chance at finding appropriate runtime
    dependencies.

    ``DISCOVERY_MODE`` defaults to the value of the
    ``CMAKE_GTEST_DISCOVER_TESTS_DISCOVERY_MODE`` variable if it is not
    passed when calling ``gtest_discover_tests()``. This provides a mechanism
    for globally selecting a preferred test discovery behavior without having
    to modify each call site.

#]=======================================================================]

# Save project's policies
cmake_policy(PUSH)
cmake_policy(SET CMP0057 NEW) # if IN_LIST

#------------------------------------------------------------------------------
function(gtest_add_tests)

  if (ARGC LESS 1)
    message(FATAL_ERROR "No arguments supplied to gtest_add_tests()")
  endif()

  set(options
      SKIP_DEPENDENCY
  )
  set(oneValueArgs
      TARGET
      WORKING_DIRECTORY
      TEST_PREFIX
      TEST_SUFFIX
      TEST_LIST
  )
  set(multiValueArgs
      SOURCES
      EXTRA_ARGS
  )
  set(allKeywords ${options} ${oneValueArgs} ${multiValueArgs})

  unset(sources)
  if("${ARGV0}" IN_LIST allKeywords)
    cmake_parse_arguments(ARGS "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})
    set(autoAddSources YES)
  else()
    # Non-keyword syntax, convert to keyword form
    if (ARGC LESS 3)
      message(FATAL_ERROR "gtest_add_tests() without keyword options requires at least 3 arguments")
    endif()
    set(ARGS_TARGET     "${ARGV0}")
    set(ARGS_EXTRA_ARGS "${ARGV1}")
    if(NOT "${ARGV2}" STREQUAL "AUTO")
      set(ARGS_SOURCES "${ARGV}")
      list(REMOVE_AT ARGS_SOURCES 0 1)
    endif()
  endif()

  # The non-keyword syntax allows the first argument to be an arbitrary
  # executable rather than a target if source files are also provided. In all
  # other cases, both forms require a target.
  if(NOT TARGET "${ARGS_TARGET}" AND NOT ARGS_SOURCES)
    message(FATAL_ERROR "${ARGS_TARGET} does not define an existing CMake target")
  endif()
  if(NOT ARGS_WORKING_DIRECTORY)
    unset(workDir)
  else()
    set(workDir WORKING_DIRECTORY "${ARGS_WORKING_DIRECTORY}")
  endif()

  if(NOT ARGS_SOURCES)
    get_property(ARGS_SOURCES TARGET ${ARGS_TARGET} PROPERTY SOURCES)
  endif()

  unset(testList)

  set(gtest_case_name_regex ".*\\( *([A-Za-z_0-9]+) *, *([A-Za-z_0-9]+) *\\).*")
  set(gtest_test_type_regex "(TYPED_TEST|TEST_?[FP]?)")

  foreach(source IN LISTS ARGS_SOURCES)
    if(NOT ARGS_SKIP_DEPENDENCY)
      set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${source})
    endif()
    file(READ "${source}" contents)
    string(REGEX MATCHALL "${gtest_test_type_regex} *\\(([A-Za-z_0-9 ,]+)\\)" found_tests "${contents}")
    foreach(hit ${found_tests})
      string(REGEX MATCH "${gtest_test_type_regex}" test_type ${hit})

      # Parameterized tests have a different signature for the filter
      if("x${test_type}" STREQUAL "xTEST_P")
        string(REGEX REPLACE ${gtest_case_name_regex}  "*/\\1.\\2/*" gtest_test_name ${hit})
      elseif("x${test_type}" STREQUAL "xTEST_F" OR "x${test_type}" STREQUAL "xTEST")
        string(REGEX REPLACE ${gtest_case_name_regex} "\\1.\\2" gtest_test_name ${hit})
      elseif("x${test_type}" STREQUAL "xTYPED_TEST")
        string(REGEX REPLACE ${gtest_case_name_regex} "\\1/*.\\2" gtest_test_name ${hit})
      else()
        message(WARNING "Could not parse GTest ${hit} for adding to CTest.")
        continue()
      endif()

      # Make sure tests disabled in GTest get disabled in CTest
      if(gtest_test_name MATCHES "(^|\\.)DISABLED_")
        # Add the disabled test if CMake is new enough
        # Note that this check is to allow backwards compatibility so this
        # module can be copied locally in projects to use with older CMake
        # versions
        if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.8.20170401)
          string(REGEX REPLACE
                 "(^|\\.)DISABLED_" "\\1"
                 orig_test_name "${gtest_test_name}"
          )
          set(ctest_test_name
              ${ARGS_TEST_PREFIX}${orig_test_name}${ARGS_TEST_SUFFIX}
          )
          add_test(NAME ${ctest_test_name}
                   ${workDir}
                   COMMAND ${ARGS_TARGET}
                     --gtest_also_run_disabled_tests
                     --gtest_filter=${gtest_test_name}
                     ${ARGS_EXTRA_ARGS}
          )
          set_tests_properties(${ctest_test_name} PROPERTIES DISABLED TRUE)
          list(APPEND testList ${ctest_test_name})
        endif()
      else()
        set(ctest_test_name ${ARGS_TEST_PREFIX}${gtest_test_name}${ARGS_TEST_SUFFIX})
        add_test(NAME ${ctest_test_name}
                 ${workDir}
                 COMMAND ${ARGS_TARGET}
                   --gtest_filter=${gtest_test_name}
                   ${ARGS_EXTRA_ARGS}
        )
        list(APPEND testList ${ctest_test_name})
      endif()
    endforeach()
  endforeach()

  if(ARGS_TEST_LIST)
    set(${ARGS_TEST_LIST} ${testList} PARENT_SCOPE)
  endif()

endfunction()

#------------------------------------------------------------------------------

function(gtest_discover_tests TARGET)
  cmake_parse_arguments(
    ""
    "NO_PRETTY_TYPES;NO_PRETTY_VALUES"
    "TEST_PREFIX;TEST_SUFFIX;WORKING_DIRECTORY;TEST_LIST;DISCOVERY_TIMEOUT;XML_OUTPUT_DIR;DISCOVERY_MODE"
    "EXTRA_ARGS;PROPERTIES"
    ${ARGN}
  )

  if(NOT _WORKING_DIRECTORY)
    set(_WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
  endif()
  if(NOT _TEST_LIST)
    set(_TEST_LIST ${TARGET}_TESTS)
  endif()
  if(NOT _DISCOVERY_TIMEOUT)
    set(_DISCOVERY_TIMEOUT 5)
  endif()
  if(NOT _DISCOVERY_MODE)
    if(NOT CMAKE_GTEST_DISCOVER_TESTS_DISCOVERY_MODE)
      set(CMAKE_GTEST_DISCOVER_TESTS_DISCOVERY_MODE "POST_BUILD")
    endif()
    set(_DISCOVERY_MODE ${CMAKE_GTEST_DISCOVER_TESTS_DISCOVERY_MODE})
  endif()

  get_property(
    has_counter
    TARGET ${TARGET}
    PROPERTY CTEST_DISCOVERED_TEST_COUNTER
    SET
  )
  if(has_counter)
    get_property(
      counter
      TARGET ${TARGET}
      PROPERTY CTEST_DISCOVERED_TEST_COUNTER
    )
    math(EXPR counter "${counter} + 1")
  else()
    set(counter 1)
  endif()
  set_property(
    TARGET ${TARGET}
    PROPERTY CTEST_DISCOVERED_TEST_COUNTER
    ${counter}
  )

  # Define rule to generate test list for aforementioned test executable
  set(ctest_file_base "${CMAKE_CURRENT_BINARY_DIR}/${TARGET}[${counter}]")
  set(ctest_include_file "${ctest_file_base}_include.cmake")
  set(ctest_tests_file "${ctest_file_base}_tests.cmake")
  get_property(crosscompiling_emulator
    TARGET ${TARGET}
    PROPERTY CROSSCOMPILING_EMULATOR
  )

  if(_DISCOVERY_MODE STREQUAL "POST_BUILD")
    add_custom_command(
      TARGET ${TARGET} POST_BUILD
      BYPRODUCTS "${ctest_tests_file}"
      COMMAND "${CMAKE_COMMAND}"
              -D "TEST_TARGET=${TARGET}"
              -D "TEST_EXECUTABLE=$<TARGET_FILE:${TARGET}>"
              -D "TEST_EXECUTOR=${crosscompiling_emulator}"
              -D "TEST_WORKING_DIR=${_WORKING_DIRECTORY}"
              -D "TEST_EXTRA_ARGS=${_EXTRA_ARGS}"
              -D "TEST_PROPERTIES=${_PROPERTIES}"
              -D "TEST_PREFIX=${_TEST_PREFIX}"
              -D "TEST_SUFFIX=${_TEST_SUFFIX}"
              -D "NO_PRETTY_TYPES=${_NO_PRETTY_TYPES}"
              -D "NO_PRETTY_VALUES=${_NO_PRETTY_VALUES}"
              -D "TEST_LIST=${_TEST_LIST}"
              -D "CTEST_FILE=${ctest_tests_file}"
              -D "TEST_DISCOVERY_TIMEOUT=${_DISCOVERY_TIMEOUT}"
              -D "TEST_XML_OUTPUT_DIR=${_XML_OUTPUT_DIR}"
              -P "${_GOOGLETEST_DISCOVER_TESTS_SCRIPT}"
      VERBATIM
    )

    file(WRITE "${ctest_include_file}"
      "if(EXISTS \"${ctest_tests_file}\")\n"
      "  include(\"${ctest_tests_file}\")\n"
      "else()\n"
      "  add_test(${TARGET}_NOT_BUILT ${TARGET}_NOT_BUILT)\n"
      "endif()\n"
    )
  elseif(_DISCOVERY_MODE STREQUAL "PRE_TEST")

    get_property(GENERATOR_IS_MULTI_CONFIG GLOBAL
        PROPERTY GENERATOR_IS_MULTI_CONFIG
    )

    if(GENERATOR_IS_MULTI_CONFIG)
      set(ctest_tests_file "${ctest_file_base}_tests-$<CONFIG>.cmake")
    endif()

    string(CONCAT ctest_include_content
      "if(EXISTS \"$<TARGET_FILE:${TARGET}>\")"                                    "\n"
      "  if(\"$<TARGET_FILE:${TARGET}>\" IS_NEWER_THAN \"${ctest_tests_file}\")"   "\n"
      "    include(\"${_GOOGLETEST_DISCOVER_TESTS_SCRIPT}\")"                      "\n"
      "    gtest_discover_tests_impl("                                             "\n"
      "      TEST_EXECUTABLE"        " [==[" "$<TARGET_FILE:${TARGET}>"   "]==]"   "\n"
      "      TEST_EXECUTOR"          " [==[" "${crosscompiling_emulator}" "]==]"   "\n"
      "      TEST_WORKING_DIR"       " [==[" "${_WORKING_DIRECTORY}"      "]==]"   "\n"
      "      TEST_EXTRA_ARGS"        " [==[" "${_EXTRA_ARGS}"             "]==]"   "\n"
      "      TEST_PROPERTIES"        " [==[" "${_PROPERTIES}"             "]==]"   "\n"
      "      TEST_PREFIX"            " [==[" "${_TEST_PREFIX}"            "]==]"   "\n"
      "      TEST_SUFFIX"            " [==[" "${_TEST_SUFFIX}"            "]==]"   "\n"
      "      NO_PRETTY_TYPES"        " [==[" "${_NO_PRETTY_TYPES}"        "]==]"   "\n"
      "      NO_PRETTY_VALUES"       " [==[" "${_NO_PRETTY_VALUES}"       "]==]"   "\n"
      "      TEST_LIST"              " [==[" "${_TEST_LIST}"              "]==]"   "\n"
      "      CTEST_FILE"             " [==[" "${ctest_tests_file}"        "]==]"   "\n"
      "      TEST_DISCOVERY_TIMEOUT" " [==[" "${_DISCOVERY_TIMEOUT}"      "]==]"   "\n"
      "      TEST_XML_OUTPUT_DIR"    " [==[" "${_XML_OUTPUT_DIR}"         "]==]"   "\n"
      "    )"                                                                      "\n"
      "  endif()"                                                                  "\n"
      "  include(\"${ctest_tests_file}\")"                                         "\n"
      "else()"                                                                     "\n"
      "  add_test(${TARGET}_NOT_BUILT ${TARGET}_NOT_BUILT)"                        "\n"
      "endif()"                                                                    "\n"
    )

    if(GENERATOR_IS_MULTI_CONFIG)
      foreach(_config ${CMAKE_CONFIGURATION_TYPES})
        file(GENERATE OUTPUT "${ctest_file_base}_include-${_config}.cmake" CONTENT "${ctest_include_content}" CONDITION $<CONFIG:${_config}>)
      endforeach()
      file(WRITE "${ctest_include_file}" "include(\"${ctest_file_base}_include-\${CTEST_CONFIGURATION_TYPE}.cmake\")")
    else()
      file(GENERATE OUTPUT "${ctest_file_base}_include.cmake" CONTENT "${ctest_include_content}")
      file(WRITE "${ctest_include_file}" "include(\"${ctest_file_base}_include.cmake\")")
    endif()

  else()
    message(FATAL_ERROR "Unknown DISCOVERY_MODE: ${_DISCOVERY_MODE}")
  endif()

  # Add discovered tests to directory TEST_INCLUDE_FILES
  set_property(DIRECTORY
    APPEND PROPERTY TEST_INCLUDE_FILES "${ctest_include_file}"
  )

endfunction()

###############################################################################

set(_GOOGLETEST_DISCOVER_TESTS_SCRIPT
  ${CMAKE_CURRENT_LIST_DIR}/GoogleTestAddTests.cmake
)

# Restore project's policies
cmake_policy(POP)
