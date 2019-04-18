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


# Check if the commit version of BoringSSL podspec, BoringSSL submodule, and
# the shadowed symbol list are all based on the same BoringSSL commit.
set -e

cd $(dirname $0)

boringssl_podspec_original="../../src/objective-c/BoringSSL-GRPC.podspec"
symbol_list="../../src/objective-c/grpc_shadow_boringssl_symbol_list"

# Check BoringSSL version matches
ver1=$(git submodule |grep "boringssl " | awk '{print $1}' | head -n 1)
ver2=$(cat $boringssl_podspec_original | grep ':commit =>' | sed -E 's/.*"(.*)".*/\1/g')
ver3=$(cat $symbol_list | sed -n '2 p')
[ $ver1 == $ver2 ] && [ $ver1 == $ver3 ] || { echo "BoringSSL podspec (src/objective-c/BoringSSL.podspec), BoringSSL submodule (third_party/boringssl), and BoringSSL symbol list (src/objective-c/grpc_shadow_boringssl_symbol_list) commit do not match." ; echo "BoringSSL podspec: $ver1" ; echo "BoringSSL submodule: $ver2" ; echo "BoringSSL symbol list: $ver3" ; exit 1 ; }

exit 0
