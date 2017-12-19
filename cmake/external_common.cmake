# Copyright 2017 gRPC authors.
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

# Common arguments that will be used for configuring all external projects (=dependencies).
set(_gRPC_EP_COMMON_ARGS)

# Build dependencies with same configuration as ourselves.
set(_gRPC_EP_COMMON_ARGS ${_gRPC_EP_COMMON_ARGS} -DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE})

# Enable building monolithic shared libraries that statically link all their dependencies (such as grpc_csharp_ext). 
set(_gRPC_EP_COMMON_ARGS ${_gRPC_EP_COMMON_ARGS} -DCMAKE_POSITION_INDEPENDENT_CODE:BOOL=${CMAKE_POSITION_INDEPENDENT_CODE})

# Necessary to be able to compile boringssl under VS2017
# We cannot use CMAKE_C_FLAGS and CMAKE_CXX_FLAGS as they are hard-set in boringssl's CMakeLists.txt script.
# TODO(jtattermusch): get rid of this hack once possible - newer versions of boringssl should build fine under VS2017
if(MSVC)
  foreach(flag_var
    CMAKE_C_FLAGS_DEBUG CMAKE_C_FLAGS_RELEASE
    CMAKE_C_FLAGS_MINSIZEREL CMAKE_C_FLAGS_RELWITHDEBINFO
    CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE
    CMAKE_CXX_FLAGS_MINSIZEREL CMAKE_CXX_FLAGS_RELWITHDEBINFO)

    set(${flag_var} "${${flag_var}} /wd4987 /wd4774 /wd4819 /wd4996 /wd4619")
  endforeach(flag_var)
endif()

# The gRPC_MSVC_STATIC_RUNTIME option manipulates compiler flags. Make sure the changes made are reflected
# by external projects.
# TODO(jtattermusch): remove this hack once possible
if(MSVC)
  foreach(flag_var
    CMAKE_C_FLAGS CMAKE_C_FLAGS_DEBUG CMAKE_C_FLAGS_RELEASE
    CMAKE_C_FLAGS_MINSIZEREL CMAKE_C_FLAGS_RELWITHDEBINFO
    CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE
    CMAKE_CXX_FLAGS_MINSIZEREL CMAKE_CXX_FLAGS_RELWITHDEBINFO)

    # override the flag_var for external projects
    set(_gRPC_EP_COMMON_ARGS ${_gRPC_EP_COMMON_ARGS} -D${flag_var}:STRING=${${flag_var}})
  endforeach(flag_var)
endif()
