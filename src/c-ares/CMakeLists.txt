# c-ares cmake file for gRPC
#
# This is currently very experimental, and unsupported.
#
# Copyright 2016 gRPC authors.
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

string(TOLOWER ${CMAKE_SYSTEM_NAME} cares_system_name)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/cares)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/cares/cares)

if(${cares_system_name} MATCHES windows)
  add_definitions(-DCARES_STATICLIB=1)
  add_definitions(-DWIN32_LEAN_AND_MEAN=1)
else()
  include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/cares/config_${cares_system_name})
  add_definitions(-DHAVE_CONFIG_H=1)
  add_definitions(-D_GNU_SOURCE=1)
endif()

file(GLOB lib_sources ../../third_party/cares/cares/*.c)
add_library(cares ${lib_sources})
