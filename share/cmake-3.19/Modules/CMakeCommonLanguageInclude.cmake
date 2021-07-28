# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.


# this file has flags that are shared across languages and sets
# cache values that can be initialized in the platform-compiler.cmake file
# it may be included by more than one language.

string(APPEND CMAKE_EXE_LINKER_FLAGS_INIT " $ENV{LDFLAGS}")
string(APPEND CMAKE_SHARED_LINKER_FLAGS_INIT " $ENV{LDFLAGS}")
string(APPEND CMAKE_MODULE_LINKER_FLAGS_INIT " $ENV{LDFLAGS}")

cmake_initialize_per_config_variable(CMAKE_EXE_LINKER_FLAGS    "Flags used by the linker")
cmake_initialize_per_config_variable(CMAKE_SHARED_LINKER_FLAGS "Flags used by the linker during the creation of shared libraries")
cmake_initialize_per_config_variable(CMAKE_MODULE_LINKER_FLAGS "Flags used by the linker during the creation of modules")
cmake_initialize_per_config_variable(CMAKE_STATIC_LINKER_FLAGS "Flags used by the linker during the creation of static libraries")

# Alias the build tool variable for backward compatibility.
set(CMAKE_BUILD_TOOL ${CMAKE_MAKE_PROGRAM})

mark_as_advanced(
CMAKE_VERBOSE_MAKEFILE
)
