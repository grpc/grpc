# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
CTestUseLaunchers
-----------------

Set the RULE_LAUNCH_* global properties when CTEST_USE_LAUNCHERS is on.

CTestUseLaunchers is automatically included when you include(CTest).
However, it is split out into its own module file so projects can use
the CTEST_USE_LAUNCHERS functionality independently.

To use launchers, set CTEST_USE_LAUNCHERS to ON in a ctest -S
dashboard script, and then also set it in the cache of the configured
project.  Both cmake and ctest need to know the value of it for the
launchers to work properly.  CMake needs to know in order to generate
proper build rules, and ctest, in order to produce the proper error
and warning analysis.

For convenience, you may set the ENV variable
CTEST_USE_LAUNCHERS_DEFAULT in your ctest -S script, too.  Then, as
long as your CMakeLists uses include(CTest) or
include(CTestUseLaunchers), it will use the value of the ENV variable
to initialize a CTEST_USE_LAUNCHERS cache variable.  This cache
variable initialization only occurs if CTEST_USE_LAUNCHERS is not
already defined. If CTEST_USE_LAUNCHERS is on in a ctest -S script
the ctest_configure command will add -DCTEST_USE_LAUNCHERS:BOOL=TRUE
to the cmake command used to configure the project.
#]=======================================================================]

if(NOT DEFINED CTEST_USE_LAUNCHERS AND DEFINED ENV{CTEST_USE_LAUNCHERS_DEFAULT})
  set(CTEST_USE_LAUNCHERS "$ENV{CTEST_USE_LAUNCHERS_DEFAULT}"
    CACHE INTERNAL "CTEST_USE_LAUNCHERS initial value from ENV")
endif()

if(NOT "${CMAKE_GENERATOR}" MATCHES "Make|Ninja")
  set(CTEST_USE_LAUNCHERS 0)
endif()

if(CTEST_USE_LAUNCHERS)
  set(__launch_common_options
    "--target-name <TARGET_NAME> --build-dir <CMAKE_CURRENT_BINARY_DIR>")

  set(__launch_compile_options
    "${__launch_common_options} --output <OBJECT> --source <SOURCE> --language <LANGUAGE>")

  set(__launch_link_options
    "${__launch_common_options} --output <TARGET> --target-type <TARGET_TYPE> --language <LANGUAGE>")

  set(__launch_custom_options
    "${__launch_common_options} --output <OUTPUT>")

  if("${CMAKE_GENERATOR}" MATCHES "Ninja")
    string(APPEND __launch_compile_options " --filter-prefix <CMAKE_CL_SHOWINCLUDES_PREFIX>")
  endif()

  set(CTEST_LAUNCH_COMPILE
    "\"${CMAKE_CTEST_COMMAND}\" --launch ${__launch_compile_options} --")

  set(CTEST_LAUNCH_LINK
    "\"${CMAKE_CTEST_COMMAND}\" --launch ${__launch_link_options} --")

  set(CTEST_LAUNCH_CUSTOM
    "\"${CMAKE_CTEST_COMMAND}\" --launch ${__launch_custom_options} --")

  set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE "${CTEST_LAUNCH_COMPILE}")
  set_property(GLOBAL PROPERTY RULE_LAUNCH_LINK "${CTEST_LAUNCH_LINK}")
  set_property(GLOBAL PROPERTY RULE_LAUNCH_CUSTOM "${CTEST_LAUNCH_CUSTOM}")
endif()
