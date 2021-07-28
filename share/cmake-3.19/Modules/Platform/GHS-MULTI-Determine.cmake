# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#Setup Green Hills MULTI specific compilation information

if(CMAKE_HOST_UNIX)
  set(GHS_OS_ROOT "/usr/ghs" CACHE PATH "GHS platform OS search root directory")
else()
  set(GHS_OS_ROOT "C:/ghs" CACHE PATH "GHS platform OS search root directory")
endif()
mark_as_advanced(GHS_OS_ROOT)

set(GHS_OS_DIR "NOTFOUND" CACHE PATH "GHS platform OS directory")
mark_as_advanced(GHS_OS_DIR)

set(GHS_OS_DIR_OPTION "-os_dir " CACHE STRING "GHS compiler OS option")
mark_as_advanced(GHS_OS_DIR_OPTION)

#set GHS_OS_DIR if not set by user
if(NOT GHS_OS_DIR)
  if(EXISTS ${GHS_OS_ROOT})

    #get all directories in root directory
    FILE(GLOB GHS_CANDIDATE_OS_DIRS
      LIST_DIRECTORIES true RELATIVE ${GHS_OS_ROOT} ${GHS_OS_ROOT}/*)
    FILE(GLOB GHS_CANDIDATE_OS_FILES
      LIST_DIRECTORIES false RELATIVE ${GHS_OS_ROOT} ${GHS_OS_ROOT}/*)
    if(GHS_CANDIDATE_OS_FILES)
      list(REMOVE_ITEM GHS_CANDIDATE_OS_DIRS ${GHS_CANDIDATE_OS_FILES})
    endif ()

    #filter based on platform name
    if(GHS_TARGET_PLATFORM MATCHES "integrity")
      list(FILTER GHS_CANDIDATE_OS_DIRS INCLUDE REGEX "int[0-9][0-9][0-9][0-9a-z]")
    else() #fall-back for standalone
      unset(GHS_CANDIDATE_OS_DIRS)
      set(GHS_OS_DIR "IGNORE")
    endif()

    if(GHS_CANDIDATE_OS_DIRS)
      list(SORT GHS_CANDIDATE_OS_DIRS)
      list(GET GHS_CANDIDATE_OS_DIRS -1 GHS_OS_DIR)
      string(CONCAT GHS_OS_DIR ${GHS_OS_ROOT} "/" ${GHS_OS_DIR})
    endif()

    #update cache with new value
    set(GHS_OS_DIR "${GHS_OS_DIR}" CACHE PATH "GHS platform OS directory" FORCE)
  endif()
endif()

set(GHS_BSP_NAME "IGNORE" CACHE STRING "BSP name")

set(GHS_CUSTOMIZATION "" CACHE FILEPATH "optional GHS customization")
mark_as_advanced(GHS_CUSTOMIZATION)
set(GHS_GPJ_MACROS "" CACHE STRING "optional GHS macros generated in the .gpjs for legacy reasons")
mark_as_advanced(GHS_GPJ_MACROS)
