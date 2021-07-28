# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

if(UNIX)
  set(CMAKE_Swift_OUTPUT_EXTENSION .o)
else()
  set(CMAKE_Swift_OUTPUT_EXTENSION .obj)
endif()

# Load compiler-specific information.
if(CMAKE_Swift_COMPILER_ID)
  include(Compiler/${CMAKE_Swift_COMPILER_ID}-Swift OPTIONAL)

  if(CMAKE_SYSTEM_PROCESSOR)
    include(Platform/${CMAKE_EFFECTIVE_SYSTEM_NAME}-${CMAKE_Swift_COMPILER_ID}-Swift-${CMAKE_SYSTEM_PROCESSOR} OPTIONAL)
  endif()
  include(Platform/${CMAKE_EFFECTIVE_SYSTEM_NAME}-${CMAKE_Swift_COMPILER_ID}-Swift OPTIONAL)
endif()

set(CMAKE_EXE_EXPORTS_Swift_FLAG "-emit-module -emit-module-path <SWIFT_MODULE> ${CMAKE_Swift_IMPLIB_LINKER_FLAGS}")

set(CMAKE_INCLUDE_FLAG_Swift "-I ")
if(CMAKE_SYSTEM_NAME STREQUAL Darwin)
  set(CMAKE_SHARED_LIBRARY_SONAME_Swift_FLAG "-Xlinker -install_name -Xlinker ")
elseif(NOT CMAKE_SYSTEM_NAME STREQUAL Windows)
  set(CMAKE_SHARED_LIBRARY_SONAME_Swift_FLAG "-Xlinker -soname -Xlinker ")
endif()

if(NOT CMAKE_SYSTEM_NAME STREQUAL Windows)
  set(CMAKE_EXECUTABLE_RUNTIME_Swift_FLAG "-Xlinker -rpath -Xlinker ")
  set(CMAKE_SHARED_LIBRARY_RUNTIME_Swift_FLAG "-Xlinker -rpath -Xlinker ")
  if(CMAKE_SYSTEM_NAME STREQUAL Darwin)
    set(CMAKE_EXECUTABLE_RUNTIME_Swift_FLAG_SEP "")
    set(CMAKE_SHARED_LIBRARY_RUNTIME_Swift_FLAG_SEP "")
  else()
    set(CMAKE_EXECUTABLE_RUNTIME_Swift_FLAG_SEP ":")
    set(CMAKE_SHARED_LIBRARY_RUNTIME_Swift_FLAG_SEP ":")
  endif()
endif()

set(CMAKE_Swift_COMPILE_OPTIONS_TARGET "-target ")
set(CMAKE_Swift_COMPILE_OPTIONS_EXTERNAL_TOOLCHAIN "-tools-directory ")
# NOTE(compnerd) the `-sdk` support is not yet ready in the compiler; when that
# is fully working, we should be able to enable this.
# set(CMAKE_Swift_COMPILE_OPTIONS_SYSROOT "-sdk ")
# NOTE(compnerd) do not setup `-frontend` as we use the compiler as the driver
# during the link phase and use that to drive the compilation
set(CMAKE_Swift_COMPILER_ARG1 "")
set(CMAKE_Swift_DEFINE_FLAG -D)
set(CMAKE_Swift_FRAMEWORK_SEARCH_FLAG "-F ")
set(CMAKE_Swift_LIBRARY_PATH_FLAG "-L ")
set(CMAKE_Swift_LIBRARY_PATH_TERMINATOR "")
set(CMAKE_Swift_LINK_LIBRARY_FLAG "-l")
set(CMAKE_Swift_LINKER_WRAPPER_FLAG "-Xlinker" " ")
set(CMAKE_Swift_RESPONSE_FILE_LINK_FLAG @)

set(CMAKE_Swift_LINKER_PREFERENCE 50)
set(CMAKE_Swift_LINKER_PREFERENCE_PROPAGATES 1)

# NOTE(compnerd) use the short form for convenience and ease of search.  They
# are treated equivalent to their long form names as well as custom Swift
# specific names.
set(CMAKE_Swift_COMPILE_OPTIONS_MSVC_RUNTIME_LIBRARY_MultiThreaded -libc MT)
set(CMAKE_Swift_COMPILE_OPTIONS_MSVC_RUNTIME_LIBRARY_MultiThreadedDLL -libc MD)
set(CMAKE_Swift_COMPILE_OPTIONS_MSVC_RUNTIME_LIBRARY_MultiThreadedDebug -libc MTd)
set(CMAKE_Swift_COMPILE_OPTIONS_MSVC_RUNTIME_LIBRARY_MultiThreadedDebugDLL -libc MDd)

set(CMAKE_Swift_FLAGS_DEBUG_INIT "-g")
set(CMAKE_Swift_FLAGS_RELEASE_INIT "-O")
set(CMAKE_Swift_FLAGS_RELWITHDEBINFO_INIT "-O -g")
set(CMAKE_Swift_FLAGS_MINSIZEREL_INIT "-Osize")

cmake_initialize_per_config_variable(CMAKE_Swift_FLAGS "Swift Compiler Flags")

# NOTE(compnerd) we do not have an object compile rule since we build the objects as part of the link step
if(NOT CMAKE_Swift_COMPILE_OBJECT)
  set(CMAKE_Swift_COMPILE_OBJECT ":")
endif()

if(NOT CMAKE_Swift_NUM_THREADS MATCHES "^[0-9]+$")
  cmake_host_system_information(RESULT CMAKE_Swift_NUM_THREADS QUERY NUMBER_OF_LOGICAL_CORES)
endif()

if(NOT CMAKE_Swift_CREATE_SHARED_LIBRARY)
  set(CMAKE_Swift_CREATE_SHARED_LIBRARY "<CMAKE_Swift_COMPILER> -output-file-map <SWIFT_OUTPUT_FILE_MAP> -incremental -j ${CMAKE_Swift_NUM_THREADS} -emit-library -o <TARGET> -module-name <SWIFT_MODULE_NAME> -module-link-name <SWIFT_LIBRARY_NAME> -emit-module -emit-module-path <SWIFT_MODULE> -emit-dependencies <DEFINES> <FLAGS> <INCLUDES> <SWIFT_SOURCES> <LINK_FLAGS> <SONAME_FLAG> <TARGET_INSTALLNAME_DIR><TARGET_SONAME> ${CMAKE_Swift_IMPLIB_LINKER_FLAGS} <LINK_LIBRARIES>")
endif()

if(NOT CMAKE_Swift_CREATE_SHARED_MODULE)
  set(CMAKE_Swift_CREATE_SHARED_MODULE ${CMAKE_Swift_CREATE_SHARED_LIBRARY})
endif()

if(NOT CMAKE_Swift_LINK_EXECUTABLE)
  set(CMAKE_Swift_LINK_EXECUTABLE "<CMAKE_Swift_COMPILER> -output-file-map <SWIFT_OUTPUT_FILE_MAP> -incremental -j ${CMAKE_Swift_NUM_THREADS} -emit-executable -o <TARGET> -emit-dependencies <DEFINES> <FLAGS> <INCLUDES> <SWIFT_SOURCES> <LINK_FLAGS> <LINK_LIBRARIES>")
endif()

if(NOT CMAKE_Swift_CREATE_STATIC_LIBRARY)
  set(CMAKE_Swift_CREATE_STATIC_LIBRARY "<CMAKE_Swift_COMPILER> -output-file-map <SWIFT_OUTPUT_FILE_MAP> -incremental -j ${CMAKE_Swift_NUM_THREADS} -emit-library -static -o <TARGET> -module-name <SWIFT_MODULE_NAME> -module-link-name <SWIFT_LIBRARY_NAME> -emit-module -emit-module-path <SWIFT_MODULE> -emit-dependencies <DEFINES> <FLAGS> <INCLUDES> <SWIFT_SOURCES> <LINK_FLAGS> <LINK_LIBRARIES>")

  set(CMAKE_Swift_ARCHIVE_CREATE "<CMAKE_AR> crs <TARGET> <OBJECTS>")
  set(CMAKE_Swift_ARCHIVE_FINISH "")
endif()

set(CMAKE_Swift_INFORMATION_LOADED 1)
