# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
FindJNI
-------

Find Java Native Interface (JNI) libraries.

JNI enables Java code running in a Java Virtual Machine (JVM) to call
and be called by native applications and libraries written in other
languages such as C, C++.

This module finds if Java is installed and determines where the
include files and libraries are.  It also determines what the name of
the library is.  The caller may set variable ``JAVA_HOME`` to specify a
Java installation prefix explicitly.

Result Variables
^^^^^^^^^^^^^^^^

This module sets the following result variables:

``JNI_INCLUDE_DIRS``
  the include dirs to use
``JNI_LIBRARIES``
  the libraries to use (JAWT and JVM)
``JNI_FOUND``
  TRUE if JNI headers and libraries were found.

Cache Variables
^^^^^^^^^^^^^^^

The following cache variables are also available to set or use:

``JAVA_AWT_LIBRARY``
  the path to the Java AWT Native Interface (JAWT) library
``JAVA_JVM_LIBRARY``
  the path to the Java Virtual Machine (JVM) library
``JAVA_INCLUDE_PATH``
  the include path to jni.h
``JAVA_INCLUDE_PATH2``
  the include path to jni_md.h and jniport.h
``JAVA_AWT_INCLUDE_PATH``
  the include path to jawt.h
#]=======================================================================]

# Expand {libarch} occurrences to java_libarch subdirectory(-ies) and set ${_var}
macro(java_append_library_directories _var)
    # Determine java arch-specific library subdir
    # Mostly based on openjdk/jdk/make/common/shared/Platform.gmk as of openjdk
    # 1.6.0_18 + icedtea patches. However, it would be much better to base the
    # guess on the first part of the GNU config.guess platform triplet.
    if(CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
      if(CMAKE_LIBRARY_ARCHITECTURE STREQUAL "x86_64-linux-gnux32")
        set(_java_libarch "x32" "amd64" "i386")
      else()
        set(_java_libarch "amd64" "i386")
      endif()
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^i.86$")
        set(_java_libarch "i386")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^aarch64")
        set(_java_libarch "arm64" "aarch64")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^alpha")
        set(_java_libarch "alpha")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^arm")
        # Subdir is "arm" for both big-endian (arm) and little-endian (armel).
        set(_java_libarch "arm" "aarch32")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^mips")
        # mips* machines are bi-endian mostly so processor does not tell
        # endianness of the underlying system.
        set(_java_libarch "${CMAKE_SYSTEM_PROCESSOR}"
            "mips" "mipsel" "mipseb" "mipsr6" "mipsr6el"
            "mips64" "mips64el" "mips64r6" "mips64r6el"
            "mipsn32" "mipsn32el" "mipsn32r6" "mipsn32r6el")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(powerpc|ppc)64le")
        set(_java_libarch "ppc64" "ppc64le")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(powerpc|ppc)64")
        set(_java_libarch "ppc64" "ppc")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(powerpc|ppc)")
        set(_java_libarch "ppc" "ppc64")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^sparc")
        # Both flavours can run on the same processor
        set(_java_libarch "${CMAKE_SYSTEM_PROCESSOR}" "sparc" "sparcv9")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(parisc|hppa)")
        set(_java_libarch "parisc" "parisc64")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^s390")
        # s390 binaries can run on s390x machines
        set(_java_libarch "${CMAKE_SYSTEM_PROCESSOR}" "s390" "s390x")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^sh")
        set(_java_libarch "sh")
    else()
        set(_java_libarch "${CMAKE_SYSTEM_PROCESSOR}")
    endif()

    # Append default list architectures if CMAKE_SYSTEM_PROCESSOR was empty or
    # system is non-Linux (where the code above has not been well tested)
    if(NOT _java_libarch OR NOT (CMAKE_SYSTEM_NAME MATCHES "Linux"))
        list(APPEND _java_libarch "i386" "amd64" "ppc")
    endif()

    # Sometimes ${CMAKE_SYSTEM_PROCESSOR} is added to the list to prefer
    # current value to a hardcoded list. Remove possible duplicates.
    list(REMOVE_DUPLICATES _java_libarch)

    foreach(_path ${ARGN})
        if(_path MATCHES "{libarch}")
            foreach(_libarch ${_java_libarch})
                string(REPLACE "{libarch}" "${_libarch}" _newpath "${_path}")
                if(EXISTS ${_newpath})
                    list(APPEND ${_var} "${_newpath}")
                endif()
            endforeach()
        else()
            if(EXISTS ${_path})
                list(APPEND ${_var} "${_path}")
            endif()
        endif()
    endforeach()
endmacro()

include(${CMAKE_CURRENT_LIST_DIR}/CMakeFindJavaCommon.cmake)

# Save CMAKE_FIND_FRAMEWORK
if(DEFINED CMAKE_FIND_FRAMEWORK)
  set(_JNI_CMAKE_FIND_FRAMEWORK ${CMAKE_FIND_FRAMEWORK})
else()
  unset(_JNI_CMAKE_FIND_FRAMEWORK)
endif()

if(_JAVA_HOME_EXPLICIT)
  set(CMAKE_FIND_FRAMEWORK NEVER)
endif()

set(JAVA_AWT_LIBRARY_DIRECTORIES)
if(_JAVA_HOME)
  JAVA_APPEND_LIBRARY_DIRECTORIES(JAVA_AWT_LIBRARY_DIRECTORIES
    ${_JAVA_HOME}/jre/lib/{libarch}
    ${_JAVA_HOME}/jre/lib
    ${_JAVA_HOME}/lib/{libarch}
    ${_JAVA_HOME}/lib
    ${_JAVA_HOME}
    )
endif()

if (WIN32)
  set (_JNI_HINTS)
  execute_process(COMMAND REG QUERY HKLM\\SOFTWARE\\JavaSoft\\JDK
    RESULT_VARIABLE _JNI_RESULT
    OUTPUT_VARIABLE _JNI_VERSIONS
    ERROR_QUIET)
  if (NOT  _JNI_RESULT)
    string (REGEX MATCHALL "HKEY_LOCAL_MACHINE\\\\SOFTWARE\\\\JavaSoft\\\\JDK\\\\[0-9.]+" _JNI_VERSIONS "${_JNI_VERSIONS}")
    if (_JNI_VERSIONS)
      # sort versions. Most recent first
      ## handle version 9 apart from other versions to get correct ordering
      set (_JNI_V9 ${_JNI_VERSIONS})
      list (FILTER _JNI_VERSIONS EXCLUDE REGEX "JDK\\\\9")
      list (SORT _JNI_VERSIONS)
      list (REVERSE _JNI_VERSIONS)
      list (FILTER _JNI_V9 INCLUDE REGEX "JDK\\\\9")
      list (SORT _JNI_V9)
      list (REVERSE _JNI_V9)
      list (APPEND _JNI_VERSIONS ${_JNI_V9})
      foreach (_JNI_HINT IN LISTS _JNI_VERSIONS)
        list(APPEND _JNI_HINTS "[${_JNI_HINT};JavaHome]")
      endforeach()
    endif()
  endif()

  foreach (_JNI_HINT IN LISTS _JNI_HINTS)
    list(APPEND JAVA_AWT_LIBRARY_DIRECTORIES "${_JNI_HINT}/lib")
  endforeach()

  get_filename_component(java_install_version
    "[HKEY_LOCAL_MACHINE\\SOFTWARE\\JavaSoft\\Java Development Kit;CurrentVersion]" NAME)

  list(APPEND JAVA_AWT_LIBRARY_DIRECTORIES
    "[HKEY_LOCAL_MACHINE\\SOFTWARE\\JavaSoft\\Java Development Kit\\1.9;JavaHome]/lib"
    "[HKEY_LOCAL_MACHINE\\SOFTWARE\\JavaSoft\\Java Development Kit\\1.8;JavaHome]/lib"
    "[HKEY_LOCAL_MACHINE\\SOFTWARE\\JavaSoft\\Java Development Kit\\1.7;JavaHome]/lib"
    "[HKEY_LOCAL_MACHINE\\SOFTWARE\\JavaSoft\\Java Development Kit\\1.6;JavaHome]/lib"
    "[HKEY_LOCAL_MACHINE\\SOFTWARE\\JavaSoft\\Java Development Kit\\1.5;JavaHome]/lib"
    "[HKEY_LOCAL_MACHINE\\SOFTWARE\\JavaSoft\\Java Development Kit\\1.4;JavaHome]/lib"
    "[HKEY_LOCAL_MACHINE\\SOFTWARE\\JavaSoft\\Java Development Kit\\1.3;JavaHome]/lib"
    "[HKEY_LOCAL_MACHINE\\SOFTWARE\\JavaSoft\\Java Development Kit\\${java_install_version};JavaHome]/lib"
    )
endif()

set(_JNI_JAVA_DIRECTORIES_BASE
  /usr/lib/jvm/java
  /usr/lib/java
  /usr/lib/jvm
  /usr/local/lib/java
  /usr/local/share/java
  /usr/lib/j2sdk1.4-sun
  /usr/lib/j2sdk1.5-sun
  /opt/sun-jdk-1.5.0.04
  /usr/lib/jvm/java-6-sun
  /usr/lib/jvm/java-1.5.0-sun
  /usr/lib/jvm/java-6-sun-1.6.0.00       # can this one be removed according to #8821 ? Alex
  /usr/lib/jvm/java-6-openjdk
  /usr/lib/jvm/java-1.6.0-openjdk-1.6.0.0        # fedora
  # Debian specific paths for default JVM
  /usr/lib/jvm/default-java
  # Arch Linux specific paths for default JVM
  /usr/lib/jvm/default
  # Ubuntu specific paths for default JVM
  /usr/lib/jvm/java-11-openjdk-{libarch}    # Ubuntu 18.04 LTS
  /usr/lib/jvm/java-8-openjdk-{libarch}     # Ubuntu 15.10
  /usr/lib/jvm/java-7-openjdk-{libarch}     # Ubuntu 15.10
  /usr/lib/jvm/java-6-openjdk-{libarch}     # Ubuntu 15.10
  # OpenBSD specific paths for default JVM
  /usr/local/jdk-1.7.0
  /usr/local/jre-1.7.0
  /usr/local/jdk-1.6.0
  /usr/local/jre-1.6.0
  # FreeBSD specific paths for default JVM
  /usr/local/openjdk15
  /usr/local/openjdk14
  /usr/local/openjdk13
  /usr/local/openjdk12
  /usr/local/openjdk11
  /usr/local/openjdk8
  /usr/local/openjdk7
  # SuSE specific paths for default JVM
  /usr/lib64/jvm/java
  /usr/lib64/jvm/jre
  )

set(_JNI_JAVA_AWT_LIBRARY_TRIES)
set(_JNI_JAVA_INCLUDE_TRIES)

foreach(_java_dir IN LISTS _JNI_JAVA_DIRECTORIES_BASE)
  list(APPEND _JNI_JAVA_AWT_LIBRARY_TRIES
    ${_java_dir}/jre/lib/{libarch}
    ${_java_dir}/jre/lib
    ${_java_dir}/lib/{libarch}
    ${_java_dir}/lib
    ${_java_dir}
  )
  list(APPEND _JNI_JAVA_INCLUDE_TRIES
    ${_java_dir}/include
  )
endforeach()

JAVA_APPEND_LIBRARY_DIRECTORIES(JAVA_AWT_LIBRARY_DIRECTORIES
    ${_JNI_JAVA_AWT_LIBRARY_TRIES}
  )

set(JAVA_JVM_LIBRARY_DIRECTORIES)
foreach(dir ${JAVA_AWT_LIBRARY_DIRECTORIES})
  list(APPEND JAVA_JVM_LIBRARY_DIRECTORIES
    "${dir}"
    "${dir}/client"
    "${dir}/server"
    # IBM SDK, Java Technology Edition, specific paths
    "${dir}/j9vm"
    "${dir}/default"
    )
endforeach()

set(JAVA_AWT_INCLUDE_DIRECTORIES)
if(_JAVA_HOME)
  list(APPEND JAVA_AWT_INCLUDE_DIRECTORIES ${_JAVA_HOME}/include)
endif()
if (WIN32)
  foreach (_JNI_HINT IN LISTS _JNI_HINTS)
    list(APPEND JAVA_AWT_INCLUDE_DIRECTORIES "${_JNI_HINT}/include")
  endforeach()
  list(APPEND JAVA_AWT_INCLUDE_DIRECTORIES
    "[HKEY_LOCAL_MACHINE\\SOFTWARE\\JavaSoft\\Java Development Kit\\1.9;JavaHome]/include"
    "[HKEY_LOCAL_MACHINE\\SOFTWARE\\JavaSoft\\Java Development Kit\\1.8;JavaHome]/include"
    "[HKEY_LOCAL_MACHINE\\SOFTWARE\\JavaSoft\\Java Development Kit\\1.7;JavaHome]/include"
    "[HKEY_LOCAL_MACHINE\\SOFTWARE\\JavaSoft\\Java Development Kit\\1.6;JavaHome]/include"
    "[HKEY_LOCAL_MACHINE\\SOFTWARE\\JavaSoft\\Java Development Kit\\1.5;JavaHome]/include"
    "[HKEY_LOCAL_MACHINE\\SOFTWARE\\JavaSoft\\Java Development Kit\\1.4;JavaHome]/include"
    "[HKEY_LOCAL_MACHINE\\SOFTWARE\\JavaSoft\\Java Development Kit\\1.3;JavaHome]/include"
    "[HKEY_LOCAL_MACHINE\\SOFTWARE\\JavaSoft\\Java Development Kit\\${java_install_version};JavaHome]/include"
    )
endif()

JAVA_APPEND_LIBRARY_DIRECTORIES(JAVA_AWT_INCLUDE_DIRECTORIES
  ${_JNI_JAVA_INCLUDE_TRIES}
  )

foreach(JAVA_PROG "${JAVA_RUNTIME}" "${JAVA_COMPILE}" "${JAVA_ARCHIVE}")
  get_filename_component(jpath "${JAVA_PROG}" PATH)
  foreach(JAVA_INC_PATH ../include ../java/include ../share/java/include)
    if(EXISTS ${jpath}/${JAVA_INC_PATH})
      list(APPEND JAVA_AWT_INCLUDE_DIRECTORIES "${jpath}/${JAVA_INC_PATH}")
    endif()
  endforeach()
  foreach(JAVA_LIB_PATH
    ../lib ../jre/lib ../jre/lib/i386
    ../java/lib ../java/jre/lib ../java/jre/lib/i386
    ../share/java/lib ../share/java/jre/lib ../share/java/jre/lib/i386)
    if(EXISTS ${jpath}/${JAVA_LIB_PATH})
      list(APPEND JAVA_AWT_LIBRARY_DIRECTORIES "${jpath}/${JAVA_LIB_PATH}")
    endif()
  endforeach()
endforeach()

if(APPLE)
  if(CMAKE_FIND_FRAMEWORK STREQUAL "ONLY")
    set(_JNI_SEARCHES FRAMEWORK)
  elseif(CMAKE_FIND_FRAMEWORK STREQUAL "NEVER")
    set(_JNI_SEARCHES NORMAL)
  elseif(CMAKE_FIND_FRAMEWORK STREQUAL "LAST")
    set(_JNI_SEARCHES NORMAL FRAMEWORK)
  else()
    set(_JNI_SEARCHES FRAMEWORK NORMAL)
  endif()
  set(_JNI_FRAMEWORK_JVM NAMES JavaVM)
  set(_JNI_FRAMEWORK_JAWT "${_JNI_FRAMEWORK_JVM}")
else()
  set(_JNI_SEARCHES NORMAL)
endif()

set(_JNI_NORMAL_JVM
  NAMES jvm
  PATHS ${JAVA_JVM_LIBRARY_DIRECTORIES}
  )

set(_JNI_NORMAL_JAWT
  NAMES jawt
  PATHS ${JAVA_AWT_LIBRARY_DIRECTORIES}
  )

foreach(search ${_JNI_SEARCHES})
  find_library(JAVA_JVM_LIBRARY ${_JNI_${search}_JVM})
  find_library(JAVA_AWT_LIBRARY ${_JNI_${search}_JAWT})
  if(JAVA_JVM_LIBRARY)
    break()
  endif()
endforeach()
unset(_JNI_SEARCHES)
unset(_JNI_FRAMEWORK_JVM)
unset(_JNI_FRAMEWORK_JAWT)
unset(_JNI_NORMAL_JVM)
unset(_JNI_NORMAL_JAWT)

# Find headers matching the library.
if("${JAVA_JVM_LIBRARY};${JAVA_AWT_LIBRARY};" MATCHES "(/JavaVM.framework|-framework JavaVM);")
  set(CMAKE_FIND_FRAMEWORK ONLY)
else()
  set(CMAKE_FIND_FRAMEWORK NEVER)
endif()

# add in the include path
find_path(JAVA_INCLUDE_PATH jni.h
  ${JAVA_AWT_INCLUDE_DIRECTORIES}
)

find_path(JAVA_INCLUDE_PATH2 NAMES jni_md.h jniport.h
  PATHS
  ${JAVA_INCLUDE_PATH}
  ${JAVA_INCLUDE_PATH}/darwin
  ${JAVA_INCLUDE_PATH}/win32
  ${JAVA_INCLUDE_PATH}/linux
  ${JAVA_INCLUDE_PATH}/freebsd
  ${JAVA_INCLUDE_PATH}/openbsd
  ${JAVA_INCLUDE_PATH}/solaris
  ${JAVA_INCLUDE_PATH}/hp-ux
  ${JAVA_INCLUDE_PATH}/alpha
  ${JAVA_INCLUDE_PATH}/aix
)

find_path(JAVA_AWT_INCLUDE_PATH jawt.h
  ${JAVA_INCLUDE_PATH}
)

# Restore CMAKE_FIND_FRAMEWORK
if(DEFINED _JNI_CMAKE_FIND_FRAMEWORK)
  set(CMAKE_FIND_FRAMEWORK ${_JNI_CMAKE_FIND_FRAMEWORK})
  unset(_JNI_CMAKE_FIND_FRAMEWORK)
else()
  unset(CMAKE_FIND_FRAMEWORK)
endif()

include(${CMAKE_CURRENT_LIST_DIR}/FindPackageHandleStandardArgs.cmake)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(JNI  DEFAULT_MSG  JAVA_AWT_LIBRARY
                                                    JAVA_JVM_LIBRARY
                                                    JAVA_INCLUDE_PATH
                                                    JAVA_INCLUDE_PATH2
                                                    JAVA_AWT_INCLUDE_PATH)

mark_as_advanced(
  JAVA_AWT_LIBRARY
  JAVA_JVM_LIBRARY
  JAVA_AWT_INCLUDE_PATH
  JAVA_INCLUDE_PATH
  JAVA_INCLUDE_PATH2
)

set(JNI_LIBRARIES
  ${JAVA_AWT_LIBRARY}
  ${JAVA_JVM_LIBRARY}
)

set(JNI_INCLUDE_DIRS
  ${JAVA_INCLUDE_PATH}
  ${JAVA_INCLUDE_PATH2}
  ${JAVA_AWT_INCLUDE_PATH}
)
