#!/bin/bash
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

echo "============TEST LOG: ${0} ==================================="
echo "PATH: ${PATH}"
echo "GRPC_BUILD_ENABLE_CCACHE: ${GRPC_BUILD_ENABLE_CCACHE}"
echo "CCACHE_BINARY_PATH: ${CCACHE_BINARY_PATH}"
echo "CCACHE_SECONDARY_STORAGE: ${CCACHE_SECONDARY_STORAGE}"
if [ -x "$(command -v ccache)" ]
then
  ccache --show-stats || true
fi
