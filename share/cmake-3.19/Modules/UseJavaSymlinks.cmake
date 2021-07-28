# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
UseJavaSymlinks
---------------





Helper script for UseJava.cmake
#]=======================================================================]

if (UNIX AND _JAVA_TARGET_OUTPUT_LINK)
    if (_JAVA_TARGET_OUTPUT_NAME)
        find_program(LN_EXECUTABLE
            NAMES
                ln
        )

        execute_process(
            COMMAND ${LN_EXECUTABLE} -sf "${_JAVA_TARGET_OUTPUT_NAME}" "${_JAVA_TARGET_OUTPUT_LINK}"
            WORKING_DIRECTORY ${_JAVA_TARGET_DIR}
        )
    else ()
        message(SEND_ERROR "FATAL: Can't find _JAVA_TARGET_OUTPUT_NAME")
    endif ()
endif ()
