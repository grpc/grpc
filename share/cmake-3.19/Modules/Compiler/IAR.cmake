# This file is processed when the IAR compiler is used for a C or C++ file
# Documentation can be downloaded here: http://www.iar.com/website1/1.0.1.0/675/1/
# The initial feature request is here: https://gitlab.kitware.com/cmake/cmake/-/issues/10176
# It also contains additional links and information.
# See USER GUIDES -> C/C++ Development Guide and ReleaseNotes for EWARM:
# version 6.30.8: http://supp.iar.com/FilesPublic/UPDINFO/006607/arm/doc/infocenter/index.ENU.html
# version 7.60.1: http://supp.iar.com/FilesPublic/UPDINFO/011006/arm/doc/infocenter/index.ENU.html
# version 8.10.1: http://netstorage.iar.com/SuppDB/Public/UPDINFO/011854/arm/doc/infocenter/index.ENU.html

# The IAR internal compiler platform generations (Predefined symbol __IAR_SYSTEMS_ICC__):
#  9 and higher means C11 and C++14 as language default (EWARM v8.x, EWRX v4.x and higher)
#  8 means C99 and C++03 as language default (EWARM v6.x, v7.x. EWRX v2.x, 3.x)
#  7 and lower means C89 and EC++ as language default. (EWARM v5.x and lower)

# C/C++ Standard versions
#
# IAR typically only supports one C and C++ Standard version,
# the exception is C89 which is always supported and can be selected
# if its not the default
#
# C++ is trickier, there were historically 3 switches,
# and some IAR versions support multiple of those.
# they are --eec++, --ec++ and --c++ and where used to
# enable various language features like exceptions
#
# recent versions only have --c++ for full support
# but can choose to disable features with further arguments
#
# C/C++ Standard compliance
#
# IAR has 3 modes: default, strict and extended
# the extended mode is needed for popular libraries like CMSIS
#
# "Silent" Operation
#
# this really is different to most programs I know.
# nothing meaningful from the operation is lost, just some redundant
# code and data size printouts (that can be inspected with common tools).

# This module is shared by multiple languages; use include blocker.
include_guard()

macro(__compiler_iar_ilink lang)
  set(CMAKE_EXECUTABLE_SUFFIX ".elf")
  set(CMAKE_${lang}_OUTPUT_EXTENSION ".o")
  if (${lang} STREQUAL "C" OR ${lang} STREQUAL "CXX")
    set(CMAKE_${lang}_COMPILE_OBJECT             "<CMAKE_${lang}_COMPILER> ${CMAKE_IAR_${lang}_FLAG} --silent <SOURCE> <DEFINES> <INCLUDES> <FLAGS> -o <OBJECT>")
    set(CMAKE_${lang}_CREATE_PREPROCESSED_SOURCE "<CMAKE_${lang}_COMPILER> ${CMAKE_IAR_${lang}_FLAG} --silent <SOURCE> <DEFINES> <INCLUDES> <FLAGS> --preprocess=cnl <PREPROCESSED_SOURCE>")
    set(CMAKE_${lang}_CREATE_ASSEMBLY_SOURCE     "<CMAKE_${lang}_COMPILER> ${CMAKE_IAR_${lang}_FLAG} --silent <SOURCE> <DEFINES> <INCLUDES> <FLAGS> -lAH <ASSEMBLY_SOURCE> -o <OBJECT>.dummy")

    set(CMAKE_${lang}_RESPONSE_FILE_LINK_FLAG "-f ")
    set(CMAKE_DEPFILE_FLAGS_${lang} "--dependencies=ns <DEPFILE>")

    string(APPEND CMAKE_${lang}_FLAGS_INIT " ")
    string(APPEND CMAKE_${lang}_FLAGS_DEBUG_INIT " -r")
    string(APPEND CMAKE_${lang}_FLAGS_MINSIZEREL_INIT " -Ohz -DNDEBUG")
    string(APPEND CMAKE_${lang}_FLAGS_RELEASE_INIT " -Oh -DNDEBUG")
    string(APPEND CMAKE_${lang}_FLAGS_RELWITHDEBINFO_INIT " -Oh -r -DNDEBUG")
  endif()

  if (${lang} STREQUAL "ASM")
    string(APPEND CMAKE_ASM_FLAGS_INIT " ")
    string(APPEND CMAKE_ASM_FLAGS_DEBUG_INIT " -r")
    string(APPEND CMAKE_ASM_FLAGS_MINSIZEREL_INIT " -DNDEBUG")
    string(APPEND CMAKE_ASM_FLAGS_RELEASE_INIT " -DNDEBUG")
    string(APPEND CMAKE_ASM_FLAGS_RELWITHDEBINFO_INIT " -r -DNDEBUG")
  endif()

  set(CMAKE_${lang}_LINK_EXECUTABLE "\"${CMAKE_IAR_LINKER}\" --silent <OBJECTS> <CMAKE_${lang}_LINK_FLAGS> <LINK_FLAGS> <LINK_LIBRARIES> -o <TARGET>")
  set(CMAKE_${lang}_CREATE_STATIC_LIBRARY "\"${CMAKE_IAR_ARCHIVE}\" <TARGET> --create <LINK_FLAGS> <OBJECTS>")
  set(CMAKE_${lang}_ARCHIVE_CREATE "\"${CMAKE_IAR_ARCHIVE}\" <TARGET> --create <LINK_FLAGS> <OBJECTS>")
  set(CMAKE_${lang}_ARCHIVE_APPEND "\"${CMAKE_IAR_ARCHIVE}\" <TARGET> --replace <LINK_FLAGS> <OBJECTS>")
  set(CMAKE_${lang}_ARCHIVE_FINISH "")

  set(CMAKE_LINKER "${CMAKE_IAR_LINKER}" CACHE FILEPATH "The IAR linker" FORCE)
  set(CMAKE_AR "${CMAKE_IAR_ARCHIVE}" CACHE FILEPATH "The IAR archiver" FORCE)
endmacro()

macro(__compiler_iar_xlink lang)
  set(CMAKE_EXECUTABLE_SUFFIX ".bin")
  if (${lang} STREQUAL "C" OR ${lang} STREQUAL "CXX")

    set(CMAKE_${lang}_COMPILE_OBJECT             "<CMAKE_${lang}_COMPILER> ${CMAKE_IAR_${lang}_FLAG} --silent <SOURCE> <DEFINES> <INCLUDES> <FLAGS> -o <OBJECT>")
    set(CMAKE_${lang}_CREATE_PREPROCESSED_SOURCE "<CMAKE_${lang}_COMPILER> ${CMAKE_IAR_${lang}_FLAG} --silent <SOURCE> <DEFINES> <INCLUDES> <FLAGS> --preprocess=cnl <PREPROCESSED_SOURCE>")
    set(CMAKE_${lang}_CREATE_ASSEMBLY_SOURCE     "<CMAKE_${lang}_COMPILER> ${CMAKE_IAR_${lang}_FLAG} --silent <SOURCE> <DEFINES> <INCLUDES> <FLAGS> -lAH <ASSEMBLY_SOURCE> -o <OBJECT>.dummy")

    set(CMAKE_${lang}_RESPONSE_FILE_LINK_FLAG "-f ")
    set(CMAKE_DEPFILE_FLAGS_${lang} "--dependencies=ns <DEPFILE>")

    string(APPEND CMAKE_${lang}_FLAGS_INIT " ")
    string(APPEND CMAKE_${lang}_FLAGS_DEBUG_INIT " -r")
    string(APPEND CMAKE_${lang}_FLAGS_MINSIZEREL_INIT " -Ohz -DNDEBUG")
    string(APPEND CMAKE_${lang}_FLAGS_RELEASE_INIT " -Oh -DNDEBUG")
    string(APPEND CMAKE_${lang}_FLAGS_RELWITHDEBINFO_INIT " -Oh -r -DNDEBUG")
  endif()

  if (${lang} STREQUAL "ASM")
    string(APPEND CMAKE_ASM_FLAGS_INIT " ")
    string(APPEND CMAKE_ASM_FLAGS_DEBUG_INIT " -r")
    string(APPEND CMAKE_ASM_FLAGS_MINSIZEREL_INIT " -DNDEBUG")
    string(APPEND CMAKE_ASM_FLAGS_RELEASE_INIT " -DNDEBUG")
    string(APPEND CMAKE_ASM_FLAGS_RELWITHDEBINFO_INIT " -r -DNDEBUG")
  endif()

  set(CMAKE_${lang}_LINK_EXECUTABLE "\"${CMAKE_IAR_LINKER}\" -S <OBJECTS> <CMAKE_${lang}_LINK_FLAGS> <LINK_FLAGS> <LINK_LIBRARIES> -o <TARGET>")
  set(CMAKE_${lang}_CREATE_STATIC_LIBRARY "\"${CMAKE_IAR_AR}\" <TARGET> <LINK_FLAGS> <OBJECTS>")
  set(CMAKE_${lang}_ARCHIVE_CREATE "\"${CMAKE_IAR_AR}\" <TARGET> <LINK_FLAGS> <OBJECTS>")
  set(CMAKE_${lang}_ARCHIVE_APPEND "")
  set(CMAKE_${lang}_ARCHIVE_FINISH "")

  set(CMAKE_LIBRARY_PATH_FLAG "-I")

  set(CMAKE_LINKER "${CMAKE_IAR_LINKER}" CACHE FILEPATH "The IAR linker" FORCE)
  set(CMAKE_AR "${CMAKE_IAR_AR}" CACHE FILEPATH "The IAR archiver" FORCE)
endmacro()
