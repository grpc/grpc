include(Compiler/Clang)
__compiler_clang(CUDA)

# Set explicitly, because __compiler_clang() doesn't set this if we're simulating MSVC.
set(CMAKE_DEPFILE_FLAGS_CUDA "-MD -MT <OBJECT> -MF <DEPFILE>")

# C++03 isn't supported for CXX, but is for CUDA, so we need to set these manually.
# Do this before __compiler_clang_cxx_standards() since that adds the feature.
set(CMAKE_CUDA03_STANDARD_COMPILE_OPTION "-std=c++03")
set(CMAKE_CUDA03_EXTENSION_COMPILE_OPTION "-std=gnu++03")
__compiler_clang_cxx_standards(CUDA)

set(CMAKE_CUDA_COMPILER_HAS_DEVICE_LINK_PHASE TRUE)
set(_CMAKE_COMPILE_AS_CUDA_FLAG "-x cuda")
set(_CMAKE_CUDA_PTX_FLAG "--cuda-device-only -S")
set(_CMAKE_CUDA_DEVICE_CODE "-fgpu-rdc -c")

# RulePlaceholderExpander expands crosscompile variables like sysroot and target only for CMAKE_<LANG>_COMPILER. Override the default.
set(CMAKE_CUDA_LINK_EXECUTABLE "<CMAKE_CUDA_COMPILER> <LINK_FLAGS> <OBJECTS> -o <TARGET> <LINK_LIBRARIES>${__IMPLICT_LINKS}")
set(CMAKE_CUDA_CREATE_SHARED_LIBRARY "<CMAKE_CUDA_COMPILER> <CMAKE_SHARED_LIBRARY_CUDA_FLAGS> <LINK_FLAGS> <CMAKE_SHARED_LIBRARY_CREATE_CUDA_FLAGS> <SONAME_FLAG><TARGET_SONAME> -o <TARGET> <OBJECTS> <LINK_LIBRARIES>${__IMPLICT_LINKS}")

set(CMAKE_CUDA_RUNTIME_LIBRARY_DEFAULT "STATIC")
set(CMAKE_CUDA_RUNTIME_LIBRARY_LINK_OPTIONS_STATIC "cudadevrt;cudart_static")
set(CMAKE_CUDA_RUNTIME_LIBRARY_LINK_OPTIONS_SHARED "cudadevrt;cudart")
set(CMAKE_CUDA_RUNTIME_LIBRARY_LINK_OPTIONS_NONE   "")

if(UNIX)
  list(APPEND CMAKE_CUDA_RUNTIME_LIBRARY_LINK_OPTIONS_STATIC "rt" "pthread" "dl")
endif()
