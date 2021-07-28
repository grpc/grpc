# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.


# This file is included by cmGlobalGenerator::EnableLanguage.
# It is included before the compiler has been determined.

# The CMAKE_EFFECTIVE_SYSTEM_NAME is used to load compiler and compiler
# wrapper configuration files. By default it equals to CMAKE_SYSTEM_NAME
# but could be overridden in the ${CMAKE_SYSTEM_NAME}-Initialize files.
#
# It is useful to share the same aforementioned configuration files and
# avoids duplicating them in case of tightly related platforms.
#
# An example are the platforms supported by Xcode (macOS, iOS, tvOS,
# and watchOS). For all of those the CMAKE_EFFECTIVE_SYSTEM_NAME is
# set to Apple which results in using
# Platform/Apple-AppleClang-CXX.cmake for the Apple C++ compiler.
set(CMAKE_EFFECTIVE_SYSTEM_NAME "${CMAKE_SYSTEM_NAME}")

include(Platform/${CMAKE_SYSTEM_NAME}-Initialize OPTIONAL)

set(CMAKE_SYSTEM_SPECIFIC_INITIALIZE_LOADED 1)
