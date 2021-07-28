# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

include(${CMAKE_ROOT}/Modules/CMakeDetermineCompiler.cmake)
include(${CMAKE_ROOT}/Modules/CMakeParseImplicitLinkInfo.cmake)

if( NOT ( ("${CMAKE_GENERATOR}" MATCHES "Make") OR
          ("${CMAKE_GENERATOR}" MATCHES "Ninja") OR
          ("${CMAKE_GENERATOR}" MATCHES "Visual Studio (1|[9][0-9])") ) )
  message(FATAL_ERROR "CUDA language not currently supported by \"${CMAKE_GENERATOR}\" generator")
endif()

if(${CMAKE_GENERATOR} MATCHES "Visual Studio")
else()
  if(NOT CMAKE_CUDA_COMPILER)
    set(CMAKE_CUDA_COMPILER_INIT NOTFOUND)

      # prefer the environment variable CUDACXX
      if(NOT $ENV{CUDACXX} STREQUAL "")
        get_filename_component(CMAKE_CUDA_COMPILER_INIT $ENV{CUDACXX} PROGRAM PROGRAM_ARGS CMAKE_CUDA_FLAGS_ENV_INIT)
        if(CMAKE_CUDA_FLAGS_ENV_INIT)
          set(CMAKE_CUDA_COMPILER_ARG1 "${CMAKE_CUDA_FLAGS_ENV_INIT}" CACHE STRING "Arguments to CXX compiler")
        endif()
        if(NOT EXISTS ${CMAKE_CUDA_COMPILER_INIT})
          message(FATAL_ERROR "Could not find compiler set in environment variable CUDACXX:\n$ENV{CUDACXX}.\n${CMAKE_CUDA_COMPILER_INIT}")
        endif()
      endif()

    # finally list compilers to try
    if(NOT CMAKE_CUDA_COMPILER_INIT)
      set(CMAKE_CUDA_COMPILER_LIST nvcc)
    endif()

    _cmake_find_compiler(CUDA)
  else()
    _cmake_find_compiler_path(CUDA)
  endif()

  mark_as_advanced(CMAKE_CUDA_COMPILER)
endif()

#Allow the user to specify a host compiler
if(NOT $ENV{CUDAHOSTCXX} STREQUAL "")
  get_filename_component(CMAKE_CUDA_HOST_COMPILER $ENV{CUDAHOSTCXX} PROGRAM)
  if(NOT EXISTS ${CMAKE_CUDA_HOST_COMPILER})
    message(FATAL_ERROR "Could not find compiler set in environment variable CUDAHOSTCXX:\n$ENV{CUDAHOSTCXX}.\n${CMAKE_CUDA_HOST_COMPILER}")
  endif()
endif()

# Build a small source file to identify the compiler.
if(NOT CMAKE_CUDA_COMPILER_ID_RUN)
  set(CMAKE_CUDA_COMPILER_ID_RUN 1)

  include(${CMAKE_ROOT}/Modules/CMakeDetermineCompilerId.cmake)

  if(${CMAKE_GENERATOR} MATCHES "Visual Studio")
    # We will not know CMAKE_CUDA_COMPILER until the main compiler id step
    # below extracts it, but we do know that the compiler id will be NVIDIA.
    set(CMAKE_CUDA_COMPILER_ID "NVIDIA")
  else()
    # We determine the vendor to help with find the toolkit and use the right flags for detection right away.
    # The main compiler identification is still needed below to extract other information.
    list(APPEND CMAKE_CUDA_COMPILER_ID_VENDORS NVIDIA Clang)
    set(CMAKE_CUDA_COMPILER_ID_VENDOR_REGEX_NVIDIA "nvcc: NVIDIA \\(R\\) Cuda compiler driver")
    set(CMAKE_CUDA_COMPILER_ID_VENDOR_REGEX_Clang "(clang version)")
    CMAKE_DETERMINE_COMPILER_ID_VENDOR(CUDA "--version")

    # Find the CUDA toolkit. We store the CMAKE_CUDA_COMPILER_TOOLKIT_ROOT and CMAKE_CUDA_COMPILER_LIBRARY_ROOT
    # in CMakeCUDACompiler.cmake, so FindCUDAToolkit can avoid searching on future runs and the toolkit stays the same.
    # This is very similar to FindCUDAToolkit, but somewhat simplified since we can issue fatal errors
    # if we fail to find things we need and we don't need to account for searching the libraries.

    # For NVCC we can easily deduce the SDK binary directory from the compiler path.
    if(CMAKE_CUDA_COMPILER_ID STREQUAL "NVIDIA")
      set(_CUDA_NVCC_EXECUTABLE "${CMAKE_CUDA_COMPILER}")
    else()
      # Search using CUDAToolkit_ROOT and then CUDA_PATH for equivalence with FindCUDAToolkit.
      # In FindCUDAToolkit CUDAToolkit_ROOT is searched automatically due to being in a find_package().
      # First we search candidate non-default paths to give them priority.
      find_program(_CUDA_NVCC_EXECUTABLE
        NAMES nvcc nvcc.exe
        PATHS ${CUDAToolkit_ROOT}
        ENV CUDAToolkit_ROOT
        ENV CUDA_PATH
        PATH_SUFFIXES bin
        NO_DEFAULT_PATH
      )

      # If we didn't find NVCC, then try the default paths.
      find_program(_CUDA_NVCC_EXECUTABLE
        NAMES nvcc nvcc.exe
        PATH_SUFFIXES bin
      )

      # If the user specified CUDAToolkit_ROOT but nvcc could not be found, this is an error.
      if(NOT _CUDA_NVCC_EXECUTABLE AND (DEFINED CUDAToolkit_ROOT OR DEFINED ENV{CUDAToolkit_ROOT}))
        set(fail_base "Could not find nvcc executable in path specified by")

        if(DEFINED CUDAToolkit_ROOT)
          message(FATAL_ERROR "${fail_base} CUDAToolkit_ROOT=${CUDAToolkit_ROOT}")
        elseif(DEFINED ENV{CUDAToolkit_ROOT})
          message(FATAL_ERROR "${fail_base} environment variable CUDAToolkit_ROOT=$ENV{CUDAToolkit_ROOT}")
        endif()
      endif()

      # CUDAToolkit_ROOT cmake/env variable not specified, try platform defaults.
      #
      # - Linux: /usr/local/cuda-X.Y
      # - macOS: /Developer/NVIDIA/CUDA-X.Y
      # - Windows: C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\vX.Y
      #
      # We will also search the default symlink location /usr/local/cuda first since
      # if CUDAToolkit_ROOT is not specified, it is assumed that the symlinked
      # directory is the desired location.
      if(NOT _CUDA_NVCC_EXECUTABLE)
        if(UNIX)
          if(NOT APPLE)
            set(platform_base "/usr/local/cuda-")
          else()
            set(platform_base "/Developer/NVIDIA/CUDA-")
          endif()
        else()
          set(platform_base "C:\\Program Files\\NVIDIA GPU Computing Toolkit\\CUDA\\v")
        endif()

        # Build out a descending list of possible cuda installations, e.g.
        file(GLOB possible_paths "${platform_base}*")
        # Iterate the glob results and create a descending list.
        set(versions)
        foreach(p ${possible_paths})
          # Extract version number from end of string
          string(REGEX MATCH "[0-9][0-9]?\\.[0-9]$" p_version ${p})
          if(IS_DIRECTORY ${p} AND p_version)
            list(APPEND versions ${p_version})
          endif()
        endforeach()

        # Sort numerically in descending order, so we try the newest versions first.
        list(SORT versions COMPARE NATURAL ORDER DESCENDING)

        # With a descending list of versions, populate possible paths to search.
        set(search_paths)
        foreach(v ${versions})
          list(APPEND search_paths "${platform_base}${v}")
        endforeach()

        # Force the global default /usr/local/cuda to the front on Unix.
        if(UNIX)
          list(INSERT search_paths 0 "/usr/local/cuda")
        endif()

        # Now search for nvcc again using the platform default search paths.
        find_program(_CUDA_NVCC_EXECUTABLE
          NAMES nvcc nvcc.exe
          PATHS ${search_paths}
          PATH_SUFFIXES bin
        )

        # We are done with these variables now, cleanup.
        unset(platform_base)
        unset(possible_paths)
        unset(versions)
        unset(search_paths)

        if(NOT _CUDA_NVCC_EXECUTABLE)
          message(FATAL_ERROR "Could not find nvcc, please set CUDAToolkit_ROOT.")
        endif()
      endif()
    endif()

    get_filename_component(CMAKE_CUDA_COMPILER_TOOLKIT_ROOT "${_CUDA_NVCC_EXECUTABLE}" DIRECTORY)
    set(CMAKE_CUDA_DEVICE_LINKER "${CMAKE_CUDA_COMPILER_TOOLKIT_ROOT}/nvlink${CMAKE_EXECUTABLE_SUFFIX}")
    set(CMAKE_CUDA_FATBINARY "${CMAKE_CUDA_COMPILER_TOOLKIT_ROOT}/fatbinary${CMAKE_EXECUTABLE_SUFFIX}")
    get_filename_component(CMAKE_CUDA_COMPILER_TOOLKIT_ROOT "${CMAKE_CUDA_COMPILER_TOOLKIT_ROOT}" DIRECTORY)

    # In a non-scattered installation the following are equivalent to CMAKE_CUDA_COMPILER_TOOLKIT_ROOT.
    # We first check for a non-scattered installation to prefer it over a scattered installation.

    # CMAKE_CUDA_COMPILER_LIBRARY_ROOT contains the device library.
    if(EXISTS "${CMAKE_CUDA_COMPILER_TOOLKIT_ROOT}/nvvm/libdevice")
      set(CMAKE_CUDA_COMPILER_LIBRARY_ROOT "${CMAKE_CUDA_COMPILER_TOOLKIT_ROOT}")
    elseif(CMAKE_SYSROOT_LINK AND EXISTS "${CMAKE_SYSROOT_LINK}/usr/lib/cuda/nvvm/libdevice")
      set(CMAKE_CUDA_COMPILER_LIBRARY_ROOT "${CMAKE_SYSROOT_LINK}/usr/lib/cuda")
    elseif(EXISTS "${CMAKE_SYSROOT}/usr/lib/cuda/nvvm/libdevice")
      set(CMAKE_CUDA_COMPILER_LIBRARY_ROOT "${CMAKE_SYSROOT}/usr/lib/cuda")
    else()
      message(FATAL_ERROR "Couldn't find CUDA library root.")
    endif()

    # CMAKE_CUDA_COMPILER_TOOLKIT_LIBRARY_ROOT contains the linking stubs necessary for device linking and other low-level library files.
    if(CMAKE_SYSROOT_LINK AND EXISTS "${CMAKE_SYSROOT_LINK}/usr/lib/nvidia-cuda-toolkit/bin/crt/link.stub")
      set(CMAKE_CUDA_COMPILER_TOOLKIT_LIBRARY_ROOT "${CMAKE_SYSROOT_LINK}/usr/lib/nvidia-cuda-toolkit")
    elseif(EXISTS "${CMAKE_SYSROOT}/usr/lib/nvidia-cuda-toolkit/bin/crt/link.stub")
      set(CMAKE_CUDA_COMPILER_TOOLKIT_LIBRARY_ROOT "${CMAKE_SYSROOT}/usr/lib/nvidia-cuda-toolkit")
    else()
      set(CMAKE_CUDA_COMPILER_TOOLKIT_LIBRARY_ROOT "${CMAKE_CUDA_COMPILER_TOOLKIT_ROOT}")
    endif()
  endif()

  set(CMAKE_CUDA_COMPILER_ID_FLAGS_ALWAYS "-v")

  if(CMAKE_CUDA_COMPILER_ID STREQUAL "NVIDIA")
    set(nvcc_test_flags "--keep --keep-dir tmp")
    if(CMAKE_CUDA_HOST_COMPILER)
      string(APPEND nvcc_test_flags " -ccbin=\"${CMAKE_CUDA_HOST_COMPILER}\"")

      # If the user has specified a host compiler we should fail instead of trying without.
      # Succeeding detection without may result in confusing errors later on, see #21076.
      set(CMAKE_CUDA_COMPILER_ID_REQUIRE_SUCCESS ON)
    endif()
  elseif(CMAKE_CUDA_COMPILER_ID STREQUAL "Clang")
    if(WIN32)
      message(FATAL_ERROR "Clang with CUDA is not yet supported on Windows. See CMake issue #20776.")
    endif()

    set(clang_test_flags "--cuda-path=\"${CMAKE_CUDA_COMPILER_LIBRARY_ROOT}\"")
    if(CMAKE_CROSSCOMPILING)
      # Need to pass the host target and include directories if we're crosscompiling.
      string(APPEND clang_test_flags " --sysroot=\"${CMAKE_SYSROOT}\" --target=${CMAKE_CUDA_COMPILER_TARGET}")
    endif()
  endif()

  # Append user-specified architectures.
  if(CMAKE_CUDA_ARCHITECTURES)
    foreach(arch ${CMAKE_CUDA_ARCHITECTURES})
      # Strip specifiers as PTX vs binary doesn't matter.
      string(REGEX MATCH "[0-9]+" arch_name "${arch}")
      string(APPEND clang_test_flags " --cuda-gpu-arch=sm_${arch_name}")
      string(APPEND nvcc_test_flags " -gencode=arch=compute_${arch_name},code=sm_${arch_name}")
      list(APPEND tested_architectures "${arch_name}")
    endforeach()

    # If the user has specified architectures we'll want to fail during compiler detection if they don't work.
    set(CMAKE_CUDA_COMPILER_ID_REQUIRE_SUCCESS ON)
  endif()

  if(CMAKE_CUDA_COMPILER_ID STREQUAL "Clang")
    if(NOT CMAKE_CUDA_ARCHITECTURES)
      # Clang doesn't automatically select an architecture supported by the SDK.
      # Try in reverse order of deprecation with the most recent at front (i.e. the most likely to work for new setups).
      foreach(arch "20" "30" "52")
        list(APPEND CMAKE_CUDA_COMPILER_ID_TEST_FLAGS_FIRST "${clang_test_flags} --cuda-gpu-arch=sm_${arch}")
      endforeach()
    endif()

    # If the user specified CMAKE_CUDA_ARCHITECTURES this will include all the architecture flags.
    # Otherwise this won't include any architecture flags and we'll fallback to Clang's defaults.
    list(APPEND CMAKE_CUDA_COMPILER_ID_TEST_FLAGS_FIRST "${clang_test_flags}")
  elseif(CMAKE_CUDA_COMPILER_ID STREQUAL "NVIDIA")
    list(APPEND CMAKE_CUDA_COMPILER_ID_TEST_FLAGS_FIRST "${nvcc_test_flags}")
  endif()

  # We perform compiler identification for a second time to extract implicit linking info and host compiler for NVCC.
  # We also use it to verify that CMAKE_CUDA_ARCHITECTURES and additionally on Clang that CUDA toolkit path works.
  # The latter could be done during compiler testing in the future to avoid doing this for Clang.
  # We need to unset the compiler ID otherwise CMAKE_DETERMINE_COMPILER_ID() doesn't work.
  set(CMAKE_CUDA_COMPILER_ID)
  set(CMAKE_CUDA_PLATFORM_ID)
  file(READ ${CMAKE_ROOT}/Modules/CMakePlatformId.h.in
    CMAKE_CUDA_COMPILER_ID_PLATFORM_CONTENT)

  CMAKE_DETERMINE_COMPILER_ID(CUDA CUDAFLAGS CMakeCUDACompilerId.cu)

  if(${CMAKE_GENERATOR} MATCHES "Visual Studio")
    # Now that we have the path to nvcc, we can compute the toolkit root.
    get_filename_component(CMAKE_CUDA_COMPILER_TOOLKIT_ROOT "${CMAKE_CUDA_COMPILER}" DIRECTORY)
    get_filename_component(CMAKE_CUDA_COMPILER_TOOLKIT_ROOT "${CMAKE_CUDA_COMPILER_TOOLKIT_ROOT}" DIRECTORY)
    set(CMAKE_CUDA_COMPILER_LIBRARY_ROOT "${CMAKE_CUDA_COMPILER_TOOLKIT_ROOT}")
  endif()

  _cmake_find_compiler_sysroot(CUDA)
endif()

set(_CMAKE_PROCESSING_LANGUAGE "CUDA")
include(CMakeFindBinUtils)
include(Compiler/${CMAKE_CUDA_COMPILER_ID}-FindBinUtils OPTIONAL)
unset(_CMAKE_PROCESSING_LANGUAGE)

if(MSVC_CUDA_ARCHITECTURE_ID)
  set(SET_MSVC_CUDA_ARCHITECTURE_ID
    "set(MSVC_CUDA_ARCHITECTURE_ID ${MSVC_CUDA_ARCHITECTURE_ID})")
endif()

if(${CMAKE_GENERATOR} MATCHES "Visual Studio")
  set(CMAKE_CUDA_HOST_LINK_LAUNCHER "${CMAKE_LINKER}")
  set(CMAKE_CUDA_HOST_IMPLICIT_LINK_LIBRARIES "")
  set(CMAKE_CUDA_HOST_IMPLICIT_LINK_DIRECTORIES "")
  set(CMAKE_CUDA_HOST_IMPLICIT_LINK_FRAMEWORK_DIRECTORIES "")

  # We do not currently detect CMAKE_CUDA_HOST_IMPLICIT_LINK_LIBRARIES but we
  # do need to detect CMAKE_CUDA_RUNTIME_LIBRARY_DEFAULT from the compiler by
  # looking at which cudart library exists in the implicit link libraries passed
  # to the host linker.
  if(CMAKE_CUDA_COMPILER_PRODUCED_OUTPUT MATCHES "link\\.exe [^\n]*cudart_static\\.lib")
    set(CMAKE_CUDA_RUNTIME_LIBRARY_DEFAULT "STATIC")
  elseif(CMAKE_CUDA_COMPILER_PRODUCED_OUTPUT MATCHES "link\\.exe [^\n]*cudart\\.lib")
    set(CMAKE_CUDA_RUNTIME_LIBRARY_DEFAULT "SHARED")
  else()
    set(CMAKE_CUDA_RUNTIME_LIBRARY_DEFAULT "NONE")
  endif()
  set(_SET_CMAKE_CUDA_RUNTIME_LIBRARY_DEFAULT
    "set(CMAKE_CUDA_RUNTIME_LIBRARY_DEFAULT \"${CMAKE_CUDA_RUNTIME_LIBRARY_DEFAULT}\")")
elseif(CMAKE_CUDA_COMPILER_ID STREQUAL "Clang")
  if(NOT CMAKE_CUDA_ARCHITECTURES)
    # Find the architecture that we successfully compiled using and set it as the default.
    string(REGEX MATCH "-target-cpu sm_([0-9]+)" dont_care "${CMAKE_CUDA_COMPILER_PRODUCED_OUTPUT}")
    set(detected_architecture "${CMAKE_MATCH_1}")
  else()
    string(REGEX MATCHALL "-target-cpu sm_([0-9]+)" target_cpus "${CMAKE_CUDA_COMPILER_PRODUCED_OUTPUT}")

    foreach(cpu ${target_cpus})
      string(REGEX MATCH "-target-cpu sm_([0-9]+)" dont_care "${cpu}")
      list(APPEND architectures "${CMAKE_MATCH_1}")
    endforeach()
  endif()

  # Find target directory when crosscompiling.
  if(CMAKE_CROSSCOMPILING)
    if(CMAKE_SYSTEM_PROCESSOR STREQUAL "armv7-a")
      # Support for NVPACK
      set(_CUDA_TARGET_NAME "armv7-linux-androideabi")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "arm")
      set(_CUDA_TARGET_NAME "armv7-linux-gnueabihf")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64")
      if(ANDROID_ARCH_NAME STREQUAL "arm64")
        set(_CUDA_TARGET_NAME "aarch64-linux-androideabi")
      else()
        set(_CUDA_TARGET_NAME "aarch64-linux")
      endif()
    elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
      set(_CUDA_TARGET_NAME "x86_64-linux")
    endif()

    if(EXISTS "${CMAKE_CUDA_COMPILER_TOOLKIT_ROOT}/targets/${_CUDA_TARGET_NAME}")
      set(_CUDA_TARGET_DIR "${CMAKE_CUDA_COMPILER_TOOLKIT_ROOT}/targets/${_CUDA_TARGET_NAME}")
    endif()
  endif()

  # If not already set we can simply use the toolkit root or it's a scattered installation.
  if(NOT _CUDA_TARGET_DIR)
    set(_CUDA_TARGET_DIR "${CMAKE_CUDA_COMPILER_TOOLKIT_ROOT}")
  endif()

  # We can't use find_library() yet at this point, so try a few guesses.
  if(EXISTS "${_CUDA_TARGET_DIR}/lib64")
    set(_CUDA_LIBRARY_DIR "${_CUDA_TARGET_DIR}/lib64")
  elseif(EXISTS "${_CUDA_TARGET_DIR}/lib/x64")
    set(_CUDA_LIBRARY_DIR "${_CUDA_TARGET_DIR}/lib/x64")
  elseif(EXISTS "${_CUDA_TARGET_DIR}/lib")
    set(_CUDA_LIBRARY_DIR "${_CUDA_TARGET_DIR}/lib")
  else()
    message(FATAL_ERROR "Unable to find _CUDA_LIBRARY_DIR based on _CUDA_TARGET_DIR=${_CUDA_TARGET_DIR}")
  endif()

  # _CUDA_TARGET_DIR always points to the directory containing the include directory.
  # On a scattered installation /usr, on a non-scattered something like /usr/local/cuda or /usr/local/cuda-10.2/targets/aarch64-linux.
  if(EXISTS "${_CUDA_TARGET_DIR}/include/cuda_runtime.h")
    set(_CUDA_INCLUDE_DIR "${_CUDA_TARGET_DIR}/include")
  else()
    message(FATAL_ERROR "Unable to find cuda_runtime.h in \"${_CUDA_TARGET_DIR}/include\" for _CUDA_INCLUDE_DIR.")
  endif()

  # Clang does not add any CUDA SDK libraries or directories when invoking the host linker.
  # Add the CUDA toolkit library directory ourselves so that linking works.
  # The CUDA runtime libraries are handled elsewhere by CMAKE_CUDA_RUNTIME_LIBRARY.
  set(CMAKE_CUDA_TOOLKIT_INCLUDE_DIRECTORIES "${_CUDA_INCLUDE_DIR}")
  set(CMAKE_CUDA_HOST_IMPLICIT_LINK_DIRECTORIES "${_CUDA_LIBRARY_DIR}")
  set(CMAKE_CUDA_HOST_IMPLICIT_LINK_LIBRARIES "")
  set(CMAKE_CUDA_HOST_IMPLICIT_LINK_FRAMEWORK_DIRECTORIES "")
elseif(CMAKE_CUDA_COMPILER_ID STREQUAL "NVIDIA")
  set(_nvcc_log "")
  string(REPLACE "\r" "" _nvcc_output_orig "${CMAKE_CUDA_COMPILER_PRODUCED_OUTPUT}")
  if(_nvcc_output_orig MATCHES "#\\\$ +PATH= *([^\n]*)\n")
    set(_nvcc_path "${CMAKE_MATCH_1}")
    string(APPEND _nvcc_log "  found 'PATH=' string: [${_nvcc_path}]\n")
    string(REPLACE ":" ";" _nvcc_path "${_nvcc_path}")
  else()
    set(_nvcc_path "")
    string(REPLACE "\n" "\n    " _nvcc_output_log "\n${_nvcc_output_orig}")
    string(APPEND _nvcc_log "  no 'PATH=' string found in nvcc output:${_nvcc_output_log}\n")
  endif()
  if(_nvcc_output_orig MATCHES "#\\\$ +LIBRARIES= *([^\n]*)\n")
    set(_nvcc_libraries "${CMAKE_MATCH_1}")
    string(APPEND _nvcc_log "  found 'LIBRARIES=' string: [${_nvcc_libraries}]\n")
  else()
    set(_nvcc_libraries "")
    string(REPLACE "\n" "\n    " _nvcc_output_log "\n${_nvcc_output_orig}")
    string(APPEND _nvcc_log "  no 'LIBRARIES=' string found in nvcc output:${_nvcc_output_log}\n")
  endif()

  set(_nvcc_link_line "")
  if(_nvcc_libraries)
    # Remove variable assignments.
    string(REGEX REPLACE "#\\\$ *[^= ]+=[^\n]*\n" "" _nvcc_output "${_nvcc_output_orig}")
    # Encode [] characters that break list expansion.
    string(REPLACE "[" "{==={" _nvcc_output "${_nvcc_output}")
    string(REPLACE "]" "}===}" _nvcc_output "${_nvcc_output}")
    # Split lines.
    string(REGEX REPLACE "\n+(#\\\$ )?" ";" _nvcc_output "${_nvcc_output}")
    foreach(line IN LISTS _nvcc_output)
      set(_nvcc_output_line "${line}")
      string(REPLACE "{==={" "[" _nvcc_output_line "${_nvcc_output_line}")
      string(REPLACE "}===}" "]" _nvcc_output_line "${_nvcc_output_line}")
      string(APPEND _nvcc_log "  considering line: [${_nvcc_output_line}]\n")
      if("${_nvcc_output_line}" MATCHES "^ *nvlink")
        string(APPEND _nvcc_log "    ignoring nvlink line\n")
      elseif(_nvcc_libraries)
        if("${_nvcc_output_line}" MATCHES "(@\"?tmp/a\\.exe\\.res\"?)")
          set(_nvcc_link_res_arg "${CMAKE_MATCH_1}")
          set(_nvcc_link_res "${CMAKE_PLATFORM_INFO_DIR}/CompilerIdCUDA/tmp/a.exe.res")
          if(EXISTS "${_nvcc_link_res}")
            file(READ "${_nvcc_link_res}" _nvcc_link_res_content)
            string(REPLACE "${_nvcc_link_res_arg}" "${_nvcc_link_res_content}" _nvcc_output_line "${_nvcc_output_line}")
          endif()
        endif()
        string(FIND "${_nvcc_output_line}" "${_nvcc_libraries}" _nvcc_libraries_pos)
        if(NOT _nvcc_libraries_pos EQUAL -1)
          set(_nvcc_link_line "${_nvcc_output_line}")
          string(APPEND _nvcc_log "    extracted link line: [${_nvcc_link_line}]\n")
        endif()
      endif()
    endforeach()
  endif()

  if(_nvcc_link_line)
    if("x${CMAKE_CUDA_SIMULATE_ID}" STREQUAL "xMSVC")
      set(CMAKE_CUDA_HOST_LINK_LAUNCHER "${CMAKE_LINKER}")
    else()
      #extract the compiler that is being used for linking
      separate_arguments(_nvcc_link_line_args UNIX_COMMAND "${_nvcc_link_line}")
      list(GET _nvcc_link_line_args 0 _nvcc_host_link_launcher)
      if(IS_ABSOLUTE "${_nvcc_host_link_launcher}")
        string(APPEND _nvcc_log "  extracted link launcher absolute path: [${_nvcc_host_link_launcher}]\n")
        set(CMAKE_CUDA_HOST_LINK_LAUNCHER "${_nvcc_host_link_launcher}")
      else()
        string(APPEND _nvcc_log "  extracted link launcher name: [${_nvcc_host_link_launcher}]\n")
        find_program(_nvcc_find_host_link_launcher
          NAMES ${_nvcc_host_link_launcher}
          PATHS ${_nvcc_path} NO_DEFAULT_PATH)
        find_program(_nvcc_find_host_link_launcher
          NAMES ${_nvcc_host_link_launcher})
        if(_nvcc_find_host_link_launcher)
          string(APPEND _nvcc_log "  found link launcher absolute path: [${_nvcc_find_host_link_launcher}]\n")
          set(CMAKE_CUDA_HOST_LINK_LAUNCHER "${_nvcc_find_host_link_launcher}")
        else()
          string(APPEND _nvcc_log "  could not find link launcher absolute path\n")
          set(CMAKE_CUDA_HOST_LINK_LAUNCHER "${_nvcc_host_link_launcher}")
        endif()
        unset(_nvcc_find_host_link_launcher CACHE)
      endif()
    endif()

    #prefix the line with cuda-fake-ld so that implicit link info believes it is
    #a link line
    set(_nvcc_link_line "cuda-fake-ld ${_nvcc_link_line}")
    CMAKE_PARSE_IMPLICIT_LINK_INFO("${_nvcc_link_line}"
                                   CMAKE_CUDA_HOST_IMPLICIT_LINK_LIBRARIES
                                   CMAKE_CUDA_HOST_IMPLICIT_LINK_DIRECTORIES
                                   CMAKE_CUDA_HOST_IMPLICIT_LINK_FRAMEWORK_DIRECTORIES
                                   log
                                   "${CMAKE_CUDA_IMPLICIT_OBJECT_REGEX}")

    # Detect CMAKE_CUDA_RUNTIME_LIBRARY_DEFAULT from the compiler by looking at which
    # cudart library exists in the implicit link libraries passed to the host linker.
    # This is required when a project sets the cuda runtime library as part of the
    # initial flags.
    if(";${CMAKE_CUDA_HOST_IMPLICIT_LINK_LIBRARIES};" MATCHES [[;cudart_static(\.lib)?;]])
      set(CMAKE_CUDA_RUNTIME_LIBRARY_DEFAULT "STATIC")
    elseif(";${CMAKE_CUDA_HOST_IMPLICIT_LINK_LIBRARIES};" MATCHES [[;cudart(\.lib)?;]])
      set(CMAKE_CUDA_RUNTIME_LIBRARY_DEFAULT "SHARED")
    else()
      set(CMAKE_CUDA_RUNTIME_LIBRARY_DEFAULT "NONE")
    endif()
    set(_SET_CMAKE_CUDA_RUNTIME_LIBRARY_DEFAULT
      "set(CMAKE_CUDA_RUNTIME_LIBRARY_DEFAULT \"${CMAKE_CUDA_RUNTIME_LIBRARY_DEFAULT}\")")

    file(APPEND ${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CMakeOutput.log
      "Parsed CUDA nvcc implicit link information from above output:\n${_nvcc_log}\n${log}\n\n")
  else()
    file(APPEND ${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CMakeError.log
      "Failed to parse CUDA nvcc implicit link information:\n${_nvcc_log}\n\n")
    message(FATAL_ERROR "Failed to extract nvcc implicit link line.")
  endif()
endif()

# CMAKE_CUDA_HOST_IMPLICIT_LINK_LIBRARIES is detected above as the list of
# libraries that the CUDA compiler implicitly passes to the host linker.
# CMake invokes the host linker directly and so needs to pass these libraries.
# We filter out those that should not be passed unconditionally both here
# and from CMAKE_CUDA_IMPLICIT_LINK_LIBRARIES in CMakeTestCUDACompiler.
set(CMAKE_CUDA_IMPLICIT_LINK_LIBRARIES_EXCLUDE
  # The CUDA runtime libraries are controlled by CMAKE_CUDA_RUNTIME_LIBRARY.
  cudart        cudart.lib
  cudart_static cudart_static.lib
  cudadevrt     cudadevrt.lib

  # Dependencies of the CUDA static runtime library on Linux hosts.
  rt
  pthread
  dl
  )
list(REMOVE_ITEM CMAKE_CUDA_HOST_IMPLICIT_LINK_LIBRARIES ${CMAKE_CUDA_IMPLICIT_LINK_LIBRARIES_EXCLUDE})

if(CMAKE_CUDA_COMPILER_SYSROOT)
  string(CONCAT _SET_CMAKE_CUDA_COMPILER_SYSROOT
    "set(CMAKE_CUDA_COMPILER_SYSROOT \"${CMAKE_CUDA_COMPILER_SYSROOT}\")\n"
    "set(CMAKE_COMPILER_SYSROOT \"${CMAKE_CUDA_COMPILER_SYSROOT}\")")
else()
  set(_SET_CMAKE_CUDA_COMPILER_SYSROOT "")
endif()

# Determine CMAKE_CUDA_TOOLKIT_INCLUDE_DIRECTORIES
if(CMAKE_CUDA_COMPILER_ID STREQUAL "NVIDIA")
  set(CMAKE_CUDA_TOOLKIT_INCLUDE_DIRECTORIES)
  string(REPLACE "\r" "" _nvcc_output_orig "${CMAKE_CUDA_COMPILER_PRODUCED_OUTPUT}")
  if(_nvcc_output_orig MATCHES "#\\\$ +INCLUDES= *([^\n]*)\n")
    set(_nvcc_includes "${CMAKE_MATCH_1}")
    string(APPEND _nvcc_log "  found 'INCLUDES=' string: [${_nvcc_includes}]\n")
  else()
    set(_nvcc_includes "")
    string(REPLACE "\n" "\n    " _nvcc_output_log "\n${_nvcc_output_orig}")
    string(APPEND _nvcc_log "  no 'INCLUDES=' string found in nvcc output:${_nvcc_output_log}\n")
  endif()
  if(_nvcc_includes)
    # across all operating system each include directory is prefixed with -I
    separate_arguments(_nvcc_output NATIVE_COMMAND "${_nvcc_includes}")
    foreach(line IN LISTS _nvcc_output)
      string(REGEX REPLACE "^-I" "" line "${line}")
      get_filename_component(line "${line}" ABSOLUTE)
      list(APPEND CMAKE_CUDA_TOOLKIT_INCLUDE_DIRECTORIES "${line}")
    endforeach()

    file(APPEND ${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CMakeOutput.log
      "Parsed CUDA nvcc include information from above output:\n${_nvcc_log}\n${log}\n\n")
  else()
    file(APPEND ${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CMakeOutput.log
      "Failed to detect CUDA nvcc include information:\n${_nvcc_log}\n\n")
  endif()

  # Parse default CUDA architecture.
  cmake_policy(GET CMP0104 _CUDA_CMP0104)
  if(NOT CMAKE_CUDA_ARCHITECTURES AND _CUDA_CMP0104 STREQUAL "NEW")
    string(REGEX MATCH "arch[ =]compute_([0-9]+)" dont_care "${CMAKE_CUDA_COMPILER_PRODUCED_OUTPUT}")
    set(detected_architecture "${CMAKE_MATCH_1}")
  elseif(CMAKE_CUDA_ARCHITECTURES)
    string(REGEX MATCHALL "-arch compute_([0-9]+)" target_cpus "${CMAKE_CUDA_COMPILER_PRODUCED_OUTPUT}")

    foreach(cpu ${target_cpus})
      string(REGEX MATCH "-arch compute_([0-9]+)" dont_care "${cpu}")
      list(APPEND architectures "${CMAKE_MATCH_1}")
    endforeach()
  endif()
endif()

# If the user didn't set the architectures, then set them to a default.
# If the user did, then make sure those architectures worked.
if(DEFINED detected_architecture AND "${CMAKE_CUDA_ARCHITECTURES}" STREQUAL "")
  set(CMAKE_CUDA_ARCHITECTURES "${detected_architecture}" CACHE STRING "CUDA architectures")

  if(NOT CMAKE_CUDA_ARCHITECTURES)
    message(FATAL_ERROR "Failed to find a working CUDA architecture.")
  endif()
elseif(architectures)
  # Sort since order mustn't matter.
  list(SORT architectures)
  list(SORT tested_architectures)

  # We don't distinguish real/virtual architectures during testing.
  # For "70-real;70-virtual" we detect "70" as working and tested_architectures is "70;70".
  # Thus we need to remove duplicates before checking if they're equal.
  list(REMOVE_DUPLICATES tested_architectures)

  if(NOT "${architectures}" STREQUAL "${tested_architectures}")
    message(FATAL_ERROR
      "The CMAKE_CUDA_ARCHITECTURES:\n"
      "  ${CMAKE_CUDA_ARCHITECTURES}\n"
      "do not all work with this compiler.  Try:\n"
      "  ${architectures}\n"
      "instead.")
  endif()
endif()

# configure all variables set in this file
configure_file(${CMAKE_ROOT}/Modules/CMakeCUDACompiler.cmake.in
  ${CMAKE_PLATFORM_INFO_DIR}/CMakeCUDACompiler.cmake
  @ONLY
)

# Don't leak variables unnecessarily to user code.
unset(_CUDA_INCLUDE_DIR CACHE)
unset(_CUDA_NVCC_EXECUTABLE CACHE)
unset(_CUDA_LIBRARY_DIR)
unset(_CUDA_TARGET_DIR)
unset(_CUDA_TARGET_NAME)

set(CMAKE_CUDA_COMPILER_ENV_VAR "CUDACXX")
set(CMAKE_CUDA_HOST_COMPILER_ENV_VAR "CUDAHOSTCXX")
