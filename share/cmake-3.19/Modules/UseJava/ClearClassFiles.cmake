# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

# This script deletes compiled Java class files.

if(CMAKE_JAVA_CLASS_OUTPUT_PATH)
  if(EXISTS "${CMAKE_JAVA_CLASS_OUTPUT_PATH}/java_class_filelist")
    file(STRINGS "${CMAKE_JAVA_CLASS_OUTPUT_PATH}/java_class_filelist" classes)
    list(TRANSFORM classes PREPEND "${CMAKE_JAVA_CLASS_OUTPUT_PATH}/")
    if(classes)
      file(REMOVE ${classes})
      message(STATUS "Clean class files from previous build")
    endif()
  endif()
else()
  message(FATAL_ERROR "Can't find CMAKE_JAVA_CLASS_OUTPUT_PATH")
endif()
