# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.


find_program(CMAKE_MAKE_PROGRAM make
  PATHS
  "[HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\MSYS-1.0_is1;Inno Setup: App Path]/bin"
  "[HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\MinGW;InstallLocation]/bin"
  c:/msys/1.0/bin /msys/1.0/bin)
mark_as_advanced(CMAKE_MAKE_PROGRAM)
