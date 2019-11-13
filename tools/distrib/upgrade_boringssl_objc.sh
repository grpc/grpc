#!/bin/bash
# Copyright 2018 gRPC authors.
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

# Generate the list of boringssl symbols that need to be shadowed based on the
# current boringssl submodule. Requires local toolchain to build boringssl.

set -e

cd "$(dirname $0)"
cd ../../third_party/boringssl

BORINGSSL_COMMIT=$(git rev-parse HEAD)

# Do the following in grpc root directory
cd ../..

docker build tools/dockerfile/grpc_objc/generate_boringssl_prefix_header -t grpc/boringssl_prefix_header
mkdir -p ./boringssl_prefix_header_out
docker run -it --rm -v $(pwd)/boringssl_prefix_header_out:/output grpc/boringssl_prefix_header $BORINGSSL_COMMIT

# increase the minor version by 1
POD_VER=$(cat templates/src/objective-c/BoringSSL-GRPC.podspec.template | grep 'version = ' | perl -pe '($_)=/([0-9]+([.][0-9]+)+)/')
POD_VER_NEW="${POD_VER%.*}.$((${POD_VER##*.}+1))"
sed -i.grpc_back -e "s/version = '$POD_VER'/version = '$POD_VER_NEW'/g" templates/src/objective-c/BoringSSL-GRPC.podspec.template
sed -i.grpc_back -e "s/dependency 'BoringSSL-GRPC', '$POD_VER'/dependency 'BoringSSL-GRPC', '$POD_VER_NEW'/g" templates/gRPC-Core.podspec.template
rm templates/src/objective-c/BoringSSL-GRPC.podspec.template.grpc_back templates/gRPC-Core.podspec.template.grpc_back

RED=$'\e[1;31m'
DEFAULT=$'\e[0m'
echo ""
echo "Upgrade is completed."
echo "${RED}TODOs:"
echo "1. The script generated prefix header at /boringssl_prefix_header_out/boringssl_prefix_symbols-$BORINGSSL_COMMIT.h. Upload it to gRPC Team Cloud Storage at location gs://grpc_boringssl_prefix_headers."
echo "2. Regenerate the project.${DEFAULT}"

exit 0
