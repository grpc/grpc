# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.


# This module is shared by multiple languages; use include blocker.
if(__WINDOWS_MSVC)
  return()
endif()
set(__WINDOWS_MSVC 1)

set(CMAKE_LIBRARY_PATH_FLAG "-LIBPATH:")
set(CMAKE_LINK_LIBRARY_FLAG "")
set(MSVC 1)

# hack: if a new cmake (which uses CMAKE_LINKER) runs on an old build tree
# (where link was hardcoded) and where CMAKE_LINKER isn't in the cache
# and still cmake didn't fail in CMakeFindBinUtils.cmake (because it isn't rerun)
# hardcode CMAKE_LINKER here to link, so it behaves as it did before, Alex
if(NOT DEFINED CMAKE_LINKER)
  set(CMAKE_LINKER link)
endif()

if(CMAKE_VERBOSE_MAKEFILE)
  set(CMAKE_CL_NOLOGO)
else()
  set(CMAKE_CL_NOLOGO "/nologo")
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "WindowsCE")
  set(CMAKE_CREATE_WIN32_EXE "/entry:WinMainCRTStartup")
  set(CMAKE_CREATE_CONSOLE_EXE "/entry:mainACRTStartup")
  set(_PLATFORM_LINK_FLAGS " /subsystem:windowsce")
else()
  set(CMAKE_CREATE_WIN32_EXE "/subsystem:windows")
  set(CMAKE_CREATE_CONSOLE_EXE "/subsystem:console")
  set(_PLATFORM_LINK_FLAGS "")
endif()

set(CMAKE_SUPPORT_WINDOWS_EXPORT_ALL_SYMBOLS 1)
if(NOT CMAKE_NO_BUILD_TYPE AND CMAKE_GENERATOR MATCHES "Visual Studio")
  set (CMAKE_NO_BUILD_TYPE 1)
endif()

if("${CMAKE_GENERATOR}" MATCHES "Visual Studio")
  set(MSVC_IDE 1)
else()
  set(MSVC_IDE 0)
endif()

if(NOT MSVC_VERSION)
  if("x${CMAKE_C_COMPILER_ID}" STREQUAL "xMSVC")
    set(_compiler_version ${CMAKE_C_COMPILER_VERSION})
  elseif("x${CMAKE_CXX_COMPILER_ID}" STREQUAL "xMSVC")
    set(_compiler_version ${CMAKE_CXX_COMPILER_VERSION})
  elseif(CMAKE_C_SIMULATE_VERSION)
    set(_compiler_version ${CMAKE_C_SIMULATE_VERSION})
  elseif(CMAKE_CXX_SIMULATE_VERSION)
    set(_compiler_version ${CMAKE_CXX_SIMULATE_VERSION})
  elseif(CMAKE_Fortran_SIMULATE_VERSION)
    set(_compiler_version ${CMAKE_Fortran_SIMULATE_VERSION})
  elseif(CMAKE_CUDA_SIMULATE_VERSION)
    set(_compiler_version ${CMAKE_CUDA_SIMULATE_VERSION})
  elseif(CMAKE_C_COMPILER_VERSION)
    set(_compiler_version ${CMAKE_C_COMPILER_VERSION})
  else()
    set(_compiler_version ${CMAKE_CXX_COMPILER_VERSION})
  endif()
  if("${_compiler_version}" MATCHES "^([0-9]+)\\.([0-9]+)")
    math(EXPR MSVC_VERSION "${CMAKE_MATCH_1}*100 + ${CMAKE_MATCH_2}")
  else()
    message(FATAL_ERROR "MSVC compiler version not detected properly: ${_compiler_version}")
  endif()

  if(MSVC_VERSION GREATER_EQUAL 1920)
    # VS 2019 or greater
    set(MSVC_TOOLSET_VERSION 142)
  elseif(MSVC_VERSION GREATER_EQUAL 1910)
    # VS 2017 or greater
    set(MSVC_TOOLSET_VERSION 141)
  elseif(MSVC_VERSION EQUAL 1900)
    # VS 2015
    set(MSVC_TOOLSET_VERSION 140)
  elseif(MSVC_VERSION EQUAL 1800)
    # VS 2013
    set(MSVC_TOOLSET_VERSION 120)
  elseif(MSVC_VERSION EQUAL 1700)
    # VS 2012
    set(MSVC_TOOLSET_VERSION 110)
  elseif(MSVC_VERSION EQUAL 1600)
    # VS 2010
    set(MSVC_TOOLSET_VERSION 100)
  elseif(MSVC_VERSION EQUAL 1500)
    # VS 2008
    set(MSVC_TOOLSET_VERSION 90)
  elseif(MSVC_VERSION EQUAL 1400)
    # VS 2005
    set(MSVC_TOOLSET_VERSION 80)
  else()
    # We don't support MSVC_TOOLSET_VERSION for earlier compiler.
  endif()

  set(MSVC10)
  set(MSVC11)
  set(MSVC12)
  set(MSVC14)
  set(MSVC60)
  set(MSVC70)
  set(MSVC71)
  set(MSVC80)
  set(MSVC90)
  set(CMAKE_COMPILER_2005)
  set(CMAKE_COMPILER_SUPPORTS_PDBTYPE)
  if(NOT "${_compiler_version}" VERSION_LESS 20)
    # We no longer provide per-version variables.  Use MSVC_VERSION instead.
  elseif(NOT "${_compiler_version}" VERSION_LESS 19)
    set(MSVC14 1)
  elseif(NOT "${_compiler_version}" VERSION_LESS 18)
    set(MSVC12 1)
  elseif(NOT "${_compiler_version}" VERSION_LESS 17)
    set(MSVC11 1)
  elseif(NOT  "${_compiler_version}" VERSION_LESS 16)
    set(MSVC10 1)
  elseif(NOT  "${_compiler_version}" VERSION_LESS 15)
    set(MSVC90 1)
  elseif(NOT  "${_compiler_version}" VERSION_LESS 14)
    set(MSVC80 1)
    set(CMAKE_COMPILER_2005 1)
  elseif(NOT  "${_compiler_version}" VERSION_LESS 13.10)
    set(MSVC71 1)
  elseif(NOT  "${_compiler_version}" VERSION_LESS 13)
    set(MSVC70 1)
  else()
    set(MSVC60 1)
    set(CMAKE_COMPILER_SUPPORTS_PDBTYPE 1)
  endif()
endif()

if(MSVC_C_ARCHITECTURE_ID MATCHES 64 OR MSVC_CXX_ARCHITECTURE_ID MATCHES 64)
  set(CMAKE_CL_64 1)
else()
  set(CMAKE_CL_64 0)
endif()
if(CMAKE_FORCE_WIN64 OR CMAKE_FORCE_IA64)
  set(CMAKE_CL_64 1)
endif()

if("${MSVC_VERSION}" GREATER 1599)
  set(MSVC_INCREMENTAL_DEFAULT ON)
endif()

# default to Debug builds
set(CMAKE_BUILD_TYPE_INIT Debug)

# Compute an architecture family from the architecture id.
foreach(lang C CXX)
  set(_MSVC_${lang}_ARCHITECTURE_FAMILY "${MSVC_${lang}_ARCHITECTURE_ID}")
  if(_MSVC_${lang}_ARCHITECTURE_FAMILY MATCHES "^ARM64")
    set(_MSVC_${lang}_ARCHITECTURE_FAMILY "ARM64")
  elseif(_MSVC_${lang}_ARCHITECTURE_FAMILY MATCHES "^ARM")
    set(_MSVC_${lang}_ARCHITECTURE_FAMILY "ARM")
  elseif(_MSVC_${lang}_ARCHITECTURE_FAMILY MATCHES "^SH")
    set(_MSVC_${lang}_ARCHITECTURE_FAMILY "SHx")
  endif()
endforeach()

if(WINCE)
  foreach(lang C CXX)
    string(TOUPPER "${_MSVC_${lang}_ARCHITECTURE_FAMILY}" _MSVC_${lang}_ARCHITECTURE_FAMILY_UPPER)
  endforeach()

  if("${CMAKE_SYSTEM_VERSION}" MATCHES "^([0-9]+)\\.([0-9]+)")
    math(EXPR _CE_VERSION "${CMAKE_MATCH_1}*100 + ${CMAKE_MATCH_2}")
  elseif("${CMAKE_SYSTEM_VERSION}" STREQUAL "")
    set(_CE_VERSION "500")
  else()
    message(FATAL_ERROR "Invalid Windows CE version: ${CMAKE_SYSTEM_VERSION}")
  endif()

  set(_PLATFORM_DEFINES "/D_WIN32_WCE=0x${_CE_VERSION} /DUNDER_CE /DWINCE")
  set(_PLATFORM_DEFINES_C " /D${_MSVC_C_ARCHITECTURE_FAMILY} /D_${_MSVC_C_ARCHITECTURE_FAMILY_UPPER}_")
  set(_PLATFORM_DEFINES_CXX " /D${_MSVC_CXX_ARCHITECTURE_FAMILY} /D_${_MSVC_CXX_ARCHITECTURE_FAMILY_UPPER}_")

  set(_RTC1 "")
  set(_FLAGS_C "")
  set(_FLAGS_CXX " /GR /EHsc")

  foreach(lang C CXX)
    if(_MSVC_${lang}_ARCHITECTURE_FAMILY STREQUAL "ARM")
      string(APPEND _PLATFORM_DEFINES_${lang} " /D${MSVC_${lang}_ARCHITECTURE_ID}")
      if(MSVC_${lang}_ARCHITECTURE_ID MATCHES "^ARMV([45])I$")
        string(APPEND _FLAGS_${lang} " /QRarch${CMAKE_MATCH_1}T")
      endif()
    endif()
  endforeach()

  set(CMAKE_C_STANDARD_LIBRARIES_INIT "coredll.lib ole32.lib oleaut32.lib uuid.lib commctrl.lib")
  foreach(t EXE SHARED MODULE)
    string(APPEND CMAKE_${t}_LINKER_FLAGS_INIT " /NODEFAULTLIB:libc.lib /NODEFAULTLIB:oldnames.lib")
  endforeach()

  if (MSVC_VERSION LESS 1600)
    string(APPEND CMAKE_C_STANDARD_LIBRARIES_INIT " corelibc.lib")
  endif ()
elseif(WINDOWS_PHONE OR WINDOWS_STORE)
  set(_PLATFORM_DEFINES "/DWIN32")
  set(_FLAGS_C " /DUNICODE /D_UNICODE")
  set(_FLAGS_CXX " /DUNICODE /D_UNICODE /GR /EHsc")
  if(WINDOWS_STORE AND MSVC_VERSION GREATER 1899)
    set(CMAKE_C_STANDARD_LIBRARIES_INIT "WindowsApp.lib")
  elseif(WINDOWS_PHONE)
    set(CMAKE_C_STANDARD_LIBRARIES_INIT "WindowsPhoneCore.lib RuntimeObject.lib PhoneAppModelHost.lib")
  elseif(_MSVC_C_ARCHITECTURE_FAMILY STREQUAL "ARM" OR _MSVC_CXX_ARCHITECTURE_FAMILY STREQUAL "ARM" OR _MSVC_C_ARCHITECTURE_FAMILY STREQUAL "ARM64" OR _MSVC_CXX_ARCHITECTURE_FAMILY STREQUAL "ARM64")
    set(CMAKE_C_STANDARD_LIBRARIES_INIT "kernel32.lib user32.lib")
  else()
    set(CMAKE_C_STANDARD_LIBRARIES_INIT "kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib")
  endif()
else()
  set(_PLATFORM_DEFINES "/DWIN32")

  if(_MSVC_C_ARCHITECTURE_FAMILY STREQUAL "ARM" OR _MSVC_CXX_ARCHITECTURE_FAMILY STREQUAL "ARM")
    set(CMAKE_C_STANDARD_LIBRARIES_INIT "kernel32.lib user32.lib")
  elseif(MSVC_VERSION GREATER 1310)
    if(CMAKE_VS_PLATFORM_TOOLSET MATCHES "v[0-9]+_clang_.*")
      # Clang/C2 in MSVC14 Update 1 seems to not support -fsantinize (yet?)
      # set(_RTC1 "-fsantinize=memory,safe-stack")
      set(_FLAGS_CXX " -frtti -fexceptions")
    else()
      set(_RTC1 "/RTC1")
      set(_FLAGS_CXX " /GR /EHsc")
    endif()
    set(CMAKE_C_STANDARD_LIBRARIES_INIT "kernel32.lib user32.lib gdi32.lib winspool.lib shell32.lib ole32.lib oleaut32.lib uuid.lib comdlg32.lib advapi32.lib")
  else()
    set(_RTC1 "/GZ")
    set(_FLAGS_CXX " /GR /GX")
    set(CMAKE_C_STANDARD_LIBRARIES_INIT "kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib")
  endif()

  if(MSVC_VERSION LESS 1310)
    set(_FLAGS_C   " /Zm1000${_FLAGS_C}")
    set(_FLAGS_CXX " /Zm1000${_FLAGS_CXX}")
  endif()
endif()

set(CMAKE_CXX_STANDARD_LIBRARIES_INIT "${CMAKE_C_STANDARD_LIBRARIES_INIT}")

# executable linker flags
set (CMAKE_LINK_DEF_FILE_FLAG "/DEF:")
# set the machine type
if(MSVC_C_ARCHITECTURE_ID)
  if(MSVC_C_ARCHITECTURE_ID MATCHES "^ARMV.I")
    set(_MACHINE_ARCH_FLAG "/machine:THUMB")
  elseif(_MSVC_C_ARCHITECTURE_FAMILY STREQUAL "ARM64")
    set(_MACHINE_ARCH_FLAG "/machine:ARM64")
  elseif(_MSVC_C_ARCHITECTURE_FAMILY STREQUAL "ARM")
    set(_MACHINE_ARCH_FLAG "/machine:ARM")
  else()
    set(_MACHINE_ARCH_FLAG "/machine:${MSVC_C_ARCHITECTURE_ID}")
  endif()
elseif(MSVC_CXX_ARCHITECTURE_ID)
  if(MSVC_CXX_ARCHITECTURE_ID MATCHES "^ARMV.I")
    set(_MACHINE_ARCH_FLAG "/machine:THUMB")
  elseif(_MSVC_CXX_ARCHITECTURE_FAMILY STREQUAL "ARM64")
    set(_MACHINE_ARCH_FLAG "/machine:ARM64")
  elseif(_MSVC_CXX_ARCHITECTURE_FAMILY STREQUAL "ARM")
    set(_MACHINE_ARCH_FLAG "/machine:ARM")
  else()
    set(_MACHINE_ARCH_FLAG "/machine:${MSVC_CXX_ARCHITECTURE_ID}")
  endif()
elseif(MSVC_Fortran_ARCHITECTURE_ID)
  set(_MACHINE_ARCH_FLAG "/machine:${MSVC_Fortran_ARCHITECTURE_ID}")
endif()

# add /debug and /INCREMENTAL:YES to DEBUG and RELWITHDEBINFO also add pdbtype
# on versions that support it
set( MSVC_INCREMENTAL_YES_FLAG "")
if(NOT WINDOWS_PHONE AND NOT WINDOWS_STORE)
  if(NOT MSVC_INCREMENTAL_DEFAULT)
    set( MSVC_INCREMENTAL_YES_FLAG "/INCREMENTAL:YES")
  else()
    set(  MSVC_INCREMENTAL_YES_FLAG "/INCREMENTAL" )
  endif()
endif()

foreach(t EXE SHARED MODULE)
  string(APPEND CMAKE_${t}_LINKER_FLAGS_INIT " ${_MACHINE_ARCH_FLAG}")
  if (CMAKE_COMPILER_SUPPORTS_PDBTYPE)
    string(APPEND CMAKE_${t}_LINKER_FLAGS_DEBUG_INIT " /debug /pdbtype:sept ${MSVC_INCREMENTAL_YES_FLAG}")
    string(APPEND CMAKE_${t}_LINKER_FLAGS_RELWITHDEBINFO_INIT " /debug /pdbtype:sept ${MSVC_INCREMENTAL_YES_FLAG}")
  else ()
    string(APPEND CMAKE_${t}_LINKER_FLAGS_DEBUG_INIT " /debug ${MSVC_INCREMENTAL_YES_FLAG}")
    string(APPEND CMAKE_${t}_LINKER_FLAGS_RELWITHDEBINFO_INIT " /debug ${MSVC_INCREMENTAL_YES_FLAG}")
  endif ()
  # for release and minsize release default to no incremental linking
  string(APPEND CMAKE_${t}_LINKER_FLAGS_MINSIZEREL_INIT " /INCREMENTAL:NO")
  string(APPEND CMAKE_${t}_LINKER_FLAGS_RELEASE_INIT " /INCREMENTAL:NO")
endforeach()

string(APPEND CMAKE_STATIC_LINKER_FLAGS_INIT " ${_MACHINE_ARCH_FLAG}")
unset(_MACHINE_ARCH_FLAG)

cmake_policy(GET CMP0091 __WINDOWS_MSVC_CMP0091)
if(__WINDOWS_MSVC_CMP0091 STREQUAL "NEW")
  set(CMAKE_MSVC_RUNTIME_LIBRARY_DEFAULT "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
else()
  set(CMAKE_MSVC_RUNTIME_LIBRARY_DEFAULT "")
endif()
unset(__WINDOWS_MSVC_CMP0091)

macro(__windows_compiler_msvc lang)
  if(NOT MSVC_VERSION LESS 1400)
    # for 2005 make sure the manifest is put in the dll with mt
    set(_CMAKE_VS_LINK_DLL "<CMAKE_COMMAND> -E vs_link_dll --intdir=<OBJECT_DIR> --rc=<CMAKE_RC_COMPILER> --mt=<CMAKE_MT> --manifests <MANIFESTS> -- ")
    set(_CMAKE_VS_LINK_EXE "<CMAKE_COMMAND> -E vs_link_exe --intdir=<OBJECT_DIR> --rc=<CMAKE_RC_COMPILER> --mt=<CMAKE_MT> --manifests <MANIFESTS> -- ")
  endif()
  set(CMAKE_${lang}_CREATE_SHARED_LIBRARY
    "${_CMAKE_VS_LINK_DLL}<CMAKE_LINKER> ${CMAKE_CL_NOLOGO} <OBJECTS> ${CMAKE_START_TEMP_FILE} /out:<TARGET> /implib:<TARGET_IMPLIB> /pdb:<TARGET_PDB> /dll /version:<TARGET_VERSION_MAJOR>.<TARGET_VERSION_MINOR>${_PLATFORM_LINK_FLAGS} <LINK_FLAGS> <LINK_LIBRARIES> ${CMAKE_END_TEMP_FILE}")

  set(CMAKE_${lang}_CREATE_SHARED_MODULE ${CMAKE_${lang}_CREATE_SHARED_LIBRARY})
  set(CMAKE_${lang}_CREATE_STATIC_LIBRARY  "<CMAKE_AR> ${CMAKE_CL_NOLOGO} <LINK_FLAGS> /out:<TARGET> <OBJECTS> ")

  set(CMAKE_${lang}_COMPILE_OBJECT
    "<CMAKE_${lang}_COMPILER> ${CMAKE_START_TEMP_FILE} ${CMAKE_CL_NOLOGO}${_COMPILE_${lang}} <DEFINES> <INCLUDES> <FLAGS> /Fo<OBJECT> /Fd<TARGET_COMPILE_PDB>${_FS_${lang}} -c <SOURCE>${CMAKE_END_TEMP_FILE}")
  set(CMAKE_${lang}_CREATE_PREPROCESSED_SOURCE
    "<CMAKE_${lang}_COMPILER> > <PREPROCESSED_SOURCE> ${CMAKE_START_TEMP_FILE} ${CMAKE_CL_NOLOGO}${_COMPILE_${lang}} <DEFINES> <INCLUDES> <FLAGS> -E <SOURCE>${CMAKE_END_TEMP_FILE}")
  set(CMAKE_${lang}_CREATE_ASSEMBLY_SOURCE
    "<CMAKE_${lang}_COMPILER> ${CMAKE_START_TEMP_FILE} ${CMAKE_CL_NOLOGO}${_COMPILE_${lang}} <DEFINES> <INCLUDES> <FLAGS> /FoNUL /FAs /Fa<ASSEMBLY_SOURCE> /c <SOURCE>${CMAKE_END_TEMP_FILE}")

  set(CMAKE_${lang}_USE_RESPONSE_FILE_FOR_OBJECTS 1)
  set(CMAKE_${lang}_LINK_EXECUTABLE
    "${_CMAKE_VS_LINK_EXE}<CMAKE_LINKER> ${CMAKE_CL_NOLOGO} <OBJECTS> ${CMAKE_START_TEMP_FILE} /out:<TARGET> /implib:<TARGET_IMPLIB> /pdb:<TARGET_PDB> /version:<TARGET_VERSION_MAJOR>.<TARGET_VERSION_MINOR>${_PLATFORM_LINK_FLAGS} <CMAKE_${lang}_LINK_FLAGS> <LINK_FLAGS> <LINK_LIBRARIES>${CMAKE_END_TEMP_FILE}")

  set(CMAKE_PCH_EXTENSION .pch)
  set(CMAKE_LINK_PCH ON)
  if (CMAKE_${lang}_COMPILER_ID STREQUAL "Clang")
    set(CMAKE_PCH_PROLOGUE "#pragma clang system_header")

    # macOS paths usually start with /Users/*. Unfortunately, clang-cl interprets
    # paths starting with /U as macro undefines, so we need to put a -- before the
    # input file path to force it to be treated as a path.
    string(REPLACE "-c <SOURCE>" "-c -- <SOURCE>" CMAKE_${lang}_COMPILE_OBJECT "${CMAKE_${lang}_COMPILE_OBJECT}")
    string(REPLACE "-c <SOURCE>" "-c -- <SOURCE>" CMAKE_${lang}_CREATE_PREPROCESSED_SOURCE "${CMAKE_${lang}_CREATE_PREPROCESSED_SOURCE}")
    string(REPLACE "-c <SOURCE>" "-c -- <SOURCE>" CMAKE_${lang}_CREATE_ASSEMBLY_SOURCE "${CMAKE_${lang}_CREATE_ASSEMBLY_SOURCE}")

  elseif(MSVC_VERSION GREATER_EQUAL 1913)
    # At least MSVC toolet 14.13 from VS 2017 15.6
    set(CMAKE_PCH_PROLOGUE "#pragma system_header")
  endif()
  if (NOT ${CMAKE_${lang}_COMPILER_ID} STREQUAL "Clang")
    set(CMAKE_PCH_COPY_COMPILE_PDB ON)
  endif()
  set(CMAKE_${lang}_COMPILE_OPTIONS_USE_PCH /Yu<PCH_HEADER> /Fp<PCH_FILE> /FI<PCH_HEADER>)
  set(CMAKE_${lang}_COMPILE_OPTIONS_CREATE_PCH /Yc<PCH_HEADER> /Fp<PCH_FILE> /FI<PCH_HEADER>)

  if("x${CMAKE_${lang}_COMPILER_ID}" STREQUAL "xMSVC")
    set(_CMAKE_${lang}_IPO_SUPPORTED_BY_CMAKE YES)
    set(_CMAKE_${lang}_IPO_MAY_BE_SUPPORTED_BY_COMPILER YES)

    set(CMAKE_${lang}_COMPILE_OPTIONS_IPO "/GL")
    set(CMAKE_${lang}_LINK_OPTIONS_IPO "/INCREMENTAL:NO" "/LTCG")
    string(REPLACE "<LINK_FLAGS> " "/LTCG <LINK_FLAGS> "
      CMAKE_${lang}_CREATE_STATIC_LIBRARY_IPO "${CMAKE_${lang}_CREATE_STATIC_LIBRARY}")
  elseif("x${CMAKE_${lang}_COMPILER_ID}" STREQUAL "xClang" OR
         "x${CMAKE_${lang}_COMPILER_ID}" STREQUAL "xFlang")
    set(_CMAKE_${lang}_IPO_SUPPORTED_BY_CMAKE YES)
    set(_CMAKE_${lang}_IPO_MAY_BE_SUPPORTED_BY_COMPILER YES)

    # '-flto=thin' available since Clang 3.9 and Xcode 8
    # * http://clang.llvm.org/docs/ThinLTO.html#clang-llvm
    # * https://trac.macports.org/wiki/XcodeVersionInfo
    set(_CMAKE_LTO_THIN TRUE)
    if(CMAKE_${lang}_COMPILER_VERSION VERSION_LESS 3.9)
      set(_CMAKE_LTO_THIN FALSE)
    endif()

    if(_CMAKE_LTO_THIN)
      set(CMAKE_${lang}_COMPILE_OPTIONS_IPO "-flto=thin")
    else()
      set(CMAKE_${lang}_COMPILE_OPTIONS_IPO "-flto")
    endif()
  endif()

  if("x${lang}" STREQUAL "xC" OR
      "x${lang}" STREQUAL "xCXX")
    if(CMAKE_MSVC_RUNTIME_LIBRARY_DEFAULT)
      set(_MDd "")
      set(_MD "")
    else()
      set(_MDd " /MDd")
      set(_MD " /MD")
    endif()

    cmake_policy(GET CMP0092 _cmp0092)
    if(_cmp0092 STREQUAL "NEW")
      set(_W3 "")
      set(_Wall "")
    else()
      set(_W3 " /W3")
      set(_Wall " -Wall")
    endif()
    unset(_cmp0092)

    if(CMAKE_VS_PLATFORM_TOOLSET MATCHES "v[0-9]+_clang_.*")
      # note: MSVC 14 2015 Update 1 sets -fno-ms-compatibility by default, but this does not allow one to compile many projects
      # that include MS's own headers. CMake itself is affected project too.
      string(APPEND CMAKE_${lang}_FLAGS_INIT " ${_PLATFORM_DEFINES}${_PLATFORM_DEFINES_${lang}} -fms-extensions -fms-compatibility -D_WINDOWS${_Wall}${_FLAGS_${lang}}")
      string(APPEND CMAKE_${lang}_FLAGS_DEBUG_INIT "${_MDd} -gline-tables-only -fno-inline -O0 ${_RTC1}")
      string(APPEND CMAKE_${lang}_FLAGS_RELEASE_INIT "${_MD} -O2 -DNDEBUG")
      string(APPEND CMAKE_${lang}_FLAGS_RELWITHDEBINFO_INIT "${_MD} -gline-tables-only -O2 -fno-inline -DNDEBUG")
      string(APPEND CMAKE_${lang}_FLAGS_MINSIZEREL_INIT "${_MD} -DNDEBUG") # TODO: Add '-Os' once VS generator maps it properly for Clang
    else()
      string(APPEND CMAKE_${lang}_FLAGS_INIT " ${_PLATFORM_DEFINES}${_PLATFORM_DEFINES_${lang}} /D_WINDOWS${_W3}${_FLAGS_${lang}}")
      string(APPEND CMAKE_${lang}_FLAGS_DEBUG_INIT "${_MDd} /Zi /Ob0 /Od ${_RTC1}")
      string(APPEND CMAKE_${lang}_FLAGS_RELEASE_INIT "${_MD} /O2 /Ob2 /DNDEBUG")
      string(APPEND CMAKE_${lang}_FLAGS_RELWITHDEBINFO_INIT "${_MD} /Zi /O2 /Ob1 /DNDEBUG")
      string(APPEND CMAKE_${lang}_FLAGS_MINSIZEREL_INIT "${_MD} /O1 /Ob1 /DNDEBUG")
    endif()
    unset(_Wall)
    unset(_W3)
    unset(_MDd)
    unset(_MD)

    set(CMAKE_${lang}_COMPILE_OPTIONS_MSVC_RUNTIME_LIBRARY_MultiThreaded         -MT)
    set(CMAKE_${lang}_COMPILE_OPTIONS_MSVC_RUNTIME_LIBRARY_MultiThreadedDLL      -MD)
    set(CMAKE_${lang}_COMPILE_OPTIONS_MSVC_RUNTIME_LIBRARY_MultiThreadedDebug    -MTd)
    set(CMAKE_${lang}_COMPILE_OPTIONS_MSVC_RUNTIME_LIBRARY_MultiThreadedDebugDLL -MDd)
  endif()
  set(CMAKE_${lang}_LINKER_SUPPORTS_PDB ON)
  set(CMAKE_NINJA_DEPTYPE_${lang} msvc)
  __windows_compiler_msvc_enable_rc("${_PLATFORM_DEFINES} ${_PLATFORM_DEFINES_${lang}}")
endmacro()

macro(__windows_compiler_msvc_enable_rc flags)
  if(NOT CMAKE_RC_COMPILER_INIT)
    set(CMAKE_RC_COMPILER_INIT rc)
  endif()
  if(NOT CMAKE_RC_FLAGS_INIT)
    # llvm-rc fails when flags are specified with /D and no space after
    string(REPLACE " /D" " -D" fixed_flags " ${flags}")
    string(APPEND CMAKE_RC_FLAGS_INIT " ${fixed_flags}")
  endif()
  if(NOT CMAKE_RC_FLAGS_DEBUG_INIT)
    string(APPEND CMAKE_RC_FLAGS_DEBUG_INIT " -D_DEBUG")
  endif()

  enable_language(RC)
  if(NOT DEFINED CMAKE_NINJA_CMCLDEPS_RC)
    set(CMAKE_NINJA_CMCLDEPS_RC 1)
  endif()
endmacro()
