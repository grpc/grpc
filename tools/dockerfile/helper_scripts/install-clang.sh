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

_OS=$(grep '^ID=' /etc/os-release 2> /dev/null | sed -E 's/ID=(.*)/\1/')
_VERSION=0
if [[ ${_OS} == "debian" ]] ; then
    _VERSION=$(grep '^VERSION_ID=' /etc/os-release | sed -E 's/VERSION_ID="([^"]+)"/\1/')
fi

if [[ "${_VERSION}" -ge "12" ]]; then
    # Is Debian 12 or higher which comes with clang >= 14.0
    apt-get update && apt-get -y install libc++-dev clang
elif [[ "${_VERSION}" -eq "11" ]]; then
    # Debian 11 comes with clang 11 which is too old. Use backport.
    if ! grep "bullseye-backports" /etc/apt/sources.list; then
        echo "deb http://archive.debian.org/debian bullseye-backports main" >> /etc/apt/sources.list
    fi
    apt-get update && apt-get install -y libc++-dev clang -t bullseye-backports cmake
else
    printf "Unsupported OS: (%s %s)\n" ${_OS} ${_VERSION}
    exit 1
fi
