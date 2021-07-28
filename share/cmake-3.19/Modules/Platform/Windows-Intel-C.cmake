include(Platform/Windows-Intel)
__windows_compiler_intel(C)
set(CMAKE_NINJA_DEPTYPE_C intel) # special value handled by CMake
set(CMAKE_DEPFILE_FLAGS_C "-QMMD -QMT <OBJECT> -QMF <DEPFILE>")
