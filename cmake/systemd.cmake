# Copyright 2022 gRPC authors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

set(gRPC_USE_SYSTEMD "AUTO" CACHE STRING "Build with libsystemd support if available. Can be ON, OFF or AUTO")

if (NOT gRPC_USE_SYSTEMD STREQUAL "OFF")
  if (gRPC_USE_SYSTEMD STREQUAL "ON")
    find_package(systemd REQUIRED)
  elseif (gRPC_USE_SYSTEMD STREQUAL "AUTO")
    find_package(systemd)
  else()
    message(FATAL_ERROR "Unknown value for gRPC_USE_SYSTEMD = ${gRPC_USE_SYSTEMD}")
  endif()

  if(TARGET systemd)
    set(_gRPC_SYSTEMD_LIBRARIES systemd ${SYSTEMD_LINK_LIBRARIES})
    add_definitions(-DHAVE_LIBSYSTEMD)
  endif()
  set(_gRPC_FIND_SYSTEMD "if(NOT systemd_FOUND)\n  find_package(systemd)\nendif()")
endif()
