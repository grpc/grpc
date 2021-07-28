# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.


# Function to compile a source file to identify the compiler.  This is
# used internally by CMake and should not be included by user code.
# If successful, sets CMAKE_<lang>_COMPILER_ID and CMAKE_<lang>_PLATFORM_ID

function(CMAKE_DETERMINE_COMPILER_ID lang flagvar src)
  # Make sure the compiler arguments are clean.
  string(STRIP "${CMAKE_${lang}_COMPILER_ARG1}" CMAKE_${lang}_COMPILER_ID_ARG1)
  string(REGEX REPLACE " +" ";" CMAKE_${lang}_COMPILER_ID_ARG1 "${CMAKE_${lang}_COMPILER_ID_ARG1}")

  # Make sure user-specified compiler flags are used.
  if(CMAKE_${lang}_FLAGS)
    set(CMAKE_${lang}_COMPILER_ID_FLAGS ${CMAKE_${lang}_FLAGS})
  elseif(DEFINED ENV{${flagvar}})
    set(CMAKE_${lang}_COMPILER_ID_FLAGS $ENV{${flagvar}})
  else(CMAKE_${lang}_FLAGS_INIT)
    set(CMAKE_${lang}_COMPILER_ID_FLAGS ${CMAKE_${lang}_FLAGS_INIT})
  endif()
  string(REPLACE " " ";" CMAKE_${lang}_COMPILER_ID_FLAGS_LIST "${CMAKE_${lang}_COMPILER_ID_FLAGS}")

  # Compute the directory in which to run the test.
  set(CMAKE_${lang}_COMPILER_ID_DIR ${CMAKE_PLATFORM_INFO_DIR}/CompilerId${lang})

  # Try building with no extra flags and then try each set
  # of helper flags.  Stop when the compiler is identified.
  foreach(userflags "${CMAKE_${lang}_COMPILER_ID_FLAGS_LIST}" "")
    foreach(testflags ${CMAKE_${lang}_COMPILER_ID_TEST_FLAGS_FIRST}
                      ""
                      ${CMAKE_${lang}_COMPILER_ID_TEST_FLAGS})
      separate_arguments(testflags UNIX_COMMAND "${testflags}")
      CMAKE_DETERMINE_COMPILER_ID_BUILD("${lang}" "${testflags}" "${userflags}" "${src}")
      CMAKE_DETERMINE_COMPILER_ID_MATCH_VENDOR("${lang}" "${COMPILER_${lang}_PRODUCED_OUTPUT}")
      if(CMAKE_${lang}_COMPILER_ID)
        break()
      endif()
      foreach(file ${COMPILER_${lang}_PRODUCED_FILES})
        CMAKE_DETERMINE_COMPILER_ID_CHECK("${lang}" "${CMAKE_${lang}_COMPILER_ID_DIR}/${file}" "${src}")
      endforeach()
      if(CMAKE_${lang}_COMPILER_ID)
        break()
      endif()
    endforeach()
    if(CMAKE_${lang}_COMPILER_ID)
      break()
    endif()
  endforeach()

  # Check if compiler id detection gave us the compiler tool.
  if(CMAKE_${lang}_COMPILER_ID_TOOL)
    set(CMAKE_${lang}_COMPILER "${CMAKE_${lang}_COMPILER_ID_TOOL}")
    set(CMAKE_${lang}_COMPILER "${CMAKE_${lang}_COMPILER_ID_TOOL}" PARENT_SCOPE)
  elseif(NOT CMAKE_${lang}_COMPILER)
    set(CMAKE_${lang}_COMPILER "CMAKE_${lang}_COMPILER-NOTFOUND" PARENT_SCOPE)
  endif()

  # If the compiler is still unknown, try to query its vendor.
  if(CMAKE_${lang}_COMPILER AND NOT CMAKE_${lang}_COMPILER_ID)
    foreach(userflags "${CMAKE_${lang}_COMPILER_ID_FLAGS_LIST}" "")
      CMAKE_DETERMINE_COMPILER_ID_VENDOR(${lang} "${userflags}")
    endforeach()
  endif()

  # If the compiler is still unknown, fallback to GHS
  if(NOT CMAKE_${lang}_COMPILER_ID  AND "${CMAKE_GENERATOR}" MATCHES "Green Hills MULTI")
    set(CMAKE_${lang}_COMPILER_ID GHS)
    file(APPEND ${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CMakeOutput.log
        "The ${lang} compiler identification is falling back to GHS.\n\n")
  endif()

  # CUDA < 7.5 is missing version macros
  if(lang STREQUAL "CUDA"
     AND CMAKE_${lang}_COMPILER_ID STREQUAL "NVIDIA"
     AND NOT CMAKE_${lang}_COMPILER_VERSION)
    execute_process(
      COMMAND "${CMAKE_${lang}_COMPILER}"
      --version
      OUTPUT_VARIABLE output ERROR_VARIABLE output
      RESULT_VARIABLE result
      TIMEOUT 10
    )
    if(output MATCHES [=[ V([0-9]+)\.([0-9]+)\.([0-9]+)]=])
      set(CMAKE_${lang}_COMPILER_VERSION "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3}")
    endif()
  endif()

  # For Swift we need to explicitly query the version.
  if(lang STREQUAL "Swift"
     AND CMAKE_${lang}_COMPILER
     AND NOT CMAKE_${lang}_COMPILER_VERSION)
    execute_process(
      COMMAND "${CMAKE_${lang}_COMPILER}"
      -version
      OUTPUT_VARIABLE output ERROR_VARIABLE output
      RESULT_VARIABLE result
      TIMEOUT 10
    )
    file(APPEND ${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CMakeOutput.log
      "Running the ${lang} compiler: \"${CMAKE_${lang}_COMPILER}\" -version\n"
      "${output}\n"
      )

    if(output MATCHES [[Swift version ([0-9]+\.[0-9]+(\.[0-9]+)?)]])
      set(CMAKE_${lang}_COMPILER_VERSION "${CMAKE_MATCH_1}")
      if(NOT CMAKE_${lang}_COMPILER_ID)
        set(CMAKE_Swift_COMPILER_ID "Apple")
      endif()
    endif()
  endif()

  # For ISPC we need to explicitly query the version.
  if(lang STREQUAL "ISPC"
     AND CMAKE_${lang}_COMPILER
     AND NOT CMAKE_${lang}_COMPILER_VERSION)
    execute_process(
      COMMAND "${CMAKE_${lang}_COMPILER}"
      --version
      OUTPUT_VARIABLE output ERROR_VARIABLE output
      RESULT_VARIABLE result
      TIMEOUT 10
    )
    file(APPEND ${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CMakeOutput.log
      "Running the ${lang} compiler: \"${CMAKE_${lang}_COMPILER}\" -version\n"
      "${output}\n"
      )

    if(output MATCHES [[ISPC\), ([0-9]+\.[0-9]+(\.[0-9]+)?)]])
      set(CMAKE_${lang}_COMPILER_VERSION "${CMAKE_MATCH_1}")
    endif()
  endif()


  if (COMPILER_QNXNTO AND CMAKE_${lang}_COMPILER_ID STREQUAL "GNU")
    execute_process(
      COMMAND "${CMAKE_${lang}_COMPILER}"
      -V
      OUTPUT_VARIABLE output ERROR_VARIABLE output
      RESULT_VARIABLE result
      TIMEOUT 10
      )
    if (output MATCHES "targets available")
      set(CMAKE_${lang}_COMPILER_ID QCC)
      # http://community.qnx.com/sf/discussion/do/listPosts/projects.community/discussion.qnx_momentics_community_support.topc3555?_pagenum=2
      # The qcc driver does not itself have a version.
    endif()
  endif()

  # if the format is unknown after all files have been checked, put "Unknown" in the cache
  if(NOT CMAKE_EXECUTABLE_FORMAT)
    set(CMAKE_EXECUTABLE_FORMAT "Unknown" CACHE INTERNAL "Executable file format")
  endif()

  if(CMAKE_GENERATOR MATCHES "^Ninja" AND MSVC_${lang}_ARCHITECTURE_ID)
    foreach(userflags "${CMAKE_${lang}_COMPILER_ID_FLAGS_LIST}" "")
      CMAKE_DETERMINE_MSVC_SHOWINCLUDES_PREFIX(${lang} "${userflags}")
    endforeach()
  else()
    set(CMAKE_${lang}_CL_SHOWINCLUDES_PREFIX "")
  endif()

  set(_variant "")
  if("x${CMAKE_${lang}_COMPILER_ID}" STREQUAL "xClang")
    if("x${CMAKE_${lang}_SIMULATE_ID}" STREQUAL "xMSVC")
      if(CMAKE_GENERATOR MATCHES "Visual Studio")
        set(CMAKE_${lang}_COMPILER_FRONTEND_VARIANT "MSVC")
      else()
        # Test whether an MSVC-like command-line option works.
        execute_process(COMMAND "${CMAKE_${lang}_COMPILER}" -?
          RESULT_VARIABLE _clang_result
          OUTPUT_VARIABLE _clang_stdout
          ERROR_VARIABLE _clang_stderr)
        if(_clang_result EQUAL 0)
          set(CMAKE_${lang}_COMPILER_FRONTEND_VARIANT "MSVC")
        else()
          set(CMAKE_${lang}_COMPILER_FRONTEND_VARIANT "GNU")
        endif()
      endif()
      set(_variant " with ${CMAKE_${lang}_COMPILER_FRONTEND_VARIANT}-like command-line")
    else()
      set(CMAKE_${lang}_COMPILER_FRONTEND_VARIANT "GNU")
    endif()
  else()
    set(CMAKE_${lang}_COMPILER_FRONTEND_VARIANT "")
  endif()

  # Display the final identification result.
  if(CMAKE_${lang}_COMPILER_ID)
    if(CMAKE_${lang}_COMPILER_VERSION)
      set(_version " ${CMAKE_${lang}_COMPILER_VERSION}")
    else()
      set(_version "")
    endif()
    if(CMAKE_${lang}_COMPILER_ARCHITECTURE_ID AND "x${CMAKE_${lang}_COMPILER_ID}" STREQUAL "xIAR")
      set(_archid " ${CMAKE_${lang}_COMPILER_ARCHITECTURE_ID}")
    else()
      set(_archid "")
    endif()
    message(STATUS "The ${lang} compiler identification is "
      "${CMAKE_${lang}_COMPILER_ID}${_archid}${_version}${_variant}")
    unset(_archid)
    unset(_version)
    unset(_variant)
  else()
    message(STATUS "The ${lang} compiler identification is unknown")
  endif()

  if(lang STREQUAL "Fortran" AND CMAKE_${lang}_COMPILER_ID STREQUAL "XL")
    set(CMAKE_${lang}_XL_CPP "${CMAKE_${lang}_COMPILER_ID_CPP}" PARENT_SCOPE)
  endif()

  set(CMAKE_${lang}_COMPILER_ID "${CMAKE_${lang}_COMPILER_ID}" PARENT_SCOPE)
  set(CMAKE_${lang}_PLATFORM_ID "${CMAKE_${lang}_PLATFORM_ID}" PARENT_SCOPE)
  set(CMAKE_${lang}_COMPILER_ARCHITECTURE_ID "${CMAKE_${lang}_COMPILER_ARCHITECTURE_ID}" PARENT_SCOPE)
  set(MSVC_${lang}_ARCHITECTURE_ID "${MSVC_${lang}_ARCHITECTURE_ID}"
    PARENT_SCOPE)
  set(CMAKE_${lang}_XCODE_ARCHS "${CMAKE_${lang}_XCODE_ARCHS}" PARENT_SCOPE)
  set(CMAKE_${lang}_CL_SHOWINCLUDES_PREFIX "${CMAKE_${lang}_CL_SHOWINCLUDES_PREFIX}" PARENT_SCOPE)
  set(CMAKE_${lang}_COMPILER_FRONTEND_VARIANT "${CMAKE_${lang}_COMPILER_FRONTEND_VARIANT}" PARENT_SCOPE)
  set(CMAKE_${lang}_COMPILER_VERSION "${CMAKE_${lang}_COMPILER_VERSION}" PARENT_SCOPE)
  set(CMAKE_${lang}_COMPILER_VERSION_INTERNAL "${CMAKE_${lang}_COMPILER_VERSION_INTERNAL}" PARENT_SCOPE)
  set(CMAKE_${lang}_COMPILER_WRAPPER "${CMAKE_${lang}_COMPILER_WRAPPER}" PARENT_SCOPE)
  set(CMAKE_${lang}_SIMULATE_ID "${CMAKE_${lang}_SIMULATE_ID}" PARENT_SCOPE)
  set(CMAKE_${lang}_SIMULATE_VERSION "${CMAKE_${lang}_SIMULATE_VERSION}" PARENT_SCOPE)
  set(CMAKE_${lang}_STANDARD_COMPUTED_DEFAULT "${CMAKE_${lang}_STANDARD_COMPUTED_DEFAULT}" PARENT_SCOPE)
  set(CMAKE_${lang}_COMPILER_PRODUCED_OUTPUT "${COMPILER_${lang}_PRODUCED_OUTPUT}" PARENT_SCOPE)
  set(CMAKE_${lang}_COMPILER_PRODUCED_FILES "${COMPILER_${lang}_PRODUCED_FILES}" PARENT_SCOPE)
endfunction()

include(CMakeCompilerIdDetection)

#-----------------------------------------------------------------------------
# Function to write the compiler id source file.
function(CMAKE_DETERMINE_COMPILER_ID_WRITE lang src)
  find_file(src_in ${src}.in PATHS ${CMAKE_ROOT}/Modules ${CMAKE_MODULE_PATH} NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
  file(READ ${src_in} ID_CONTENT_IN)

  compiler_id_detection(CMAKE_${lang}_COMPILER_ID_CONTENT ${lang}
    ID_STRING
    VERSION_STRINGS
    PLATFORM_DEFAULT_COMPILER
  )

  unset(src_in CACHE)
  string(CONFIGURE "${ID_CONTENT_IN}" ID_CONTENT_OUT @ONLY)
  file(WRITE ${CMAKE_${lang}_COMPILER_ID_DIR}/${src} "${ID_CONTENT_OUT}")
endfunction()

#-----------------------------------------------------------------------------
# Function to build the compiler id source file and look for output
# files.
function(CMAKE_DETERMINE_COMPILER_ID_BUILD lang testflags userflags src)
  # Create a clean working directory.
  file(REMOVE_RECURSE ${CMAKE_${lang}_COMPILER_ID_DIR})
  file(MAKE_DIRECTORY ${CMAKE_${lang}_COMPILER_ID_DIR})
  file(MAKE_DIRECTORY ${CMAKE_${lang}_COMPILER_ID_DIR}/tmp)
  CMAKE_DETERMINE_COMPILER_ID_WRITE("${lang}" "${src}")

  # Construct a description of this test case.
  set(COMPILER_DESCRIPTION
    "Compiler: ${CMAKE_${lang}_COMPILER} ${CMAKE_${lang}_COMPILER_ID_ARG1}
Build flags: ${userflags}
Id flags: ${testflags} ${CMAKE_${lang}_COMPILER_ID_FLAGS_ALWAYS}
")

  # Compile the compiler identification source.
  if("${CMAKE_GENERATOR}" MATCHES "Visual Studio ([0-9]+)")
    set(vs_version ${CMAKE_MATCH_1})
    set(id_platform ${CMAKE_VS_PLATFORM_NAME})
    set(id_lang "${lang}")
    set(id_PostBuildEvent_Command "")
    if(CMAKE_VS_PLATFORM_TOOLSET MATCHES "^[Ll][Ll][Vv][Mm](_v[0-9]+(_xp)?)?$")
      set(id_cl_var "ClangClExecutable")
    elseif(CMAKE_VS_PLATFORM_TOOLSET MATCHES "^[Cc][Ll][Aa][Nn][Gg]([Cc][Ll]$|_[0-9])")
      set(id_cl "$(CLToolExe)")
    elseif(CMAKE_VS_PLATFORM_TOOLSET MATCHES "v[0-9]+_clang_.*")
      set(id_cl clang.exe)
    else()
      set(id_cl cl.exe)
    endif()
    if(CMAKE_VS_PLATFORM_NAME STREQUAL "Tegra-Android")
      set(v NsightTegra)
      set(ext vcxproj)
      if(lang STREQUAL CXX)
        set(id_gcc g++)
        set(id_clang clang++)
      else()
        set(id_gcc gcc)
        set(id_clang clang)
      endif()
    elseif(lang STREQUAL Fortran)
      set(v Intel)
      set(ext vfproj)
      set(id_cl ifort.exe)
    elseif(lang STREQUAL CSharp)
      set(v 10)
      set(ext csproj)
      set(id_cl csc.exe)
    elseif(NOT "${vs_version}" VERSION_LESS 10)
      set(v 10)
      set(ext vcxproj)
    else()
      set(id_version ${vs_version}.00)
      set(v 7)
      set(ext vcproj)
    endif()
    if(CMAKE_VS_PLATFORM_TOOLSET)
      if(CMAKE_VS_PLATFORM_NAME STREQUAL "Tegra-Android")
        set(id_toolset "<NdkToolchainVersion>${CMAKE_VS_PLATFORM_TOOLSET}</NdkToolchainVersion>")
      else()
        set(id_toolset "<PlatformToolset>${CMAKE_VS_PLATFORM_TOOLSET}</PlatformToolset>")
        if(CMAKE_VS_PLATFORM_TOOLSET MATCHES "Intel")
          set(id_cl icl.exe)
        endif()
        if(CMAKE_VS_PLATFORM_TOOLSET_VERSION)
          set(id_sep "\\")
          if(CMAKE_VS_PLATFORM_TOOLSET_VERSION VERSION_GREATER_EQUAL "14.20")
            if(EXISTS "${CMAKE_GENERATOR_INSTANCE}/VC/Auxiliary/Build.${CMAKE_VS_PLATFORM_TOOLSET_VERSION}/Microsoft.VCToolsVersion.${CMAKE_VS_PLATFORM_TOOLSET_VERSION}.props")
              set(id_sep ".")
            endif()
          endif()
          set(id_toolset_version_props "<Import Project=\"${CMAKE_GENERATOR_INSTANCE}\\VC\\Auxiliary\\Build${id_sep}${CMAKE_VS_PLATFORM_TOOLSET_VERSION}\\Microsoft.VCToolsVersion.${CMAKE_VS_PLATFORM_TOOLSET_VERSION}.props\" />")
          unset(id_sep)
        endif()
      endif()
    else()
      set(id_toolset "")
      set(id_toolset_version_props "")
    endif()
    if(CMAKE_VS_PLATFORM_TOOLSET_HOST_ARCHITECTURE)
      set(id_PreferredToolArchitecture "<PreferredToolArchitecture>${CMAKE_VS_PLATFORM_TOOLSET_HOST_ARCHITECTURE}</PreferredToolArchitecture>")
    else()
      set(id_PreferredToolArchitecture "")
    endif()
    if(CMAKE_SYSTEM_NAME STREQUAL "WindowsPhone")
      set(id_keyword "Win32Proj")
      set(id_system "<ApplicationType>Windows Phone</ApplicationType>")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "WindowsStore")
      set(id_keyword "Win32Proj")
      set(id_system "<ApplicationType>Windows Store</ApplicationType>")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Android")
      set(id_keyword "Android")
      set(id_system "<ApplicationType>Android</ApplicationType>")
    else()
      set(id_keyword "Win32Proj")
      set(id_system "")
    endif()
    if(id_keyword STREQUAL "Android")
      if(CMAKE_GENERATOR MATCHES "Visual Studio 14")
        set(id_system_version "<ApplicationTypeRevision>2.0</ApplicationTypeRevision>")
      elseif(CMAKE_GENERATOR MATCHES "Visual Studio 1[56]")
        set(id_system_version "<ApplicationTypeRevision>3.0</ApplicationTypeRevision>")
      else()
        set(id_system_version "")
      endif()
    elseif(id_system AND CMAKE_SYSTEM_VERSION MATCHES "^([0-9]+\\.[0-9]+)")
      set(id_system_version "<ApplicationTypeRevision>${CMAKE_MATCH_1}</ApplicationTypeRevision>")
    else()
      set(id_system_version "")
    endif()
    if(id_keyword STREQUAL "Android")
      set(id_config_type "DynamicLibrary")
    else()
      set(id_config_type "Application")
    endif()
    if(CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION)
      set(id_WindowsTargetPlatformVersion "<WindowsTargetPlatformVersion>${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}</WindowsTargetPlatformVersion>")
    endif()
    if(CMAKE_VS_PLATFORM_TOOLSET_VCTARGETS_CUSTOM_DIR)
      set(id_ToolsetVCTargetsDir "<VCTargetsPath>${CMAKE_VS_PLATFORM_TOOLSET_VCTARGETS_CUSTOM_DIR}</VCTargetsPath>")
    endif()
    set(id_CustomGlobals "")
    foreach(pair IN LISTS CMAKE_VS_GLOBALS)
      if("${pair}" MATCHES "([^=]+)=(.*)$")
        string(APPEND id_CustomGlobals "<${CMAKE_MATCH_1}>${CMAKE_MATCH_2}</${CMAKE_MATCH_1}>\n    ")
      endif()
    endforeach()
    if(id_keyword STREQUAL "Android")
      set(id_WindowsSDKDesktopARMSupport "")
    elseif(id_platform STREQUAL "ARM64")
      set(id_WindowsSDKDesktopARMSupport "<WindowsSDKDesktopARM64Support>true</WindowsSDKDesktopARM64Support>")
    elseif(id_platform STREQUAL "ARM")
      set(id_WindowsSDKDesktopARMSupport "<WindowsSDKDesktopARMSupport>true</WindowsSDKDesktopARMSupport>")
    else()
      set(id_WindowsSDKDesktopARMSupport "")
    endif()
    if(CMAKE_VS_WINCE_VERSION)
      set(id_entrypoint "mainACRTStartup")
      if("${vs_version}" VERSION_LESS 9)
        set(id_subsystem 9)
      else()
        set(id_subsystem 8)
      endif()
    else()
      set(id_subsystem 1)
    endif()
    set(id_dir ${CMAKE_${lang}_COMPILER_ID_DIR})
    set(id_src "${src}")
    set(id_compile "ClCompile")
    if(id_cl_var)
      set(id_PostBuildEvent_Command "echo CMAKE_${lang}_COMPILER=$(${id_cl_var})")
    else()
      set(id_PostBuildEvent_Command "for %%i in (${id_cl}) do %40echo CMAKE_${lang}_COMPILER=%%~$PATH:i")
    endif()
    set(id_Import_props "")
    set(id_Import_targets "")
    set(id_ItemDefinitionGroup_entry "")
    set(id_Link_AdditionalDependencies "")
    if(lang STREQUAL CUDA)
      if(NOT CMAKE_VS_PLATFORM_TOOLSET_CUDA)
        message(FATAL_ERROR "No CUDA toolset found.")
      endif()
      set(cuda_tools "CUDA ${CMAKE_VS_PLATFORM_TOOLSET_CUDA}")
      set(id_compile "CudaCompile")
      if(CMAKE_VS_PLATFORM_NAME STREQUAL x64)
        set(cuda_target "<TargetMachinePlatform>64</TargetMachinePlatform>")
      endif()
      foreach(arch ${CMAKE_CUDA_ARCHITECTURES})
        string(REGEX MATCH "[0-9]+" arch_name "${arch}")
        string(APPEND cuda_codegen "compute_${arch_name},sm_${arch_name};")
      endforeach()
      set(id_ItemDefinitionGroup_entry "<CudaCompile>${cuda_target}<AdditionalOptions>%(AdditionalOptions)-v</AdditionalOptions><CodeGeneration>${cuda_codegen}</CodeGeneration></CudaCompile>")
      set(id_PostBuildEvent_Command [[echo CMAKE_CUDA_COMPILER=$(CudaToolkitBinDir)\nvcc.exe]])
      if(CMAKE_VS_PLATFORM_TOOLSET_CUDA_CUSTOM_DIR)
        set(id_CudaToolkitCustomDir "<CudaToolkitCustomDir>${CMAKE_VS_PLATFORM_TOOLSET_CUDA_CUSTOM_DIR}nvcc</CudaToolkitCustomDir>")
        string(CONCAT id_Import_props "<Import Project=\"${CMAKE_VS_PLATFORM_TOOLSET_CUDA_CUSTOM_DIR}\\CUDAVisualStudioIntegration\\extras\\visual_studio_integration\\MSBuildExtensions\\${cuda_tools}.props\" />")
        string(CONCAT id_Import_targets "<Import Project=\"${CMAKE_VS_PLATFORM_TOOLSET_CUDA_CUSTOM_DIR}\\CUDAVisualStudioIntegration\\extras\\visual_studio_integration\\MSBuildExtensions\\${cuda_tools}.targets\" />")
      else()
        string(CONCAT id_Import_props [[<Import Project="$(VCTargetsPath)\BuildCustomizations\]] "${cuda_tools}" [[.props" />]])
        string(CONCAT id_Import_targets [[<Import Project="$(VCTargetsPath)\BuildCustomizations\]] "${cuda_tools}" [[.targets" />]])
      endif()
      if(CMAKE_CUDA_FLAGS MATCHES "(^| )-cudart +shared( |$)")
        set(id_Link_AdditionalDependencies "<AdditionalDependencies>cudart.lib</AdditionalDependencies>")
      else()
        set(id_Link_AdditionalDependencies "<AdditionalDependencies>cudart_static.lib</AdditionalDependencies>")
      endif()
    endif()
    configure_file(${CMAKE_ROOT}/Modules/CompilerId/VS-${v}.${ext}.in
      ${id_dir}/CompilerId${lang}.${ext} @ONLY)
    if(CMAKE_VS_MSBUILD_COMMAND AND NOT lang STREQUAL "Fortran")
      set(command "${CMAKE_VS_MSBUILD_COMMAND}" "CompilerId${lang}.${ext}"
        "/p:Configuration=Debug" "/p:Platform=${id_platform}" "/p:VisualStudioVersion=${vs_version}.0"
        )
    elseif(CMAKE_VS_DEVENV_COMMAND)
      set(command "${CMAKE_VS_DEVENV_COMMAND}" "CompilerId${lang}.${ext}" "/build" "Debug")
    else()
      set(command "")
    endif()
    if(command)
      execute_process(
        COMMAND ${command}
        WORKING_DIRECTORY ${CMAKE_${lang}_COMPILER_ID_DIR}
        OUTPUT_VARIABLE CMAKE_${lang}_COMPILER_ID_OUTPUT
        ERROR_VARIABLE CMAKE_${lang}_COMPILER_ID_OUTPUT
        RESULT_VARIABLE CMAKE_${lang}_COMPILER_ID_RESULT
        )
    else()
      set(CMAKE_${lang}_COMPILER_ID_RESULT 1)
      set(CMAKE_${lang}_COMPILER_ID_OUTPUT "VS environment not known to support ${lang}")
    endif()
    # Match the compiler location line printed out.
    if("${CMAKE_${lang}_COMPILER_ID_OUTPUT}" MATCHES "CMAKE_${lang}_COMPILER=([^%\r\n]+)[\r\n]")
      # Strip VS diagnostic output from the end of the line.
      string(REGEX REPLACE " \\(TaskId:[0-9]*\\)$" "" _comp "${CMAKE_MATCH_1}")
      if(EXISTS "${_comp}")
        file(TO_CMAKE_PATH "${_comp}" _comp)
        set(CMAKE_${lang}_COMPILER_ID_TOOL "${_comp}" PARENT_SCOPE)
      endif()
    endif()
  elseif("${CMAKE_GENERATOR}" MATCHES "Xcode")
    set(id_lang "${lang}")
    set(id_type ${CMAKE_${lang}_COMPILER_XCODE_TYPE})
    set(id_dir ${CMAKE_${lang}_COMPILER_ID_DIR})
    set(id_src "${src}")
    if(CMAKE_XCODE_PLATFORM_TOOLSET)
      set(id_toolset "GCC_VERSION = ${CMAKE_XCODE_PLATFORM_TOOLSET};")
    else()
      set(id_toolset "")
    endif()
    if("${lang}" STREQUAL "Swift")
      if(CMAKE_Swift_LANGUAGE_VERSION)
        set(id_lang_version "SWIFT_VERSION = ${CMAKE_Swift_LANGUAGE_VERSION};")
      elseif(XCODE_VERSION VERSION_GREATER_EQUAL 10.2)
        set(id_lang_version "SWIFT_VERSION = 4.0;")
      elseif(XCODE_VERSION VERSION_GREATER_EQUAL 8.3)
        set(id_lang_version "SWIFT_VERSION = 3.0;")
      else()
        set(id_lang_version "SWIFT_VERSION = 2.3;")
      endif()
    else()
      set(id_lang_version "")
    endif()
    if(CMAKE_OSX_DEPLOYMENT_TARGET)
      set(id_deployment_target
        "MACOSX_DEPLOYMENT_TARGET = \"${CMAKE_OSX_DEPLOYMENT_TARGET}\";")
    else()
      set(id_deployment_target "")
    endif()
    set(id_product_type "com.apple.product-type.tool")
    if(CMAKE_OSX_SYSROOT)
      set(id_sdkroot "SDKROOT = \"${CMAKE_OSX_SYSROOT}\";")
      if(CMAKE_OSX_SYSROOT MATCHES "(^|/)[Ii][Pp][Hh][Oo][Nn][Ee]" OR
        CMAKE_OSX_SYSROOT MATCHES "(^|/)[Aa][Pp][Pp][Ll][Ee][Tt][Vv]")
        set(id_product_type "com.apple.product-type.bundle.unit-test")
      elseif(CMAKE_OSX_SYSROOT MATCHES "(^|/)[Ww][Aa][Tt][Cc][Hh]")
        set(id_product_type "com.apple.product-type.framework")
      endif()
    else()
      set(id_sdkroot "")
    endif()
    set(id_clang_cxx_library "")
    set(stdlib_regex "(^| )(-stdlib=)([^ ]+)( |$)")
    string(REGEX MATCHALL "${stdlib_regex}" all_stdlib_matches "${CMAKE_CXX_FLAGS}")
    if(all_stdlib_matches)
      list(GET all_stdlib_matches "-1" last_stdlib_match)
      if(last_stdlib_match MATCHES "${stdlib_regex}")
        set(id_clang_cxx_library "CLANG_CXX_LIBRARY = \"${CMAKE_MATCH_3}\";")
      endif()
    endif()
    if(CMAKE_SYSTEM_NAME STREQUAL "Darwin" AND CMAKE_OSX_SYSROOT MATCHES "^$|[Mm][Aa][Cc][Oo][Ss]")
      # When targeting macOS, use only the host architecture.
      if (_CMAKE_APPLE_ARCHS_DEFAULT)
        set(id_archs "ARCHS = \"${_CMAKE_APPLE_ARCHS_DEFAULT}\";")
        set(id_arch_active "ONLY_ACTIVE_ARCH = NO;")
      else()
        set(id_archs [[ARCHS = "$(NATIVE_ARCH_ACTUAL)";]])
        set(id_arch_active "ONLY_ACTIVE_ARCH = YES;")
      endif()
    else()
      set(id_archs "")
      set(id_arch_active "ONLY_ACTIVE_ARCH = YES;")
    endif()
    configure_file(${CMAKE_ROOT}/Modules/CompilerId/Xcode-3.pbxproj.in
      ${id_dir}/CompilerId${lang}.xcodeproj/project.pbxproj @ONLY)
    unset(_ENV_MACOSX_DEPLOYMENT_TARGET)
    if(DEFINED ENV{MACOSX_DEPLOYMENT_TARGET})
      set(_ENV_MACOSX_DEPLOYMENT_TARGET "$ENV{MACOSX_DEPLOYMENT_TARGET}")
      set(ENV{MACOSX_DEPLOYMENT_TARGET} "")
    endif()
    execute_process(COMMAND xcodebuild
      WORKING_DIRECTORY ${CMAKE_${lang}_COMPILER_ID_DIR}
      OUTPUT_VARIABLE CMAKE_${lang}_COMPILER_ID_OUTPUT
      ERROR_VARIABLE CMAKE_${lang}_COMPILER_ID_OUTPUT
      RESULT_VARIABLE CMAKE_${lang}_COMPILER_ID_RESULT
      )
    if(DEFINED _ENV_MACOSX_DEPLOYMENT_TARGET)
      set(ENV{MACOSX_DEPLOYMENT_TARGET} "${_ENV_MACOSX_DEPLOYMENT_TARGET}")
    endif()

    if(DEFINED CMAKE_${lang}_COMPILER_ID_TOOL_MATCH_REGEX)
      if("${CMAKE_${lang}_COMPILER_ID_OUTPUT}" MATCHES "${CMAKE_${lang}_COMPILER_ID_TOOL_MATCH_REGEX}")
        set(_comp "${CMAKE_MATCH_${CMAKE_${lang}_COMPILER_ID_TOOL_MATCH_INDEX}}")
        if(EXISTS "${_comp}")
          set(CMAKE_${lang}_COMPILER_ID_TOOL "${_comp}" PARENT_SCOPE)
        endif()
      endif()
    endif()
    if("${CMAKE_${lang}_COMPILER_ID_OUTPUT}" MATCHES "ARCHS=([^%\r\n]+)[\r\n]")
      set(CMAKE_${lang}_XCODE_ARCHS "${CMAKE_MATCH_1}")
      separate_arguments(CMAKE_${lang}_XCODE_ARCHS)
      set(CMAKE_${lang}_XCODE_ARCHS "${CMAKE_${lang}_XCODE_ARCHS}" PARENT_SCOPE)
    endif()
  elseif("${CMAKE_GENERATOR}" MATCHES "Green Hills MULTI")
    set(id_dir ${CMAKE_${lang}_COMPILER_ID_DIR})
    set(id_src "${src}")
    if (GHS_PRIMARY_TARGET)
      set(ghs_primary_target "${GHS_PRIMARY_TARGET}")
    else()
      set(ghs_primary_target "${CMAKE_GENERATOR_PLATFORM}_${GHS_TARGET_PLATFORM}.tgt")
    endif()
    if ("${GHS_TARGET_PLATFORM}" MATCHES "integrity")
        set(bsp_name "macro GHS_BSP=${GHS_BSP_NAME}")
        set(os_dir "macro GHS_OS=${GHS_OS_DIR}")
    endif()
    set(command "${CMAKE_MAKE_PROGRAM}" "-commands" "-top" "GHS_default.gpj")
    configure_file(${CMAKE_ROOT}/Modules/CompilerId/GHS_default.gpj.in
      ${id_dir}/GHS_default.gpj @ONLY)
    configure_file(${CMAKE_ROOT}/Modules/CompilerId/GHS_lib.gpj.in
      ${id_dir}/GHS_lib.gpj @ONLY)
    execute_process(COMMAND ${command}
      WORKING_DIRECTORY ${id_dir}
      OUTPUT_VARIABLE CMAKE_${lang}_COMPILER_ID_OUTPUT
      ERROR_VARIABLE CMAKE_${lang}_COMPILER_ID_OUTPUT
      RESULT_VARIABLE CMAKE_${lang}_COMPILER_ID_RESULT
      )
    # Match the compiler location line printed out.
    set(ghs_toolpath "${CMAKE_MAKE_PROGRAM}")
    if(CMAKE_HOST_UNIX)
      string(REPLACE "/gbuild" "/" ghs_toolpath ${ghs_toolpath})
    else()
      string(REPLACE "/gbuild.exe" "/" ghs_toolpath ${ghs_toolpath})
      string(REPLACE / "\\\\" ghs_toolpath ${ghs_toolpath})
    endif()
    if("${CMAKE_${lang}_COMPILER_ID_OUTPUT}" MATCHES "(${ghs_toolpath}[^ ]*)")
      if(CMAKE_HOST_UNIX)
        set(_comp "${CMAKE_MATCH_1}")
      else()
        set(_comp "${CMAKE_MATCH_1}.exe")
      endif()
      if(EXISTS "${_comp}")
        file(TO_CMAKE_PATH "${_comp}" _comp)
        set(CMAKE_${lang}_COMPILER_ID_TOOL "${_comp}" PARENT_SCOPE)
      endif()
    endif()
  else()
    execute_process(
      COMMAND "${CMAKE_${lang}_COMPILER}"
              ${CMAKE_${lang}_COMPILER_ID_ARG1}
              ${userflags}
              ${testflags}
              ${CMAKE_${lang}_COMPILER_ID_FLAGS_ALWAYS}
              "${src}"
      WORKING_DIRECTORY ${CMAKE_${lang}_COMPILER_ID_DIR}
      OUTPUT_VARIABLE CMAKE_${lang}_COMPILER_ID_OUTPUT
      ERROR_VARIABLE CMAKE_${lang}_COMPILER_ID_OUTPUT
      RESULT_VARIABLE CMAKE_${lang}_COMPILER_ID_RESULT
      )
    if("${CMAKE_${lang}_COMPILER_ID_OUTPUT}" MATCHES "exec: [^\n]*\\((/[^,\n]*/cpp),CMakeFortranCompilerId.F")
      set(_cpp "${CMAKE_MATCH_1}")
      if(EXISTS "${_cpp}")
        set(CMAKE_${lang}_COMPILER_ID_CPP "${_cpp}" PARENT_SCOPE)
      endif()
    endif()
  endif()

  # Check the result of compilation.
  if(CMAKE_${lang}_COMPILER_ID_RESULT
     # Intel Fortran warns and ignores preprocessor lines without /fpp
     OR CMAKE_${lang}_COMPILER_ID_OUTPUT MATCHES "Bad # preprocessor line"
     )
    # Compilation failed.
    set(MSG
      "Compiling the ${lang} compiler identification source file \"${src}\" failed.
${COMPILER_DESCRIPTION}
The output was:
${CMAKE_${lang}_COMPILER_ID_RESULT}
${CMAKE_${lang}_COMPILER_ID_OUTPUT}

")
    file(APPEND ${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CMakeError.log "${MSG}")

    # Some languages may know the correct/desired set of flags and want to fail right away if they don't work.
    # This is currently only used by CUDA.
    if(CMAKE_${lang}_COMPILER_ID_REQUIRE_SUCCESS)
      message(FATAL_ERROR "${MSG}")
    endif()

    # No output files should be inspected.
    set(COMPILER_${lang}_PRODUCED_FILES)
    set(COMPILER_${lang}_PRODUCED_OUTPUT)
  else()
    # Compilation succeeded.
    file(APPEND ${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CMakeOutput.log
      "Compiling the ${lang} compiler identification source file \"${src}\" succeeded.
${COMPILER_DESCRIPTION}
The output was:
${CMAKE_${lang}_COMPILER_ID_RESULT}
${CMAKE_${lang}_COMPILER_ID_OUTPUT}

")

    # Find the executable produced by the compiler, try all files in the
    # binary dir.
    string(REGEX REPLACE "([][])" "[\\1]" _glob_id_dir "${CMAKE_${lang}_COMPILER_ID_DIR}")
    file(GLOB files
      RELATIVE ${CMAKE_${lang}_COMPILER_ID_DIR}

      # normal case
      ${_glob_id_dir}/*

      # com.apple.package-type.bundle.unit-test
      ${_glob_id_dir}/*.xctest/*

      # com.apple.product-type.framework
      ${_glob_id_dir}/*.framework/*
      )
    list(REMOVE_ITEM files "${src}")
    set(COMPILER_${lang}_PRODUCED_FILES "")
    foreach(file ${files})
      if(NOT IS_DIRECTORY ${CMAKE_${lang}_COMPILER_ID_DIR}/${file})
        list(APPEND COMPILER_${lang}_PRODUCED_FILES ${file})
        file(APPEND ${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CMakeOutput.log
          "Compilation of the ${lang} compiler identification source \""
          "${src}\" produced \"${file}\"\n\n")
      endif()
    endforeach()

    if(NOT COMPILER_${lang}_PRODUCED_FILES)
      # No executable was found.
      file(APPEND ${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CMakeError.log
        "Compilation of the ${lang} compiler identification source \""
        "${src}\" did not produce an executable in \""
        "${CMAKE_${lang}_COMPILER_ID_DIR}\".\n\n")
    endif()

    set(COMPILER_${lang}_PRODUCED_OUTPUT "${CMAKE_${lang}_COMPILER_ID_OUTPUT}")
  endif()

  # Return the files produced by the compilation.
  set(COMPILER_${lang}_PRODUCED_FILES "${COMPILER_${lang}_PRODUCED_FILES}" PARENT_SCOPE)
  set(COMPILER_${lang}_PRODUCED_OUTPUT "${COMPILER_${lang}_PRODUCED_OUTPUT}" PARENT_SCOPE)

endfunction()

#-----------------------------------------------------------------------------
# Function to extract the compiler id from compiler output.
function(CMAKE_DETERMINE_COMPILER_ID_MATCH_VENDOR lang output)
  foreach(vendor ${CMAKE_${lang}_COMPILER_ID_MATCH_VENDORS})
    if(output MATCHES "${CMAKE_${lang}_COMPILER_ID_MATCH_VENDOR_REGEX_${vendor}}")
      set(CMAKE_${lang}_COMPILER_ID "${vendor}")
    endif()
  endforeach()
  set(CMAKE_${lang}_COMPILER_ID "${CMAKE_${lang}_COMPILER_ID}" PARENT_SCOPE)
endfunction()

#-----------------------------------------------------------------------------
# Function to extract the compiler id from an executable.
function(CMAKE_DETERMINE_COMPILER_ID_CHECK lang file)
  # Look for a compiler id if not yet known.
  if(NOT CMAKE_${lang}_COMPILER_ID)
    # Read the compiler identification string from the executable file.
    set(COMPILER_ID)
    set(COMPILER_VERSION)
    set(COMPILER_VERSION_MAJOR 0)
    set(COMPILER_VERSION_MINOR 0)
    set(COMPILER_VERSION_PATCH 0)
    set(COMPILER_VERSION_TWEAK 0)
    set(COMPILER_VERSION_INTERNAL "")
    set(HAVE_COMPILER_VERSION_MAJOR 0)
    set(HAVE_COMPILER_VERSION_MINOR 0)
    set(HAVE_COMPILER_VERSION_PATCH 0)
    set(HAVE_COMPILER_VERSION_TWEAK 0)
    set(COMPILER_WRAPPER)
    set(DIGIT_VALUE_1 1)
    set(DIGIT_VALUE_2 10)
    set(DIGIT_VALUE_3 100)
    set(DIGIT_VALUE_4 1000)
    set(DIGIT_VALUE_5 10000)
    set(DIGIT_VALUE_6 100000)
    set(DIGIT_VALUE_7 1000000)
    set(DIGIT_VALUE_8 10000000)
    set(PLATFORM_ID)
    set(ARCHITECTURE_ID)
    set(SIMULATE_ID)
    set(SIMULATE_VERSION)
    foreach(encoding "" "ENCODING;UTF-16LE" "ENCODING;UTF-16BE")
      file(STRINGS "${file}" CMAKE_${lang}_COMPILER_ID_STRINGS
        LIMIT_COUNT 38 ${encoding}
        REGEX ".?I.?N.?F.?O.?:.?[A-Za-z0-9_]+\\[[^]]*\\]")
      if(NOT CMAKE_${lang}_COMPILER_ID_STRINGS STREQUAL "")
        break()
      endif()
    endforeach()

    # With the IAR Compiler, some strings are found twice, first time as incomplete
    # list like "?<Constant "INFO:compiler[IAR]">".  Remove the incomplete copies.
    list(FILTER CMAKE_${lang}_COMPILER_ID_STRINGS EXCLUDE REGEX "\\?<Constant \\\"")

    # The IAR-AVR compiler uses a binary format that places a '6'
    # character (0x34) before each character in the string.  Strip
    # out these characters without removing any legitimate characters.
    if(CMAKE_${lang}_COMPILER_ID_STRINGS MATCHES "(.)I.N.F.O.:.")
      string(REGEX REPLACE "${CMAKE_MATCH_1}([^;])" "\\1"
        CMAKE_${lang}_COMPILER_ID_STRINGS "${CMAKE_${lang}_COMPILER_ID_STRINGS}")
    endif()

    # Remove arbitrary text that may appear before or after each INFO string.
    string(REGEX MATCHALL "INFO:[A-Za-z0-9_]+\\[([^]\"]*)\\]"
      CMAKE_${lang}_COMPILER_ID_STRINGS "${CMAKE_${lang}_COMPILER_ID_STRINGS}")

    # In C# binaries, some strings are found more than once.
    list(REMOVE_DUPLICATES CMAKE_${lang}_COMPILER_ID_STRINGS)

    set(COMPILER_ID_TWICE)
    foreach(info ${CMAKE_${lang}_COMPILER_ID_STRINGS})
      if("${info}" MATCHES "INFO:compiler\\[([^]\"]*)\\]")
        if(COMPILER_ID)
          set(COMPILER_ID_TWICE 1)
        endif()
        set(COMPILER_ID "${CMAKE_MATCH_1}")
      endif()
      if("${info}" MATCHES "INFO:platform\\[([^]\"]*)\\]")
        set(PLATFORM_ID "${CMAKE_MATCH_1}")
      endif()
      if("${info}" MATCHES "INFO:arch\\[([^]\"]*)\\]")
        set(ARCHITECTURE_ID "${CMAKE_MATCH_1}")
      endif()
      if("${info}" MATCHES "INFO:compiler_version\\[([^]\"]*)\\]")
        string(REGEX REPLACE "^0+([0-9]+)" "\\1" COMPILER_VERSION "${CMAKE_MATCH_1}")
        string(REGEX REPLACE "\\.0+([0-9])" ".\\1" COMPILER_VERSION "${COMPILER_VERSION}")
      endif()
      if("${info}" MATCHES "INFO:compiler_version_internal\\[([^]\"]*)\\]")
        string(REGEX REPLACE "^0+([0-9])" "\\1" COMPILER_VERSION_INTERNAL "${CMAKE_MATCH_1}")
        string(REGEX REPLACE "\\.0+([0-9])" ".\\1" COMPILER_VERSION_INTERNAL "${COMPILER_VERSION_INTERNAL}")
      endif()
      foreach(comp MAJOR MINOR PATCH TWEAK)
        foreach(digit 1 2 3 4 5 6 7 8 9)
          if("${info}" MATCHES "INFO:compiler_version_${comp}_digit_${digit}\\[([0-9])\\]")
            set(value ${CMAKE_MATCH_1})
            math(EXPR COMPILER_VERSION_${comp} "${COMPILER_VERSION_${comp}} + ${value} * ${DIGIT_VALUE_${digit}}")
            set(HAVE_COMPILER_VERSION_${comp} 1)
          endif()
        endforeach()
      endforeach()
      if("${info}" MATCHES "INFO:compiler_wrapper\\[([^]\"]*)\\]")
        set(COMPILER_WRAPPER "${CMAKE_MATCH_1}")
      endif()
      if("${info}" MATCHES "INFO:simulate\\[([^]\"]*)\\]")
        set(SIMULATE_ID "${CMAKE_MATCH_1}")
      endif()
      if("${info}" MATCHES "INFO:simulate_version\\[([^]\"]*)\\]")
        string(REGEX REPLACE "^0+([0-9])" "\\1" SIMULATE_VERSION "${CMAKE_MATCH_1}")
        string(REGEX REPLACE "\\.0+([0-9])" ".\\1" SIMULATE_VERSION "${SIMULATE_VERSION}")
      endif()
      if("${info}" MATCHES "INFO:qnxnto\\[\\]")
        set(COMPILER_QNXNTO 1)
      endif()
      if("${info}" MATCHES "INFO:dialect_default\\[([^]\"]*)\\]")
        set(CMAKE_${lang}_STANDARD_COMPUTED_DEFAULT "${CMAKE_MATCH_1}")
      endif()
    endforeach()

    # Construct compiler version from components if needed.
    if(NOT DEFINED COMPILER_VERSION AND HAVE_COMPILER_VERSION_MAJOR)
      set(COMPILER_VERSION "${COMPILER_VERSION_MAJOR}")
      if(HAVE_COMPILER_VERSION_MINOR)
        string(APPEND COMPILER_VERSION ".${COMPILER_VERSION_MINOR}")
        if(HAVE_COMPILER_VERSION_PATCH)
          string(APPEND COMPILER_VERSION ".${COMPILER_VERSION_PATCH}")
          if(HAVE_COMPILER_VERSION_TWEAK)
            string(APPEND COMPILER_VERSION ".${COMPILER_VERSION_TWEAK}")
          endif()
        endif()
      endif()
    endif()

    # Detect the exact architecture from the PE header.
    if(WIN32)
      # The offset to the PE signature is stored at 0x3c.
      file(READ ${file} peoffsethex LIMIT 1 OFFSET 60 HEX)
      if(NOT peoffsethex STREQUAL "")
        string(SUBSTRING "${peoffsethex}" 0 1 peoffsethex1)
        string(SUBSTRING "${peoffsethex}" 1 1 peoffsethex2)
        set(peoffsetexpression "${peoffsethex1} * 16 + ${peoffsethex2}")
        string(REPLACE "a" "10" peoffsetexpression "${peoffsetexpression}")
        string(REPLACE "b" "11" peoffsetexpression "${peoffsetexpression}")
        string(REPLACE "c" "12" peoffsetexpression "${peoffsetexpression}")
        string(REPLACE "d" "13" peoffsetexpression "${peoffsetexpression}")
        string(REPLACE "e" "14" peoffsetexpression "${peoffsetexpression}")
        string(REPLACE "f" "15" peoffsetexpression "${peoffsetexpression}")
        math(EXPR peoffset "${peoffsetexpression}")

        file(READ ${file} peheader LIMIT 6 OFFSET ${peoffset} HEX)
        if(peheader STREQUAL "50450000a201")
          set(ARCHITECTURE_ID "SH3")
        elseif(peheader STREQUAL "50450000a301")
          set(ARCHITECTURE_ID "SH3DSP")
        elseif(peheader STREQUAL "50450000a601")
          set(ARCHITECTURE_ID "SH4")
        elseif(peheader STREQUAL "50450000a801")
          set(ARCHITECTURE_ID "SH5")
        endif()
      endif()
    endif()

    # Check if a valid compiler and platform were found.
    if(COMPILER_ID AND NOT COMPILER_ID_TWICE)
      set(CMAKE_${lang}_COMPILER_ID "${COMPILER_ID}")
      set(CMAKE_${lang}_PLATFORM_ID "${PLATFORM_ID}")
      set(CMAKE_${lang}_COMPILER_ARCHITECTURE_ID "${ARCHITECTURE_ID}")
      set(MSVC_${lang}_ARCHITECTURE_ID "${ARCHITECTURE_ID}")
      set(CMAKE_${lang}_COMPILER_VERSION "${COMPILER_VERSION}")
      set(CMAKE_${lang}_COMPILER_VERSION_INTERNAL "${COMPILER_VERSION_INTERNAL}")
      set(CMAKE_${lang}_SIMULATE_ID "${SIMULATE_ID}")
      set(CMAKE_${lang}_SIMULATE_VERSION "${SIMULATE_VERSION}")
    endif()

    # Check the compiler identification string.
    if(CMAKE_${lang}_COMPILER_ID)
      # The compiler identification was found.
      file(APPEND ${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CMakeOutput.log
        "The ${lang} compiler identification is ${CMAKE_${lang}_COMPILER_ID}, found in \""
        "${file}\"\n\n")
    else()
      # The compiler identification could not be found.
      file(APPEND ${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CMakeError.log
        "The ${lang} compiler identification could not be found in \""
        "${file}\"\n\n")
    endif()
  endif()

  # try to figure out the executable format: ELF, COFF, Mach-O
  if(NOT CMAKE_EXECUTABLE_FORMAT)
    file(READ ${file} CMAKE_EXECUTABLE_MAGIC LIMIT 4 HEX)

    # ELF files start with 0x7f"ELF"
    if("${CMAKE_EXECUTABLE_MAGIC}" STREQUAL "7f454c46")
      set(CMAKE_EXECUTABLE_FORMAT "ELF" CACHE INTERNAL "Executable file format")
    endif()

#    # COFF (.exe) files start with "MZ"
#    if("${CMAKE_EXECUTABLE_MAGIC}" MATCHES "4d5a....")
#      set(CMAKE_EXECUTABLE_FORMAT "COFF" CACHE STRING "Executable file format")
#    endif()
#
    # Mach-O files start with MH_MAGIC or MH_CIGAM
    if("${CMAKE_EXECUTABLE_MAGIC}" MATCHES "feedface|cefaedfe|feedfacf|cffaedfe")
      set(CMAKE_EXECUTABLE_FORMAT "MACHO" CACHE STRING "Executable file format")
    endif()

  endif()
  if(NOT DEFINED CMAKE_EXECUTABLE_FORMAT)
    set(CMAKE_EXECUTABLE_FORMAT)
  endif()
  # Return the information extracted.
  set(CMAKE_${lang}_COMPILER_ID "${CMAKE_${lang}_COMPILER_ID}" PARENT_SCOPE)
  set(CMAKE_${lang}_PLATFORM_ID "${CMAKE_${lang}_PLATFORM_ID}" PARENT_SCOPE)
  set(CMAKE_${lang}_COMPILER_ARCHITECTURE_ID "${CMAKE_${lang}_COMPILER_ARCHITECTURE_ID}" PARENT_SCOPE)
  set(MSVC_${lang}_ARCHITECTURE_ID "${MSVC_${lang}_ARCHITECTURE_ID}"
    PARENT_SCOPE)
  set(CMAKE_${lang}_COMPILER_VERSION "${CMAKE_${lang}_COMPILER_VERSION}" PARENT_SCOPE)
  set(CMAKE_${lang}_COMPILER_VERSION_INTERNAL "${CMAKE_${lang}_COMPILER_VERSION_INTERNAL}" PARENT_SCOPE)
  set(CMAKE_${lang}_COMPILER_WRAPPER "${COMPILER_WRAPPER}" PARENT_SCOPE)
  set(CMAKE_${lang}_SIMULATE_ID "${CMAKE_${lang}_SIMULATE_ID}" PARENT_SCOPE)
  set(CMAKE_${lang}_SIMULATE_VERSION "${CMAKE_${lang}_SIMULATE_VERSION}" PARENT_SCOPE)
  set(CMAKE_EXECUTABLE_FORMAT "${CMAKE_EXECUTABLE_FORMAT}" PARENT_SCOPE)
  set(COMPILER_QNXNTO "${COMPILER_QNXNTO}" PARENT_SCOPE)
  set(CMAKE_${lang}_STANDARD_COMPUTED_DEFAULT "${CMAKE_${lang}_STANDARD_COMPUTED_DEFAULT}" PARENT_SCOPE)
endfunction()

#-----------------------------------------------------------------------------
# Function to query the compiler vendor.
# This uses a table with entries of the form
#   list(APPEND CMAKE_${lang}_COMPILER_ID_VENDORS ${vendor})
#   set(CMAKE_${lang}_COMPILER_ID_VENDOR_FLAGS_${vendor} -some-vendor-flag)
#   set(CMAKE_${lang}_COMPILER_ID_VENDOR_REGEX_${vendor} "Some Vendor Output")
# We try running the compiler with the flag for each vendor and
# matching its regular expression in the output.
function(CMAKE_DETERMINE_COMPILER_ID_VENDOR lang userflags)

  if(NOT CMAKE_${lang}_COMPILER_ID_DIR)
    # We get here when this function is called not from within CMAKE_DETERMINE_COMPILER_ID()
    # This is done e.g. for detecting the compiler ID for assemblers.
    # Compute the directory in which to run the test and Create a clean working directory.
    set(CMAKE_${lang}_COMPILER_ID_DIR ${CMAKE_PLATFORM_INFO_DIR}/CompilerId${lang})
    file(REMOVE_RECURSE ${CMAKE_${lang}_COMPILER_ID_DIR})
    file(MAKE_DIRECTORY ${CMAKE_${lang}_COMPILER_ID_DIR})
  endif()

  # Save the current LC_ALL, LC_MESSAGES, and LANG environment variables
  # and set them to "C" so we get the expected output to match.
  set(_orig_lc_all      $ENV{LC_ALL})
  set(_orig_lc_messages $ENV{LC_MESSAGES})
  set(_orig_lang        $ENV{LANG})
  set(ENV{LC_ALL}      C)
  set(ENV{LC_MESSAGES} C)
  set(ENV{LANG}        C)

  foreach(vendor ${CMAKE_${lang}_COMPILER_ID_VENDORS})
    set(flags ${CMAKE_${lang}_COMPILER_ID_VENDOR_FLAGS_${vendor}})
    set(regex ${CMAKE_${lang}_COMPILER_ID_VENDOR_REGEX_${vendor}})
    execute_process(
      COMMAND "${CMAKE_${lang}_COMPILER}"
      ${CMAKE_${lang}_COMPILER_ID_ARG1}
      ${userflags}
      ${flags}
      WORKING_DIRECTORY ${CMAKE_${lang}_COMPILER_ID_DIR}
      OUTPUT_VARIABLE output ERROR_VARIABLE output
      RESULT_VARIABLE result
      TIMEOUT 10
      )

    if("${output}" MATCHES "${regex}")
      file(APPEND ${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CMakeOutput.log
        "Checking whether the ${lang} compiler is ${vendor} using \"${flags}\" "
        "matched \"${regex}\":\n${output}")
      set(CMAKE_${lang}_COMPILER_ID "${vendor}" PARENT_SCOPE)
      set(CMAKE_${lang}_COMPILER_ID_OUTPUT "${output}" PARENT_SCOPE)
      set(CMAKE_${lang}_COMPILER_ID_VENDOR_MATCH "${CMAKE_MATCH_1}" PARENT_SCOPE)
      break()
    else()
      if("${result}" MATCHES  "timeout")
        file(APPEND ${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CMakeError.log
          "Checking whether the ${lang} compiler is ${vendor} using \"${flags}\" "
          "terminated after 10 s due to timeout.")
      else()
        file(APPEND ${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CMakeError.log
          "Checking whether the ${lang} compiler is ${vendor} using \"${flags}\" "
          "did not match \"${regex}\":\n${output}")
       endif()
    endif()
  endforeach()

  # Restore original LC_ALL, LC_MESSAGES, and LANG
  set(ENV{LC_ALL}      ${_orig_lc_all})
  set(ENV{LC_MESSAGES} ${_orig_lc_messages})
  set(ENV{LANG}        ${_orig_lang})
endfunction()

function(CMAKE_DETERMINE_MSVC_SHOWINCLUDES_PREFIX lang userflags)
  # Run this MSVC-compatible compiler to detect what the /showIncludes
  # option displays.  We can use a C source even with the C++ compiler
  # because MSVC-compatible compilers handle both and show the same output.
  set(showdir ${CMAKE_BINARY_DIR}/CMakeFiles/ShowIncludes)
  file(WRITE ${showdir}/foo.h "\n")
  file(WRITE ${showdir}/main.c "#include \"foo.h\" \nint main(){}\n")
  execute_process(
    COMMAND "${CMAKE_${lang}_COMPILER}"
            ${CMAKE_${lang}_COMPILER_ID_ARG1}
            ${userflags}
            /nologo /showIncludes /c main.c
    WORKING_DIRECTORY ${showdir}
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err
    RESULT_VARIABLE res
    ENCODING AUTO # cl prints in current code page
    )
  if(res EQUAL 0 AND "${out}" MATCHES "(^|\n)([^:\n]*:[^:\n]*:[ \t]*)")
    set(CMAKE_${lang}_CL_SHOWINCLUDES_PREFIX "${CMAKE_MATCH_2}" PARENT_SCOPE)
  else()
    set(CMAKE_${lang}_CL_SHOWINCLUDES_PREFIX "" PARENT_SCOPE)
  endif()
endfunction()
