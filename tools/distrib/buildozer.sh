#! /bin/bash
# Copyright 2019 The gRPC Authors
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

set -e

BUILDOZER_VERSION="4.2.2"
TEMP_BUILDOZER_PATH="/tmp/buildozer-for-grpc"

function error_handling() {
    error=$1
    if [[ -x "$error" ]]; then
        echo "${error}"
        exit 1
    fi
}

function download_buildozer() {
    platform="$(uname -s)"
    case "${platform}" in
        Linux*)     download_link="https://github.com/bazelbuild/buildtools/releases/download/${BUILDOZER_VERSION}/buildozer-linux-amd64";;
        Darwin*)    download_link="https://github.com/bazelbuild/buildtools/releases/download/${BUILDOZER_VERSION}/buildozer-darwin-amd64";;
        *)          error_handling "Unsupported platform: ${platform}";;
    esac

    if [ -x "$(command -v curl)" ]; then
        curl -L -o ${TEMP_BUILDOZER_PATH} ${download_link}
    elif [ -x "$(command -v wget)" ]; then
        wget -O ${TEMP_BUILDOZER_PATH} ${download_link}
    else
        error_handling "Download failed: curl and wget not available"
    fi

    chmod +x ${TEMP_BUILDOZER_PATH}
}


# Get the correct version of buildozer
if [ -x "$(command -v buildozer)" ]; then
    existing_buildozer_version="$(buildozer -version 2>&1 | head -n1 | cut -d" " -f3)"
    if [[ "${existing_buildozer_version}" != "${BUILDOZER_VERSION}" ]]; then
        download_buildozer
        buildozer_bin="${TEMP_BUILDOZER_PATH}"
    else
        buildozer_bin="buildozer"
    fi
else
    if [ -x ${TEMP_BUILDOZER_PATH} ]; then
        existing_buildozer_version="$(${TEMP_BUILDOZER_PATH} -version 2>&1 | head -n1 | cut -d" " -f3)"
        if [[ "${existing_buildozer_version}" != "${BUILDOZER_VERSION}" ]]; then
            download_buildozer
        fi
    else
        download_buildozer
    fi
    buildozer_bin="${TEMP_BUILDOZER_PATH}"
fi

# cd to repo root
dir=$(dirname "${0}")
cd "${dir}/../.."

set -ex

# shellcheck disable=SC2086,SC2068
${buildozer_bin} "$@"
