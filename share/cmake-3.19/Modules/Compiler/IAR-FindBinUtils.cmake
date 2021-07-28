if(NOT DEFINED _CMAKE_PROCESSING_LANGUAGE OR _CMAKE_PROCESSING_LANGUAGE STREQUAL "")
  message(FATAL_ERROR "Internal error: _CMAKE_PROCESSING_LANGUAGE is not set")
endif()

# Try to find tools in the same directory as the compiler itself
get_filename_component(__iar_hint_1 "${CMAKE_${_CMAKE_PROCESSING_LANGUAGE}_COMPILER}" REALPATH)
get_filename_component(__iar_hint_1 "${__iar_hint_1}" DIRECTORY)

get_filename_component(__iar_hint_2 "${CMAKE_${_CMAKE_PROCESSING_LANGUAGE}_COMPILER}" DIRECTORY)

set(__iar_hints "${__iar_hint_1}" "${__iar_hint_2}")

if("${CMAKE_${_CMAKE_PROCESSING_LANGUAGE}_COMPILER_ARCHITECTURE_ID}" STREQUAL "ARM" OR
   "${CMAKE_${_CMAKE_PROCESSING_LANGUAGE}_COMPILER_ARCHITECTURE_ID}" STREQUAL "RX" OR
   "${CMAKE_${_CMAKE_PROCESSING_LANGUAGE}_COMPILER_ARCHITECTURE_ID}" STREQUAL "RH850" OR
   "${CMAKE_${_CMAKE_PROCESSING_LANGUAGE}_COMPILER_ARCHITECTURE_ID}" STREQUAL "RL78" OR
   "${CMAKE_${_CMAKE_PROCESSING_LANGUAGE}_COMPILER_ARCHITECTURE_ID}" STREQUAL "RISCV")

  string(TOLOWER "${CMAKE_${_CMAKE_PROCESSING_LANGUAGE}_COMPILER_ARCHITECTURE_ID}" _archid_lower)

  # Find linker
  find_program(CMAKE_IAR_LINKER ilink${_archid_lower} HINTS ${__iar_hints}
      DOC "The IAR ILINK linker")
  find_program(CMAKE_IAR_ARCHIVE iarchive HINTS ${__iar_hints}
      DOC "The IAR archiver")

  # Find utility tools
  find_program(CMAKE_IAR_ELFTOOL ielftool HINTS ${__iar_hints}
      DOC "The IAR ELF Tool")
    find_program(CMAKE_IAR_ELFDUMP ielfdump${_archid_lower} HINTS ${__iar_hints}
      DOC "The IAR ELF Dumper")
  find_program(CMAKE_IAR_OBJMANIP iobjmanip HINTS ${__iar_hints}
      DOC "The IAR ELF Object Tool")
  find_program(CMAKE_IAR_SYMEXPORT isymexport HINTS ${__iar_hints}
      DOC "The IAR Absolute Symbol Exporter")
  mark_as_advanced(CMAKE_IAR_LINKER CMAKE_IAR_ARCHIVE CMAKE_IAR_ELFTOOL CMAKE_IAR_ELFDUMP CMAKE_IAR_OBJMANIP CMAKE_IAR_SYMEXPORT)

  set(CMAKE_${_CMAKE_PROCESSING_LANGUAGE}_COMPILER_CUSTOM_CODE
"set(CMAKE_IAR_LINKER \"${CMAKE_IAR_LINKER}\")
set(CMAKE_IAR_ARCHIVE \"${CMAKE_IAR_ARCHIVE}\")
set(CMAKE_IAR_ELFTOOL \"${CMAKE_IAR_ELFTOOL}\")
set(CMAKE_IAR_ELFDUMP \"${CMAKE_IAR_ELFDUMP}\")
set(CMAKE_IAR_OBJMANIP \"${CMAKE_IAR_OBJMANIP}\")
set(CMAKE_IAR_LINKER \"${CMAKE_IAR_LINKER}\")
")

elseif("${CMAKE_${_CMAKE_PROCESSING_LANGUAGE}_COMPILER_ARCHITECTURE_ID}" STREQUAL "AVR" OR
       "${CMAKE_${_CMAKE_PROCESSING_LANGUAGE}_COMPILER_ARCHITECTURE_ID}" STREQUAL "MSP430" OR
       "${CMAKE_${_CMAKE_PROCESSING_LANGUAGE}_COMPILER_ARCHITECTURE_ID}" STREQUAL "V850" OR
       "${CMAKE_${_CMAKE_PROCESSING_LANGUAGE}_COMPILER_ARCHITECTURE_ID}" STREQUAL "8051")

  # Find the "xlink" linker and "xar" archiver:
  find_program(CMAKE_IAR_LINKER xlink HINTS ${__iar_hints}
      DOC "The IAR XLINK linker")
  find_program(CMAKE_IAR_AR xar HINTS ${__iar_hints}
      DOC "The IAR archiver")
  mark_as_advanced(CMAKE_IAR_LINKER CMAKE_IAR_AR)

  set(CMAKE_${_CMAKE_PROCESSING_LANGUAGE}_COMPILER_CUSTOM_CODE
"set(CMAKE_IAR_LINKER \"${CMAKE_IAR_LINKER}\")
set(CMAKE_IAR_AR \"${CMAKE_IAR_AR}\")
")
endif()
