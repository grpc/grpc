# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.


__BlueGeneP_set_dynamic_flags(XL C)

# -qhalt=e       = Halt on error messages (rather than just severe errors)
string(APPEND CMAKE_C_FLAGS_INIT " -qhalt=e")
