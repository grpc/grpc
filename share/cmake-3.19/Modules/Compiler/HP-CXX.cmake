set(CMAKE_CXX_VERBOSE_FLAG "-v")

set(CMAKE_CXX_CREATE_ASSEMBLY_SOURCE "<CMAKE_CXX_COMPILER> <DEFINES> <INCLUDES> <FLAGS> -S <SOURCE> -o <ASSEMBLY_SOURCE>")
set(CMAKE_CXX_CREATE_PREPROCESSED_SOURCE "<CMAKE_CXX_COMPILER> <DEFINES> <INCLUDES> <FLAGS> -E <SOURCE> > <PREPROCESSED_SOURCE>")

set(CMAKE_CXX_LINKER_WRAPPER_FLAG "-Wl,")
set(CMAKE_CXX_LINKER_WRAPPER_FLAG_SEP ",")

# HP aCC since version 3.80 supports the flag +hpxstd98 to get ANSI C++98
# template support. It is known that version 6.25 doesn't need that flag.
# Current assumption: the flag is needed for every version from 3.80 to 4
# to get it working.
if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 4 AND
   NOT CMAKE_CXX_COMPILER_VERSION VERSION_LESS 3.80)
  set(CMAKE_CXX98_STANDARD_COMPILE_OPTION "+hpxstd98")
endif()
