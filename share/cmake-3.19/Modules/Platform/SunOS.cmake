if(CMAKE_SYSTEM MATCHES "SunOS-4")
  set(CMAKE_C_COMPILE_OPTIONS_PIC "-PIC")
  set(CMAKE_C_COMPILE_OPTIONS_PIE "-PIE")
  set(CMAKE_SHARED_LIBRARY_C_FLAGS "-PIC")
  set(CMAKE_SHARED_LIBRARY_CREATE_C_FLAGS "-shared -Wl,-r")
  set(CMAKE_SHARED_LIBRARY_RUNTIME_C_FLAG "-Wl,-R")
  set(CMAKE_SHARED_LIBRARY_RUNTIME_C_FLAG_SEP ":")
endif()

include(Platform/UnixPaths)

list(APPEND CMAKE_SYSTEM_PREFIX_PATH
  /opt/csw
  /opt/openwin
  )

# The Sun linker needs to find transitive shared library dependencies
# in the -L path.
set(CMAKE_LINK_DEPENDENT_LIBRARY_DIRS 1)

# Shared libraries with no builtin soname may not be linked safely by
# specifying the file path.
set(CMAKE_PLATFORM_USES_PATH_WHEN_NO_SONAME 1)
