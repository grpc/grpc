# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
UseEcos
-------

This module defines variables and macros required to build eCos application.

This file contains the following macros:
ECOS_ADD_INCLUDE_DIRECTORIES() - add the eCos include dirs
ECOS_ADD_EXECUTABLE(name source1 ...  sourceN ) - create an eCos
executable ECOS_ADJUST_DIRECTORY(VAR source1 ...  sourceN ) - adjusts
the path of the source files and puts the result into VAR

Macros for selecting the toolchain: ECOS_USE_ARM_ELF_TOOLS() - enable
the ARM ELF toolchain for the directory where it is called
ECOS_USE_I386_ELF_TOOLS() - enable the i386 ELF toolchain for the
directory where it is called ECOS_USE_PPC_EABI_TOOLS() - enable the
PowerPC toolchain for the directory where it is called

It contains the following variables: ECOS_DEFINITIONS
ECOSCONFIG_EXECUTABLE ECOS_CONFIG_FILE - defaults to ecos.ecc, if your
eCos configuration file has a different name, adjust this variable for
internal use only:

::

  ECOS_ADD_TARGET_LIB
#]=======================================================================]

# first check that ecosconfig is available
find_program(ECOSCONFIG_EXECUTABLE NAMES ecosconfig)
if(NOT ECOSCONFIG_EXECUTABLE)
  message(SEND_ERROR "ecosconfig was not found. Either include it in the system path or set it manually using ccmake.")
else()
  message(STATUS "Found ecosconfig: ${ECOSCONFIG_EXECUTABLE}")
endif()

# check that ECOS_REPOSITORY is set correctly
if (NOT EXISTS $ENV{ECOS_REPOSITORY}/ecos.db)
  message(SEND_ERROR "The environment variable ECOS_REPOSITORY is not set correctly. Set it to the directory which contains the file ecos.db")
else ()
  message(STATUS "ECOS_REPOSITORY is set to $ENV{ECOS_REPOSITORY}")
endif ()

# check that tclsh (coming with TCL) is available, otherwise ecosconfig doesn't work
find_package(Tclsh)
if (NOT TCL_TCLSH)
  message(SEND_ERROR "The TCL tclsh was not found. Please install TCL, it is required for building eCos applications.")
else ()
  message(STATUS "tlcsh found: ${TCL_TCLSH}")
endif ()

#add the globale include-diretories
#usage: ECOS_ADD_INCLUDE_DIRECTORIES()
macro(ECOS_ADD_INCLUDE_DIRECTORIES)
#check for ProjectSources.txt one level higher
  if (EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../ProjectSources.txt)
    include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../)
  else ()
    include_directories(${CMAKE_CURRENT_SOURCE_DIR}/)
  endif ()

#the ecos include directory
  include_directories(${CMAKE_CURRENT_BINARY_DIR}/ecos/install/include/)

endmacro()


#we want to compile for the xscale processor, in this case the following macro has to be called
#usage: ECOS_USE_ARM_ELF_TOOLS()
macro (ECOS_USE_ARM_ELF_TOOLS)
  set(CMAKE_CXX_COMPILER "arm-elf-c++")
  set(CMAKE_COMPILER_IS_GNUCXX 1)
  set(CMAKE_C_COMPILER "arm-elf-gcc")
  set(CMAKE_AR "arm-elf-ar")
  set(CMAKE_RANLIB "arm-elf-ranlib")
#for linking
  set(ECOS_LD_MCPU "-mcpu=xscale")
#for compiling
  add_definitions(-mcpu=xscale -mapcs-frame)
#for the obj-tools
  set(ECOS_ARCH_PREFIX "arm-elf-")
endmacro ()

#usage: ECOS_USE_PPC_EABI_TOOLS()
macro (ECOS_USE_PPC_EABI_TOOLS)
  set(CMAKE_CXX_COMPILER "powerpc-eabi-c++")
  set(CMAKE_COMPILER_IS_GNUCXX 1)
  set(CMAKE_C_COMPILER "powerpc-eabi-gcc")
  set(CMAKE_AR "powerpc-eabi-ar")
  set(CMAKE_RANLIB "powerpc-eabi-ranlib")
#for linking
  set(ECOS_LD_MCPU "")
#for compiling
  add_definitions()
#for the obj-tools
  set(ECOS_ARCH_PREFIX "powerpc-eabi-")
endmacro ()

#usage: ECOS_USE_I386_ELF_TOOLS()
macro (ECOS_USE_I386_ELF_TOOLS)
  set(CMAKE_CXX_COMPILER "i386-elf-c++")
  set(CMAKE_COMPILER_IS_GNUCXX 1)
  set(CMAKE_C_COMPILER "i386-elf-gcc")
  set(CMAKE_AR "i386-elf-ar")
  set(CMAKE_RANLIB "i386-elf-ranlib")
#for linking
  set(ECOS_LD_MCPU "")
#for compiling
  add_definitions()
#for the obj-tools
  set(ECOS_ARCH_PREFIX "i386-elf-")
endmacro ()


#since the actual sources are located one level upwards
#a "../" has to be prepended in front of every source file
#call the following macro to achieve this, the first parameter
#is the name of the new list of source files with adjusted paths,
#followed by all source files
#usage: ECOS_ADJUST_DIRECTORY(adjusted_SRCS ${my_srcs})
macro(ECOS_ADJUST_DIRECTORY _target_FILES )
  foreach (_current_FILE ${ARGN})
    get_filename_component(_abs_FILE ${_current_FILE} ABSOLUTE)
      if (NOT ${_abs_FILE} STREQUAL ${_current_FILE})
        get_filename_component(_abs_FILE ${CMAKE_CURRENT_SOURCE_DIR}/../${_current_FILE} ABSOLUTE)
      endif ()
    list(APPEND ${_target_FILES} ${_abs_FILE})
  endforeach ()
endmacro()

# the default ecos config file name
# maybe in future also out-of-source builds may be possible
set(ECOS_CONFIG_FILE ecos.ecc)

#creates the dependency from all source files on the ecos target.ld,
#adds the command for compiling ecos
macro(ECOS_ADD_TARGET_LIB)
# when building out-of-source, create the ecos/ subdir
  if(NOT EXISTS ${CMAKE_CURRENT_BINARY_DIR}/ecos)
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/ecos)
  endif()

#sources depend on target.ld
  set_source_files_properties(
    ${ARGN}
    PROPERTIES
    OBJECT_DEPENDS
    ${CMAKE_CURRENT_BINARY_DIR}/ecos/install/lib/target.ld
  )

  add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/ecos/install/lib/target.ld
    COMMAND sh -c \"make -C ${CMAKE_CURRENT_BINARY_DIR}/ecos || exit -1\; if [ -e ${CMAKE_CURRENT_BINARY_DIR}/ecos/install/lib/target.ld ] \; then touch ${CMAKE_CURRENT_BINARY_DIR}/ecos/install/lib/target.ld\; fi\"
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/ecos/makefile
  )

  add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/ecos/makefile
    COMMAND sh -c \" cd ${CMAKE_CURRENT_BINARY_DIR}/ecos\; ${ECOSCONFIG_EXECUTABLE} --config=${CMAKE_CURRENT_SOURCE_DIR}/ecos/${ECOS_CONFIG_FILE} tree || exit -1\;\"
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/ecos/${ECOS_CONFIG_FILE}
  )

  add_custom_target( ecos make -C ${CMAKE_CURRENT_BINARY_DIR}/ecos/ DEPENDS  ${CMAKE_CURRENT_BINARY_DIR}/ecos/makefile )
endmacro()

# get the directory of the current file, used later on in the file
get_filename_component( ECOS_CMAKE_MODULE_DIR ${CMAKE_CURRENT_LIST_FILE} PATH)

#macro for creating an executable ecos application
#the first parameter is the name of the executable,
#the second is the list of all source files (where the path
#has been adjusted beforehand by calling ECOS_ADJUST_DIRECTORY()
#usage: ECOS_ADD_EXECUTABLE(my_app ${adjusted_SRCS})
macro(ECOS_ADD_EXECUTABLE _exe_NAME )
  #definitions, valid for all ecos projects
  #the optimization and "-g" for debugging has to be enabled
  #in the project-specific CMakeLists.txt
  add_definitions(-D__ECOS__=1 -D__ECOS=1)
  set(ECOS_DEFINITIONS -Wall -Wno-long-long -pipe -fno-builtin)

#the executable depends on ecos target.ld
  ECOS_ADD_TARGET_LIB(${ARGN})

# when using nmake makefiles, the custom buildtype suppresses the default cl.exe flags
# and the rules for creating objects are adjusted for gcc
  set(CMAKE_BUILD_TYPE CUSTOM_ECOS_BUILD)
  set(CMAKE_C_COMPILE_OBJECT     "<CMAKE_C_COMPILER> <DEFINES> <INCLUDES> <FLAGS> -o <OBJECT> -c <SOURCE>")
  set(CMAKE_CXX_COMPILE_OBJECT   "<CMAKE_CXX_COMPILER> <DEFINES> <INCLUDES> <FLAGS> -o <OBJECT> -c <SOURCE>")
# special link commands for ecos-executables
  set(CMAKE_CXX_LINK_EXECUTABLE  "<CMAKE_CXX_COMPILER> <CMAKE_CXX_LINK_FLAGS> <OBJECTS> -o <TARGET> ${_ecos_EXTRA_LIBS} -nostdlib -nostartfiles -L${CMAKE_CURRENT_BINARY_DIR}/ecos/install/lib -Ttarget.ld ${ECOS_LD_MCPU}")
  set(CMAKE_C_LINK_EXECUTABLE    "<CMAKE_C_COMPILER> <CMAKE_C_LINK_FLAGS> <OBJECTS> -o <TARGET> ${_ecos_EXTRA_LIBS} -nostdlib -nostartfiles -L${CMAKE_CURRENT_BINARY_DIR}/ecos/install/lib -Ttarget.ld ${ECOS_LD_MCPU}")
# some strict compiler flags
  set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wstrict-prototypes")
  set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Woverloaded-virtual -fno-rtti -Wctor-dtor-privacy -fno-strict-aliasing -fno-exceptions")

  add_executable(${_exe_NAME} ${ARGN})
  set_target_properties(${_exe_NAME} PROPERTIES SUFFIX ".elf")

#create a binary file
  add_custom_command(
    TARGET ${_exe_NAME}
    POST_BUILD
    COMMAND ${ECOS_ARCH_PREFIX}objcopy
    ARGS -O binary ${CMAKE_CURRENT_BINARY_DIR}/${_exe_NAME}.elf ${CMAKE_CURRENT_BINARY_DIR}/${_exe_NAME}.bin
  )

#and an srec file
  add_custom_command(
    TARGET ${_exe_NAME}
    POST_BUILD
    COMMAND ${ECOS_ARCH_PREFIX}objcopy
    ARGS -O srec ${CMAKE_CURRENT_BINARY_DIR}/${_exe_NAME}.elf ${CMAKE_CURRENT_BINARY_DIR}/${_exe_NAME}.srec
  )

#add the created files to the clean-files
  set_property(DIRECTORY APPEND PROPERTY ADDITIONAL_CLEAN_FILES
    "${CMAKE_CURRENT_BINARY_DIR}/${_exe_NAME}.bin"
    "${CMAKE_CURRENT_BINARY_DIR}/${_exe_NAME}.srec"
    "${CMAKE_CURRENT_BINARY_DIR}/${_exe_NAME}.lst")

  add_custom_target(ecosclean ${CMAKE_COMMAND} -DECOS_DIR=${CMAKE_CURRENT_BINARY_DIR}/ecos/ -P ${ECOS_CMAKE_MODULE_DIR}/ecos_clean.cmake  )
  add_custom_target(normalclean ${CMAKE_MAKE_PROGRAM} clean WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
  add_dependencies (ecosclean normalclean)


  add_custom_target( listing
    COMMAND echo -e   \"\\n--- Symbols sorted by address ---\\n\" > ${CMAKE_CURRENT_BINARY_DIR}/${_exe_NAME}.lst
    COMMAND ${ECOS_ARCH_PREFIX}nm -S -C -n ${CMAKE_CURRENT_BINARY_DIR}/${_exe_NAME}.elf >> ${CMAKE_CURRENT_BINARY_DIR}/${_exe_NAME}.lst
    COMMAND echo -e \"\\n--- Symbols sorted by size ---\\n\" >> ${CMAKE_CURRENT_BINARY_DIR}/${_exe_NAME}.lst
    COMMAND ${ECOS_ARCH_PREFIX}nm -S -C -r --size-sort ${CMAKE_CURRENT_BINARY_DIR}/${_exe_NAME}.elf >> ${CMAKE_CURRENT_BINARY_DIR}/${_exe_NAME}.lst
    COMMAND echo -e \"\\n--- Full assembly listing ---\\n\" >> ${CMAKE_CURRENT_BINARY_DIR}/${_exe_NAME}.lst
    COMMAND ${ECOS_ARCH_PREFIX}objdump -S -x -d -C ${CMAKE_CURRENT_BINARY_DIR}/${_exe_NAME}.elf >> ${CMAKE_CURRENT_BINARY_DIR}/${_exe_NAME}.lst )

endmacro()

