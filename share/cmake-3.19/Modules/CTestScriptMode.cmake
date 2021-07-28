# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
CTestScriptMode
---------------



This file is read by ctest in script mode (-S)
#]=======================================================================]

# Determine the current system, so this information can be used
# in ctest scripts
include(CMakeDetermineSystem)

# Also load the system specific file, which sets up e.g. the search paths.
# This makes the FIND_XXX() calls work much better
include(CMakeSystemSpecificInformation)

