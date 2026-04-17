# Copyright 2026 The gRPC Authors
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

set -ex
export primary_url=$1
export fallback_url=$2
export output_file=$3
export hash=$4

try_download() {
  local url=$1
  curl -L --fail "${url}" -o "${output_file}"
  sha256sum -c <(echo "${hash} ${output_file}")
}

try_download "${primary_url}" || try_download "${fallback_url}"
