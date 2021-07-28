# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
GNUInstallDirs
--------------

Define GNU standard installation directories

Provides install directory variables as defined by the
`GNU Coding Standards`_.

.. _`GNU Coding Standards`: https://www.gnu.org/prep/standards/html_node/Directory-Variables.html

Result Variables
^^^^^^^^^^^^^^^^

Inclusion of this module defines the following variables:

``CMAKE_INSTALL_<dir>``

  Destination for files of a given type.  This value may be passed to
  the ``DESTINATION`` options of :command:`install` commands for the
  corresponding file type.

``CMAKE_INSTALL_FULL_<dir>``

  The absolute path generated from the corresponding ``CMAKE_INSTALL_<dir>``
  value.  If the value is not already an absolute path, an absolute path
  is constructed typically by prepending the value of the
  :variable:`CMAKE_INSTALL_PREFIX` variable.  However, there are some
  `special cases`_ as documented below.

where ``<dir>`` is one of:

``BINDIR``
  user executables (``bin``)
``SBINDIR``
  system admin executables (``sbin``)
``LIBEXECDIR``
  program executables (``libexec``)
``SYSCONFDIR``
  read-only single-machine data (``etc``)
``SHAREDSTATEDIR``
  modifiable architecture-independent data (``com``)
``LOCALSTATEDIR``
  modifiable single-machine data (``var``)
``RUNSTATEDIR``
  run-time variable data (``LOCALSTATEDIR/run``)
``LIBDIR``
  object code libraries (``lib`` or ``lib64``
  or ``lib/<multiarch-tuple>`` on Debian)
``INCLUDEDIR``
  C header files (``include``)
``OLDINCLUDEDIR``
  C header files for non-gcc (``/usr/include``)
``DATAROOTDIR``
  read-only architecture-independent data root (``share``)
``DATADIR``
  read-only architecture-independent data (``DATAROOTDIR``)
``INFODIR``
  info documentation (``DATAROOTDIR/info``)
``LOCALEDIR``
  locale-dependent data (``DATAROOTDIR/locale``)
``MANDIR``
  man documentation (``DATAROOTDIR/man``)
``DOCDIR``
  documentation root (``DATAROOTDIR/doc/PROJECT_NAME``)

If the includer does not define a value the above-shown default will be
used and the value will appear in the cache for editing by the user.

Special Cases
^^^^^^^^^^^^^

The following values of :variable:`CMAKE_INSTALL_PREFIX` are special:

``/``

  For ``<dir>`` other than the ``SYSCONFDIR``, ``LOCALSTATEDIR`` and
  ``RUNSTATEDIR``, the value of ``CMAKE_INSTALL_<dir>`` is prefixed
  with ``usr/`` if it is not user-specified as an absolute path.
  For example, the ``INCLUDEDIR`` value ``include`` becomes ``usr/include``.
  This is required by the `GNU Coding Standards`_, which state:

    When building the complete GNU system, the prefix will be empty
    and ``/usr`` will be a symbolic link to ``/``.

``/usr``

  For ``<dir>`` equal to ``SYSCONFDIR``, ``LOCALSTATEDIR`` or
  ``RUNSTATEDIR``, the ``CMAKE_INSTALL_FULL_<dir>`` is computed by
  prepending just ``/`` to the value of ``CMAKE_INSTALL_<dir>``
  if it is not user-specified as an absolute path.
  For example, the ``SYSCONFDIR`` value ``etc`` becomes ``/etc``.
  This is required by the `GNU Coding Standards`_.

``/opt/...``

  For ``<dir>`` equal to ``SYSCONFDIR``, ``LOCALSTATEDIR`` or
  ``RUNSTATEDIR``, the ``CMAKE_INSTALL_FULL_<dir>`` is computed by
  *appending* the prefix to the value of ``CMAKE_INSTALL_<dir>``
  if it is not user-specified as an absolute path.
  For example, the ``SYSCONFDIR`` value ``etc`` becomes ``/etc/opt/...``.
  This is defined by the `Filesystem Hierarchy Standard`_.

.. _`Filesystem Hierarchy Standard`: https://refspecs.linuxfoundation.org/FHS_3.0/fhs/index.html

Macros
^^^^^^

.. command:: GNUInstallDirs_get_absolute_install_dir

  ::

    GNUInstallDirs_get_absolute_install_dir(absvar var)

  Set the given variable ``absvar`` to the absolute path contained
  within the variable ``var``.  This is to allow the computation of an
  absolute path, accounting for all the special cases documented
  above.  While this macro is used to compute the various
  ``CMAKE_INSTALL_FULL_<dir>`` variables, it is exposed publicly to
  allow users who create additional path variables to also compute
  absolute paths where necessary, using the same logic.
#]=======================================================================]

cmake_policy(PUSH)
cmake_policy(SET CMP0054 NEW) # if() quoted variables not dereferenced

# Convert a cache variable to PATH type

macro(_GNUInstallDirs_cache_convert_to_path var description)
  get_property(_GNUInstallDirs_cache_type CACHE ${var} PROPERTY TYPE)
  if(_GNUInstallDirs_cache_type STREQUAL "UNINITIALIZED")
    file(TO_CMAKE_PATH "${${var}}" _GNUInstallDirs_cmakepath)
    set_property(CACHE ${var} PROPERTY TYPE PATH)
    set_property(CACHE ${var} PROPERTY VALUE "${_GNUInstallDirs_cmakepath}")
    set_property(CACHE ${var} PROPERTY HELPSTRING "${description}")
    unset(_GNUInstallDirs_cmakepath)
  endif()
  unset(_GNUInstallDirs_cache_type)
endmacro()

# Create a cache variable with default for a path.
macro(_GNUInstallDirs_cache_path var default description)
  if(NOT DEFINED ${var})
    set(${var} "${default}" CACHE PATH "${description}")
  endif()
  _GNUInstallDirs_cache_convert_to_path("${var}" "${description}")
endmacro()

# Create a cache variable with not default for a path, with a fallback
# when unset; used for entries slaved to other entries such as
# DATAROOTDIR.
macro(_GNUInstallDirs_cache_path_fallback var default description)
  if(NOT ${var})
    set(${var} "" CACHE PATH "${description}")
    set(${var} "${default}")
  endif()
  _GNUInstallDirs_cache_convert_to_path("${var}" "${description}")
endmacro()

# Installation directories
#

_GNUInstallDirs_cache_path(CMAKE_INSTALL_BINDIR "bin"
  "User executables (bin)")
_GNUInstallDirs_cache_path(CMAKE_INSTALL_SBINDIR "sbin"
  "System admin executables (sbin)")
_GNUInstallDirs_cache_path(CMAKE_INSTALL_SYSCONFDIR "etc"
  "Read-only single-machine data (etc)")
_GNUInstallDirs_cache_path(CMAKE_INSTALL_SHAREDSTATEDIR "com"
  "Modifiable architecture-independent data (com)")
_GNUInstallDirs_cache_path(CMAKE_INSTALL_LOCALSTATEDIR "var"
  "Modifiable single-machine data (var)")

# We check if the variable was manually set and not cached, in order to
# allow projects to set the values as normal variables before including
# GNUInstallDirs to avoid having the entries cached or user-editable. It
# replaces the "if(NOT DEFINED CMAKE_INSTALL_XXX)" checks in all the
# other cases.
# If CMAKE_INSTALL_LIBDIR is defined, if _libdir_set is false, then the
# variable is a normal one, otherwise it is a cache one.
get_property(_libdir_set CACHE CMAKE_INSTALL_LIBDIR PROPERTY TYPE SET)
if(NOT DEFINED CMAKE_INSTALL_LIBDIR OR (_libdir_set
    AND DEFINED _GNUInstallDirs_LAST_CMAKE_INSTALL_PREFIX
    AND NOT "${_GNUInstallDirs_LAST_CMAKE_INSTALL_PREFIX}" STREQUAL "${CMAKE_INSTALL_PREFIX}"))
  # If CMAKE_INSTALL_LIBDIR is not defined, it is always executed.
  # Otherwise:
  #  * if _libdir_set is false it is not executed (meaning that it is
  #    not a cache variable)
  #  * if _GNUInstallDirs_LAST_CMAKE_INSTALL_PREFIX is not defined it is
  #    not executed
  #  * if _GNUInstallDirs_LAST_CMAKE_INSTALL_PREFIX and
  #    CMAKE_INSTALL_PREFIX are the same string it is not executed.
  #    _GNUInstallDirs_LAST_CMAKE_INSTALL_PREFIX is updated after the
  #    execution, of this part of code, therefore at the next inclusion
  #    of the file, CMAKE_INSTALL_LIBDIR is defined, and the 2 strings
  #    are equal, meaning that the if is not executed the code the
  #    second time.

  set(_LIBDIR_DEFAULT "lib")
  # Override this default 'lib' with 'lib64' iff:
  #  - we are on Linux system but NOT cross-compiling
  #  - we are NOT on debian
  #  - we are on a 64 bits system
  # reason is: amd64 ABI: https://github.com/hjl-tools/x86-psABI/wiki/X86-psABI
  # For Debian with multiarch, use 'lib/${CMAKE_LIBRARY_ARCHITECTURE}' if
  # CMAKE_LIBRARY_ARCHITECTURE is set (which contains e.g. "i386-linux-gnu"
  # and CMAKE_INSTALL_PREFIX is "/usr"
  # See http://wiki.debian.org/Multiarch
  if(DEFINED _GNUInstallDirs_LAST_CMAKE_INSTALL_PREFIX)
    set(__LAST_LIBDIR_DEFAULT "lib")
    # __LAST_LIBDIR_DEFAULT is the default value that we compute from
    # _GNUInstallDirs_LAST_CMAKE_INSTALL_PREFIX, not a cache entry for
    # the value that was last used as the default.
    # This value is used to figure out whether the user changed the
    # CMAKE_INSTALL_LIBDIR value manually, or if the value was the
    # default one. When CMAKE_INSTALL_PREFIX changes, the value is
    # updated to the new default, unless the user explicitly changed it.
  endif()
  if (NOT DEFINED CMAKE_SYSTEM_NAME OR NOT DEFINED CMAKE_SIZEOF_VOID_P)
    message(AUTHOR_WARNING
      "Unable to determine default CMAKE_INSTALL_LIBDIR directory because no target architecture is known. "
      "Please enable at least one language before including GNUInstallDirs.")
  endif()
  if(CMAKE_SYSTEM_NAME MATCHES "^(Linux|kFreeBSD|GNU)$"
      AND NOT CMAKE_CROSSCOMPILING
      AND NOT EXISTS "/etc/arch-release")
    if (EXISTS "/etc/debian_version") # is this a debian system ?
      if(CMAKE_LIBRARY_ARCHITECTURE)
        if("${CMAKE_INSTALL_PREFIX}" MATCHES "^/usr/?$")
          set(_LIBDIR_DEFAULT "lib/${CMAKE_LIBRARY_ARCHITECTURE}")
        endif()
        if(DEFINED _GNUInstallDirs_LAST_CMAKE_INSTALL_PREFIX
            AND "${_GNUInstallDirs_LAST_CMAKE_INSTALL_PREFIX}" MATCHES "^/usr/?$")
          set(__LAST_LIBDIR_DEFAULT "lib/${CMAKE_LIBRARY_ARCHITECTURE}")
        endif()
      endif()
    else() # not debian, rely on CMAKE_SIZEOF_VOID_P:
      if("${CMAKE_SIZEOF_VOID_P}" EQUAL "8")
        set(_LIBDIR_DEFAULT "lib64")
        if(DEFINED _GNUInstallDirs_LAST_CMAKE_INSTALL_PREFIX)
          set(__LAST_LIBDIR_DEFAULT "lib64")
        endif()
      endif()
    endif()
  endif()
  if(NOT DEFINED CMAKE_INSTALL_LIBDIR)
    set(CMAKE_INSTALL_LIBDIR "${_LIBDIR_DEFAULT}" CACHE PATH "Object code libraries (${_LIBDIR_DEFAULT})")
  elseif(DEFINED __LAST_LIBDIR_DEFAULT
      AND "${__LAST_LIBDIR_DEFAULT}" STREQUAL "${CMAKE_INSTALL_LIBDIR}")
    set_property(CACHE CMAKE_INSTALL_LIBDIR PROPERTY VALUE "${_LIBDIR_DEFAULT}")
  endif()
endif()
_GNUInstallDirs_cache_convert_to_path(CMAKE_INSTALL_LIBDIR "Object code libraries (lib)")

# Save for next run
set(_GNUInstallDirs_LAST_CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}" CACHE INTERNAL "CMAKE_INSTALL_PREFIX during last run")
unset(_libdir_set)
unset(__LAST_LIBDIR_DEFAULT)

if(CMAKE_SYSTEM_NAME MATCHES "^(Linux|kFreeBSD|GNU)$"
    AND NOT CMAKE_CROSSCOMPILING
    AND NOT EXISTS "/etc/arch-release"
    AND EXISTS "/etc/debian_version" # is this a debian system ?
    AND "${CMAKE_INSTALL_PREFIX}" MATCHES "^/usr/?$")
  # see https://refspecs.linuxfoundation.org/FHS_3.0/fhs-3.0.html#usrlibexec
  # and https://www.debian.org/doc/debian-policy/ch-opersys#file-system-structure (section 9.1.1 bullet point 4)
  _GNUInstallDirs_cache_path(CMAKE_INSTALL_LIBEXECDIR "${CMAKE_INSTALL_LIBDIR}"
    "Program executables (${CMAKE_INSTALL_LIBDIR})")
else()
  _GNUInstallDirs_cache_path(CMAKE_INSTALL_LIBEXECDIR "libexec"
    "Program executables (libexec)")
endif()
_GNUInstallDirs_cache_path(CMAKE_INSTALL_INCLUDEDIR "include"
  "C header files (include)")
_GNUInstallDirs_cache_path(CMAKE_INSTALL_OLDINCLUDEDIR "/usr/include"
  "C header files for non-gcc (/usr/include)")
_GNUInstallDirs_cache_path(CMAKE_INSTALL_DATAROOTDIR "share"
  "Read-only architecture-independent data root (share)")

#-----------------------------------------------------------------------------
# Values whose defaults are relative to DATAROOTDIR.  Store empty values in
# the cache and store the defaults in local variables if the cache values are
# not set explicitly.  This auto-updates the defaults as DATAROOTDIR changes.

_GNUInstallDirs_cache_path_fallback(CMAKE_INSTALL_DATADIR "${CMAKE_INSTALL_DATAROOTDIR}"
  "Read-only architecture-independent data (DATAROOTDIR)")

if(CMAKE_SYSTEM_NAME MATCHES "^(([^kF].*)?BSD|DragonFly)$")
  _GNUInstallDirs_cache_path_fallback(CMAKE_INSTALL_INFODIR "info"
    "Info documentation (info)")
else()
  _GNUInstallDirs_cache_path_fallback(CMAKE_INSTALL_INFODIR "${CMAKE_INSTALL_DATAROOTDIR}/info"
    "Info documentation (DATAROOTDIR/info)")
endif()

if(CMAKE_SYSTEM_NAME MATCHES "^(([^k].*)?BSD|DragonFly)$")
  _GNUInstallDirs_cache_path_fallback(CMAKE_INSTALL_MANDIR "man"
    "Man documentation (man)")
else()
  _GNUInstallDirs_cache_path_fallback(CMAKE_INSTALL_MANDIR "${CMAKE_INSTALL_DATAROOTDIR}/man"
    "Man documentation (DATAROOTDIR/man)")
endif()

_GNUInstallDirs_cache_path_fallback(CMAKE_INSTALL_LOCALEDIR "${CMAKE_INSTALL_DATAROOTDIR}/locale"
  "Locale-dependent data (DATAROOTDIR/locale)")
_GNUInstallDirs_cache_path_fallback(CMAKE_INSTALL_DOCDIR "${CMAKE_INSTALL_DATAROOTDIR}/doc/${PROJECT_NAME}"
  "Documentation root (DATAROOTDIR/doc/PROJECT_NAME)")

_GNUInstallDirs_cache_path_fallback(CMAKE_INSTALL_RUNSTATEDIR "${CMAKE_INSTALL_LOCALSTATEDIR}/run"
  "Run-time variable data (LOCALSTATEDIR/run)")

#-----------------------------------------------------------------------------

mark_as_advanced(
  CMAKE_INSTALL_BINDIR
  CMAKE_INSTALL_SBINDIR
  CMAKE_INSTALL_LIBEXECDIR
  CMAKE_INSTALL_SYSCONFDIR
  CMAKE_INSTALL_SHAREDSTATEDIR
  CMAKE_INSTALL_LOCALSTATEDIR
  CMAKE_INSTALL_RUNSTATEDIR
  CMAKE_INSTALL_LIBDIR
  CMAKE_INSTALL_INCLUDEDIR
  CMAKE_INSTALL_OLDINCLUDEDIR
  CMAKE_INSTALL_DATAROOTDIR
  CMAKE_INSTALL_DATADIR
  CMAKE_INSTALL_INFODIR
  CMAKE_INSTALL_LOCALEDIR
  CMAKE_INSTALL_MANDIR
  CMAKE_INSTALL_DOCDIR
  )

macro(GNUInstallDirs_get_absolute_install_dir absvar var)
  if(NOT IS_ABSOLUTE "${${var}}")
    # Handle special cases:
    # - CMAKE_INSTALL_PREFIX == /
    # - CMAKE_INSTALL_PREFIX == /usr
    # - CMAKE_INSTALL_PREFIX == /opt/...
    if("${CMAKE_INSTALL_PREFIX}" STREQUAL "/")
      if("${dir}" STREQUAL "SYSCONFDIR" OR "${dir}" STREQUAL "LOCALSTATEDIR" OR "${dir}" STREQUAL "RUNSTATEDIR")
        set(${absvar} "/${${var}}")
      else()
        if (NOT "${${var}}" MATCHES "^usr/")
          set(${var} "usr/${${var}}")
        endif()
        set(${absvar} "/${${var}}")
      endif()
    elseif("${CMAKE_INSTALL_PREFIX}" MATCHES "^/usr/?$")
      if("${dir}" STREQUAL "SYSCONFDIR" OR "${dir}" STREQUAL "LOCALSTATEDIR" OR "${dir}" STREQUAL "RUNSTATEDIR")
        set(${absvar} "/${${var}}")
      else()
        set(${absvar} "${CMAKE_INSTALL_PREFIX}/${${var}}")
      endif()
    elseif("${CMAKE_INSTALL_PREFIX}" MATCHES "^/opt/.*")
      if("${dir}" STREQUAL "SYSCONFDIR" OR "${dir}" STREQUAL "LOCALSTATEDIR" OR "${dir}" STREQUAL "RUNSTATEDIR")
        set(${absvar} "/${${var}}${CMAKE_INSTALL_PREFIX}")
      else()
        set(${absvar} "${CMAKE_INSTALL_PREFIX}/${${var}}")
      endif()
    else()
      set(${absvar} "${CMAKE_INSTALL_PREFIX}/${${var}}")
    endif()
  else()
    set(${absvar} "${${var}}")
  endif()
endmacro()

# Result directories
#
foreach(dir
    BINDIR
    SBINDIR
    LIBEXECDIR
    SYSCONFDIR
    SHAREDSTATEDIR
    LOCALSTATEDIR
    RUNSTATEDIR
    LIBDIR
    INCLUDEDIR
    OLDINCLUDEDIR
    DATAROOTDIR
    DATADIR
    INFODIR
    LOCALEDIR
    MANDIR
    DOCDIR
    )
  GNUInstallDirs_get_absolute_install_dir(CMAKE_INSTALL_FULL_${dir} CMAKE_INSTALL_${dir})
endforeach()

cmake_policy(POP)
