include(Compiler/XL)
__compiler_xl(C)
string(APPEND CMAKE_C_FLAGS_RELEASE_INIT " -DNDEBUG")
string(APPEND CMAKE_C_FLAGS_MINSIZEREL_INIT " -DNDEBUG")

# -qthreaded = Ensures that all optimizations will be thread-safe
string(APPEND CMAKE_C_FLAGS_INIT " -qthreaded")

if (CMAKE_C_COMPILER_VERSION VERSION_GREATER_EQUAL 10.1)
  set(CMAKE_C90_STANDARD_COMPILE_OPTION "-qlanglvl=stdc89")
  set(CMAKE_C90_EXTENSION_COMPILE_OPTION "-qlanglvl=extc89")
  set(CMAKE_C90_STANDARD__HAS_FULL_SUPPORT ON)
  set(CMAKE_C99_STANDARD_COMPILE_OPTION "-qlanglvl=stdc99")
  set(CMAKE_C99_EXTENSION_COMPILE_OPTION "-qlanglvl=extc99")
  set(CMAKE_C99_STANDARD__HAS_FULL_SUPPORT ON)
  if (CMAKE_C_COMPILER_VERSION VERSION_GREATER_EQUAL 12.1)
    set(CMAKE_C11_STANDARD_COMPILE_OPTION "-qlanglvl=extc1x")
    set(CMAKE_C11_EXTENSION_COMPILE_OPTION "-qlanglvl=extc1x")
    set(CMAKE_C11_STANDARD__HAS_FULL_SUPPORT ON)
  endif ()
endif()

__compiler_check_default_language_standard(C 10.1 90 11.1 99)
