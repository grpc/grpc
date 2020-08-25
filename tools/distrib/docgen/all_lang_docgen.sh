#! /bin/bash
# Copyright 2020 The gRPC Authors
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
#
# A script to automatically generate API references and push to GitHub.
# This script covers Core/C++/ObjC/C#/PHP/Python. Due to lack of tooling for
# Cython, Python document generation unfortunately needs to compile everything.
# So, this script will take couple minutes to run.
#
# Generate and push:
#
#     tools/distrib/docgen/all_lang-docgen.sh YOUR_GITHUB_USERNAME
#
# Just generate:
#
#     tools/distrib/docgen/all_lang-docgen.sh
#

set -e

# Find out the gRPC version and print it
GRPC_VERSION="$(grep -m1 -Eo ' version: .*' build_handwritten.yaml | grep -Eo '[0-9][^ ]*')"
echo "Generating documents for version ${GRPC_VERSION}..."

# Specifies your GitHub user name or generates documents locally
if [ $# -eq 0 ]; then
    read -r -p "- Are you sure to generate documents without pushing to GitHub? [y/N] " response
    if [[ "${response[0]}" =~ ^([yY][eE][sS]|[yY])$ ]]; then
        GITHUB_USER=''
    else
        echo "Generation stopped"
        exit 1
    fi
else
    if [ $# -eq 1 ]; then
        GITHUB_USER=$1
    else
        echo "Too many arguments!"
        exit 1
    fi
fi

# Exits on pending changes; please double check for unwanted code changes
git diff --exit-code
git submodule update --init --recursive

# Changes to project root
dir=$(dirname "${0}")
cd "${dir}/../../.."

# Clones the API reference GitHub Pages branch
PAGES_PATH="/tmp/gh-pages"
rm -rf "${PAGES_PATH}"
git clone --single-branch https://github.com/grpc/grpc -b gh-pages "${PAGES_PATH}"

# Generates Core / C++ / ObjC / PHP documents
rm -rf "${PAGES_PATH}/core" "${PAGES_PATH}/cpp" "${PAGES_PATH}/objc" "${PAGES_PATH}/php"
echo "Generating Core / C++ / ObjC / PHP documents in Docker..."
docker run --rm -it \
    -v "$(pwd)":/work/grpc \
    --user "$(id -u):$(id -g)" \
    hrektts/doxygen /work/grpc/tools/doxygen/run_doxygen.sh
mv doc/ref/c++/html "${PAGES_PATH}/cpp"
mv doc/ref/core/html "${PAGES_PATH}/core"
mv doc/ref/objc/html "${PAGES_PATH}/objc"
mv doc/ref/php/html "${PAGES_PATH}/php"

# Generates C# documents
rm -rf "${PAGES_PATH}/csharp"
echo "Generating C# documents in Docker..."
docker run --rm -it \
    -v "$(pwd)":/work \
    -w /work/src/csharp/docfx \
    --user "$(id -u):$(id -g)" \
    tsgkadot/docker-docfx:latest docfx
mv src/csharp/docfx/html "${PAGES_PATH}/csharp"

# Generates Python documents
rm -rf "${PAGES_PATH}/python"
echo "Generating Python documents in Docker..."
docker run --rm -it \
    -v "$(pwd)":/work \
    -w /work \
    --user "$(id -u):$(id -g)" \
    python:3.8 tools/distrib/docgen/_generate_python_doc.sh
mv doc/build "${PAGES_PATH}/python"

# At this point, document generation is finished.
echo "================================================================="
echo "  Successfully generated documents for version ${GRPC_VERSION}."
echo "================================================================="

# Uploads to GitHub
if [[ -n "${GITHUB_USER}" ]]; then
    BRANCH_NAME="doc-${GRPC_VERSION}"

    (cd "${PAGES_PATH}"
        git remote add "${GITHUB_USER}" "git@github.com:${GITHUB_USER}/grpc.git"
        git checkout -b "${BRANCH_NAME}"
        git add --all
        git commit -m "Auto-update documentation for gRPC ${GRPC_VERSION}"
        git push --set-upstream "${GITHUB_USER}" "${BRANCH_NAME}"
    )

    echo "Please check https://github.com/${GITHUB_USER}/grpc/tree/${BRANCH_NAME} for generated documents."
    echo "Click https://github.com/grpc/grpc/compare/gh-pages...${GITHUB_USER}:${BRANCH_NAME} to create a PR."
else
    echo "Please check ${PAGES_PATH} for generated documents."
fi
