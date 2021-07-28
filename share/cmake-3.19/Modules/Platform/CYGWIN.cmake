if("${CMAKE_MINIMUM_REQUIRED_VERSION}" VERSION_LESS "2.8.3.20101214")
  set(__USE_CMAKE_LEGACY_CYGWIN_WIN32 1)
endif()
if(NOT DEFINED WIN32)
  set(WIN32 0)
  if(DEFINED __USE_CMAKE_LEGACY_CYGWIN_WIN32)
    if(NOT DEFINED CMAKE_LEGACY_CYGWIN_WIN32
        AND DEFINED ENV{CMAKE_LEGACY_CYGWIN_WIN32})
      set(CMAKE_LEGACY_CYGWIN_WIN32 $ENV{CMAKE_LEGACY_CYGWIN_WIN32})
    endif()
    if(CMAKE_LEGACY_CYGWIN_WIN32)
      message(STATUS "Defining WIN32 under Cygwin due to CMAKE_LEGACY_CYGWIN_WIN32")
      set(WIN32 1)
    elseif("x${CMAKE_LEGACY_CYGWIN_WIN32}" STREQUAL "x")
      message(WARNING "CMake no longer defines WIN32 on Cygwin!"
        "\n"
        "(1) If you are just trying to build this project, ignore this warning "
        "or quiet it by setting CMAKE_LEGACY_CYGWIN_WIN32=0 in your environment or "
        "in the CMake cache.  "
        "If later configuration or build errors occur then this project may "
        "have been written under the assumption that Cygwin is WIN32.  "
        "In that case, set CMAKE_LEGACY_CYGWIN_WIN32=1 instead."
        "\n"
        "(2) If you are developing this project, add the line\n"
        "  set(CMAKE_LEGACY_CYGWIN_WIN32 0) # Remove when CMake >= 2.8.4 is required\n"
        "at the top of your top-level CMakeLists.txt file or set the minimum "
        "required version of CMake to 2.8.4 or higher.  "
        "Then teach your project to build on Cygwin without WIN32.")
    endif()
  elseif(DEFINED CMAKE_LEGACY_CYGWIN_WIN32)
    message(AUTHOR_WARNING "CMAKE_LEGACY_CYGWIN_WIN32 ignored because\n"
      "  cmake_minimum_required(VERSION ${CMAKE_MINIMUM_REQUIRED_VERSION})\n"
      "is at least 2.8.4.")
  endif()
endif()
if(DEFINED __USE_CMAKE_LEGACY_CYGWIN_WIN32)
  # Pass WIN32 legacy setting to scripts.
  if(WIN32)
    set(ENV{CMAKE_LEGACY_CYGWIN_WIN32} 1)
  else()
    set(ENV{CMAKE_LEGACY_CYGWIN_WIN32} 0)
  endif()
  unset(__USE_CMAKE_LEGACY_CYGWIN_WIN32)
endif()

set(CYGWIN 1)

set(CMAKE_SHARED_LIBRARY_PREFIX "cyg")
set(CMAKE_SHARED_LIBRARY_SUFFIX ".dll")
set(CMAKE_SHARED_MODULE_PREFIX "cyg")
set(CMAKE_SHARED_MODULE_SUFFIX ".dll")
set(CMAKE_IMPORT_LIBRARY_PREFIX "lib")
set(CMAKE_IMPORT_LIBRARY_SUFFIX ".dll.a")
set(CMAKE_EXECUTABLE_SUFFIX ".exe")          # .exe
# Modules have a different default prefix that shared libs.
set(CMAKE_MODULE_EXISTS 1)

set(CMAKE_FIND_LIBRARY_PREFIXES "lib")
set(CMAKE_FIND_LIBRARY_SUFFIXES ".dll.a" ".a")

# Shared libraries on cygwin can be named with their version number.
set(CMAKE_SHARED_LIBRARY_NAME_WITH_VERSION 1)

include(Platform/UnixPaths)

# Windows API on Cygwin
list(APPEND CMAKE_SYSTEM_INCLUDE_PATH
  /usr/include/w32api
  )

# Windows API on Cygwin
list(APPEND CMAKE_SYSTEM_LIBRARY_PATH
  /usr/lib/w32api
  )
