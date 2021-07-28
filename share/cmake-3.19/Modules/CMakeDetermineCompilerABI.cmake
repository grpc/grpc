# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.


# Function to compile a source file to identify the compiler ABI.
# This is used internally by CMake and should not be included by user
# code.

include(${CMAKE_ROOT}/Modules/CMakeParseImplicitIncludeInfo.cmake)
include(${CMAKE_ROOT}/Modules/CMakeParseImplicitLinkInfo.cmake)
include(CMakeTestCompilerCommon)

function(CMAKE_DETERMINE_COMPILER_ABI lang src)
  if(NOT DEFINED CMAKE_${lang}_ABI_COMPILED)
    message(CHECK_START "Detecting ${lang} compiler ABI info")

    # Compile the ABI identification source.
    set(BIN "${CMAKE_PLATFORM_INFO_DIR}/CMakeDetermineCompilerABI_${lang}.bin")
    set(CMAKE_FLAGS )
    set(COMPILE_DEFINITIONS )
    if(DEFINED CMAKE_${lang}_VERBOSE_FLAG)
      set(CMAKE_FLAGS "-DEXE_LINKER_FLAGS=${CMAKE_${lang}_VERBOSE_FLAG}")
      set(COMPILE_DEFINITIONS "${CMAKE_${lang}_VERBOSE_FLAG}")
    endif()
    if(DEFINED CMAKE_${lang}_VERBOSE_COMPILE_FLAG)
      set(COMPILE_DEFINITIONS "${CMAKE_${lang}_VERBOSE_COMPILE_FLAG}")
    endif()
    if(NOT "x${CMAKE_${lang}_COMPILER_ID}" STREQUAL "xMSVC")
      # Avoid adding our own platform standard libraries for compilers
      # from which we might detect implicit link libraries.
      list(APPEND CMAKE_FLAGS "-DCMAKE_${lang}_STANDARD_LIBRARIES=")
    endif()
    __TestCompiler_setTryCompileTargetType()

    # Avoid failing ABI detection on warnings.
    string(REGEX REPLACE "(^| )-Werror([= ][^ ]*)?( |$)" " " CMAKE_${lang}_FLAGS "${CMAKE_${lang}_FLAGS}")

    # Save the current LC_ALL, LC_MESSAGES, and LANG environment variables
    # and set them to "C" that way GCC's "search starts here" text is in
    # English and we can grok it.
    set(_orig_lc_all      $ENV{LC_ALL})
    set(_orig_lc_messages $ENV{LC_MESSAGES})
    set(_orig_lang        $ENV{LANG})
    set(ENV{LC_ALL}      C)
    set(ENV{LC_MESSAGES} C)
    set(ENV{LANG}        C)

    try_compile(CMAKE_${lang}_ABI_COMPILED
      ${CMAKE_BINARY_DIR} ${src}
      CMAKE_FLAGS ${CMAKE_FLAGS}
                  # Ignore unused flags when we are just determining the ABI.
                  "--no-warn-unused-cli"
      COMPILE_DEFINITIONS ${COMPILE_DEFINITIONS}
      OUTPUT_VARIABLE OUTPUT
      COPY_FILE "${BIN}"
      COPY_FILE_ERROR _copy_error
      __CMAKE_INTERNAL ABI
      )

    # Restore original LC_ALL, LC_MESSAGES, and LANG
    set(ENV{LC_ALL}      ${_orig_lc_all})
    set(ENV{LC_MESSAGES} ${_orig_lc_messages})
    set(ENV{LANG}        ${_orig_lang})

    # Move result from cache to normal variable.
    set(CMAKE_${lang}_ABI_COMPILED ${CMAKE_${lang}_ABI_COMPILED})
    unset(CMAKE_${lang}_ABI_COMPILED CACHE)
    if(CMAKE_${lang}_ABI_COMPILED AND _copy_error)
      set(CMAKE_${lang}_ABI_COMPILED 0)
    endif()
    set(CMAKE_${lang}_ABI_COMPILED ${CMAKE_${lang}_ABI_COMPILED} PARENT_SCOPE)

    # Load the resulting information strings.
    if(CMAKE_${lang}_ABI_COMPILED)
      message(CHECK_PASS "done")
      file(APPEND ${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CMakeOutput.log
        "Detecting ${lang} compiler ABI info compiled with the following output:\n${OUTPUT}\n\n")
      file(STRINGS "${BIN}" ABI_STRINGS LIMIT_COUNT 2 REGEX "INFO:[A-Za-z0-9_]+\\[[^]]*\\]")
      foreach(info ${ABI_STRINGS})
        if("${info}" MATCHES "INFO:sizeof_dptr\\[0*([^]]*)\\]")
          set(ABI_SIZEOF_DPTR "${CMAKE_MATCH_1}")
        endif()
        if("${info}" MATCHES "INFO:abi\\[([^]]*)\\]")
          set(ABI_NAME "${CMAKE_MATCH_1}")
        endif()
      endforeach()

      if(ABI_SIZEOF_DPTR)
        set(CMAKE_${lang}_SIZEOF_DATA_PTR "${ABI_SIZEOF_DPTR}" PARENT_SCOPE)
      elseif(CMAKE_${lang}_SIZEOF_DATA_PTR_DEFAULT)
        set(CMAKE_${lang}_SIZEOF_DATA_PTR "${CMAKE_${lang}_SIZEOF_DATA_PTR_DEFAULT}" PARENT_SCOPE)
      endif()

      if(ABI_NAME)
        set(CMAKE_${lang}_COMPILER_ABI "${ABI_NAME}" PARENT_SCOPE)
      endif()

      # Parse implicit include directory for this language, if available.
      if(CMAKE_${lang}_VERBOSE_FLAG)
        set (implicit_incdirs "")
        cmake_parse_implicit_include_info("${OUTPUT}" "${lang}"
          implicit_incdirs log rv)
        file(APPEND ${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CMakeOutput.log
          "Parsed ${lang} implicit include dir info from above output: rv=${rv}\n${log}\n\n")
        if("${rv}" STREQUAL "done")
          # Entries that we have been told to explicitly pass as standard include
          # directories will not be implicitly added by the compiler.
          if(CMAKE_${lang}_STANDARD_INCLUDE_DIRECTORIES)
            list(REMOVE_ITEM implicit_incdirs ${CMAKE_${lang}_STANDARD_INCLUDE_DIRECTORIES})
          endif()

          # We parsed implicit include directories, so override the default initializer.
          set(_CMAKE_${lang}_IMPLICIT_INCLUDE_DIRECTORIES_INIT "${implicit_incdirs}")
        endif()
      endif()
      set(CMAKE_${lang}_IMPLICIT_INCLUDE_DIRECTORIES "${_CMAKE_${lang}_IMPLICIT_INCLUDE_DIRECTORIES_INIT}" PARENT_SCOPE)

      # Parse implicit linker information for this language, if available.
      set(implicit_dirs "")
      set(implicit_libs "")
      set(implicit_fwks "")
      if(CMAKE_${lang}_VERBOSE_FLAG)
        CMAKE_PARSE_IMPLICIT_LINK_INFO("${OUTPUT}" implicit_libs implicit_dirs implicit_fwks log
          "${CMAKE_${lang}_IMPLICIT_OBJECT_REGEX}")
        file(APPEND ${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CMakeOutput.log
          "Parsed ${lang} implicit link information from above output:\n${log}\n\n")
      endif()
      # for VS IDE Intel Fortran we have to figure out the
      # implicit link path for the fortran run time using
      # a try-compile
      if("${lang}" MATCHES "Fortran"
          AND "${CMAKE_GENERATOR}" MATCHES "Visual Studio")
        message(CHECK_START "Determine Intel Fortran Compiler Implicit Link Path")
        # Build a sample project which reports symbols.
        try_compile(IFORT_LIB_PATH_COMPILED
          ${CMAKE_BINARY_DIR}/CMakeFiles/IntelVSImplicitPath
          ${CMAKE_ROOT}/Modules/IntelVSImplicitPath
          IntelFortranImplicit
          CMAKE_FLAGS
          "-DCMAKE_Fortran_FLAGS:STRING=${CMAKE_Fortran_FLAGS}"
          OUTPUT_VARIABLE _output)
        file(WRITE
          "${CMAKE_BINARY_DIR}/CMakeFiles/IntelVSImplicitPath/output.txt"
          "${_output}")
        include(${CMAKE_BINARY_DIR}/CMakeFiles/IntelVSImplicitPath/output.cmake OPTIONAL)
        message(CHECK_PASS "done")
      endif()

      # Implicit link libraries cannot be used explicitly for multiple
      # OS X architectures, so we skip it.
      if(DEFINED CMAKE_OSX_ARCHITECTURES)
        if("${CMAKE_OSX_ARCHITECTURES}" MATCHES ";")
          set(implicit_libs "")
        endif()
      endif()

      set(CMAKE_${lang}_IMPLICIT_LINK_LIBRARIES "${implicit_libs}" PARENT_SCOPE)
      set(CMAKE_${lang}_IMPLICIT_LINK_DIRECTORIES "${implicit_dirs}" PARENT_SCOPE)
      set(CMAKE_${lang}_IMPLICIT_LINK_FRAMEWORK_DIRECTORIES "${implicit_fwks}" PARENT_SCOPE)

      # Detect library architecture directory name.
      if(CMAKE_LIBRARY_ARCHITECTURE_REGEX)
        foreach(dir ${implicit_dirs})
          if("${dir}" MATCHES "/lib/${CMAKE_LIBRARY_ARCHITECTURE_REGEX}$")
            get_filename_component(arch "${dir}" NAME)
            set(CMAKE_${lang}_LIBRARY_ARCHITECTURE "${arch}" PARENT_SCOPE)
            break()
          endif()
        endforeach()
      elseif(CMAKE_CXX_COMPILER_ID STREQUAL QCC)
        foreach(dir ${implicit_dirs})
          if (dir MATCHES "/lib$")
            get_filename_component(assumedArchDir "${dir}" DIRECTORY)
            get_filename_component(archParentDir "${assumedArchDir}" DIRECTORY)
            if (archParentDir STREQUAL CMAKE_SYSROOT)
              get_filename_component(archDirName "${assumedArchDir}" NAME)
              set(CMAKE_${lang}_LIBRARY_ARCHITECTURE "${archDirName}" PARENT_SCOPE)
              break()
            endif()
          endif()
        endforeach()
      endif()

    else()
      message(CHECK_FAIL "failed")
      file(APPEND ${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CMakeError.log
        "Detecting ${lang} compiler ABI info failed to compile with the following output:\n${OUTPUT}\n${_copy_error}\n\n")
    endif()
  endif()
endfunction()
