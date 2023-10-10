#!/bin/bash
# Copyright 2021 The gRPC Authors
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

# ARGUMENTS
# $1 - python version in "3.X.Y" format
# $2 - python tags (as in manylinx images) e.g. "/opt/python/cp37-cp37m"
PYTHON_VERSION="$1"
PYTHON_RELEASE="$2"
PYTHON_PREFIX="$3"

curl -O "https://www.python.org/ftp/python/${PYTHON_VERSION}/Python-${PYTHON_RELEASE}.tar.xz"
tar -xf "Python-${PYTHON_RELEASE}.tar.xz"
pushd "Python-${PYTHON_RELEASE}"

# In this step, we are building python that can run on the architecture where the build runs (x64).
# Since the CC, CXX and other env vars are set by default to point to the crosscompilation toolchain,
# we explicitly unset them to end up with x64 python binaries.
(unset AS AR CC CPP CXX LD && ./configure --prefix="${PYTHON_PREFIX}" && make -j 4 && make install)

# When building the crosscompiled native extension, python will automatically add its include directory
# that contains "Python.h" and other headers. But since we are going to be crosscompiling
# the wheels, we need the pyconfig.h that corresponds to the target architecture.
# Since pyconfig.h is the only generated header and python itself (once built) doesn't
# really need this header anymore, we simply generate a new pyconfig.h using our crosscompilation
# toolchain and overwrite the current ("wrong") version in the python's include directory.
./configure && cp pyconfig.h "${PYTHON_PREFIX}"/include/python*

popd
# remove the build directory to decrease the overall docker image size
rm -rf "Python-${PYTHON_VERSION}"

# install cython and wheel
"${PYTHON_PREFIX}/bin/python3" -m pip install --upgrade 'cython<3.0.0rc1' wheel
