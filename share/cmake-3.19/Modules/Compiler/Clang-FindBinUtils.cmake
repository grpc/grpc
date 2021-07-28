if(NOT DEFINED _CMAKE_PROCESSING_LANGUAGE OR _CMAKE_PROCESSING_LANGUAGE STREQUAL "")
  message(FATAL_ERROR "Internal error: _CMAKE_PROCESSING_LANGUAGE is not set")
endif()

# Debian:
# * /usr/bin/llvm-ar-4.0
# * /usr/bin/llvm-ranlib-4.0
string(REGEX MATCH "^([0-9]+\\.[0-9]+)" __version_x_y
    "${CMAKE_${_CMAKE_PROCESSING_LANGUAGE}_COMPILER_VERSION}")

# Try to find tools in the same directory as Clang itself
get_filename_component(__clang_hint_1 "${CMAKE_${_CMAKE_PROCESSING_LANGUAGE}_COMPILER}" REALPATH)
get_filename_component(__clang_hint_1 "${__clang_hint_1}" DIRECTORY)

get_filename_component(__clang_hint_2 "${CMAKE_${_CMAKE_PROCESSING_LANGUAGE}_COMPILER}" DIRECTORY)

set(__clang_hints ${__clang_hint_1} ${__clang_hint_2})

# http://manpages.ubuntu.com/manpages/precise/en/man1/llvm-ar.1.html
find_program(CMAKE_${_CMAKE_PROCESSING_LANGUAGE}_COMPILER_AR NAMES
    "${_CMAKE_TOOLCHAIN_PREFIX}llvm-ar-${__version_x_y}"
    "${_CMAKE_TOOLCHAIN_PREFIX}llvm-ar"
    HINTS ${__clang_hints}
    DOC "LLVM archiver"
)
mark_as_advanced(CMAKE_${_CMAKE_PROCESSING_LANGUAGE}_COMPILER_AR)

# http://manpages.ubuntu.com/manpages/precise/en/man1/llvm-ranlib.1.html
find_program(CMAKE_${_CMAKE_PROCESSING_LANGUAGE}_COMPILER_RANLIB NAMES
    "${_CMAKE_TOOLCHAIN_PREFIX}llvm-ranlib-${__version_x_y}"
    "${_CMAKE_TOOLCHAIN_PREFIX}llvm-ranlib"
    HINTS ${__clang_hints}
    DOC "Generate index for LLVM archive"
)
mark_as_advanced(CMAKE_${_CMAKE_PROCESSING_LANGUAGE}_COMPILER_RANLIB)
