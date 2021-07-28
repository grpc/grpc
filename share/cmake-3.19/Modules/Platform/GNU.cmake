# GCC is the default compiler on GNU/Hurd.
set(CMAKE_DL_LIBS "dl")
set(CMAKE_SHARED_LIBRARY_C_FLAGS "-fPIC")
set(CMAKE_SHARED_LIBRARY_CREATE_C_FLAGS "-shared")
set(CMAKE_SHARED_LIBRARY_RUNTIME_C_FLAG "-Wl,-rpath,")
set(CMAKE_SHARED_LIBRARY_RUNTIME_C_FLAG_SEP ":")
set(CMAKE_SHARED_LIBRARY_RPATH_LINK_C_FLAG "-Wl,-rpath-link,")
set(CMAKE_SHARED_LIBRARY_SONAME_C_FLAG "-Wl,-soname,")
set(CMAKE_EXE_EXPORTS_C_FLAG "-Wl,--export-dynamic")

# Debian policy requires that shared libraries be installed without
# executable permission.  Fedora policy requires that shared libraries
# be installed with the executable permission.  Since the native tools
# create shared libraries with execute permission in the first place a
# reasonable policy seems to be to install with execute permission by
# default.  In order to support debian packages we provide an option
# here.  The option default is based on the current distribution, but
# packagers can set it explicitly on the command line.
if(DEFINED CMAKE_INSTALL_SO_NO_EXE)
  # Store the decision variable in the cache.  This preserves any
  # setting the user provides on the command line.
  set(CMAKE_INSTALL_SO_NO_EXE "${CMAKE_INSTALL_SO_NO_EXE}" CACHE INTERNAL
    "Install .so files without execute permission.")
else()
  # Store the decision variable as an internal cache entry to avoid
  # checking the platform every time.  This option is advanced enough
  # that only package maintainers should need to adjust it.  They are
  # capable of providing a setting on the command line.
  if(EXISTS "/etc/debian_version")
    set(CMAKE_INSTALL_SO_NO_EXE 1 CACHE INTERNAL
      "Install .so files without execute permission.")
  else()
    set(CMAKE_INSTALL_SO_NO_EXE 0 CACHE INTERNAL
      "Install .so files without execute permission.")
  endif()
endif()

set(CMAKE_LIBRARY_ARCHITECTURE_REGEX "[a-z0-9_]+(-[a-z0-9_]+)?-gnu[a-z0-9_]*")

include(Platform/UnixPaths)
