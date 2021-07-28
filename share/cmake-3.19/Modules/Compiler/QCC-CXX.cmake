# To include compiler feature detection
include(Compiler/GNU-CXX)

include(Compiler/QCC)
__compiler_qcc(CXX)

# If the toolchain uses qcc for CMAKE_CXX_COMPILER instead of QCC, the
# default for the driver is not c++.
set(CMAKE_CXX_COMPILE_OBJECT
  "<CMAKE_CXX_COMPILER> -lang-c++ <DEFINES> <INCLUDES> <FLAGS> -o <OBJECT> -c <SOURCE>")

set(CMAKE_CXX_LINK_EXECUTABLE
  "<CMAKE_CXX_COMPILER> -lang-c++ <FLAGS> <CMAKE_CXX_LINK_FLAGS> <LINK_FLAGS> <OBJECTS> -o <TARGET> <LINK_LIBRARIES>")

set(CMAKE_CXX_COMPILE_OPTIONS_VISIBILITY_INLINES_HIDDEN "-fvisibility-inlines-hidden")
