set(WIN32 1)

if(CMAKE_SYSTEM_NAME STREQUAL "WindowsCE")
  set(WINCE 1)
elseif(CMAKE_SYSTEM_NAME STREQUAL "WindowsPhone")
  set(WINDOWS_PHONE 1)
elseif(CMAKE_SYSTEM_NAME STREQUAL "WindowsStore")
  set(WINDOWS_STORE 1)
endif()

set(CMAKE_STATIC_LIBRARY_PREFIX "")
set(CMAKE_STATIC_LIBRARY_SUFFIX ".lib")
set(CMAKE_SHARED_LIBRARY_PREFIX "")          # lib
set(CMAKE_SHARED_LIBRARY_SUFFIX ".dll")          # .so
set(CMAKE_IMPORT_LIBRARY_PREFIX "")
set(CMAKE_IMPORT_LIBRARY_SUFFIX ".lib")
set(CMAKE_EXECUTABLE_SUFFIX ".exe")          # .exe
set(CMAKE_LINK_LIBRARY_SUFFIX ".lib")
set(CMAKE_DL_LIBS "")
set(CMAKE_EXTRA_LINK_EXTENSIONS ".targets")

set(CMAKE_FIND_LIBRARY_PREFIXES "")
set(CMAKE_FIND_LIBRARY_SUFFIXES ".lib")

# for borland make long command lines are redirected to a file
# with the following syntax, see Windows-bcc32.cmake for use
if(CMAKE_GENERATOR MATCHES "Borland")
  set(CMAKE_START_TEMP_FILE "@&&|\n")
  set(CMAKE_END_TEMP_FILE "\n|")
endif()

# for nmake make long command lines are redirected to a file
# with the following syntax, see Windows-bcc32.cmake for use
if(CMAKE_GENERATOR MATCHES "NMake")
  set(CMAKE_START_TEMP_FILE "@<<\n")
  set(CMAKE_END_TEMP_FILE "\n<<")
endif()

include(Platform/WindowsPaths)

# uncomment these out to debug nmake and borland makefiles
#set(CMAKE_START_TEMP_FILE "")
#set(CMAKE_END_TEMP_FILE "")
#set(CMAKE_VERBOSE_MAKEFILE 1)

