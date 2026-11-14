# Copyright 2026 gRPC authors.
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

set(CEL_C_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/cel-c)
set(_gRPC_CEL_C_INCLUDE_DIR "${CEL_C_ROOT_DIR}")

# Same source fix as third_party/cel-c.patch for building against the protobuf
# (upb) version used by gRPC, where _upb_Message_UnknownFieldsAreEqual is
# wrapped in UPB_PRIVATE(). Because UPB_PRIVATE(x) token-pastes x directly,
# macro-defining the unprefixed name does not affect the header declaration in
# upb/message/internal/compare_unknown.h.
set_source_files_properties(
  "${CEL_C_ROOT_DIR}/cel-c/internal/message_equality.cc"
  PROPERTIES COMPILE_DEFINITIONS
  "_upb_Message_UnknownFieldsAreEqual=_upb_Message_UnknownFieldsAreEqual_dont_copy_me__upb_internal_use_only"
)
