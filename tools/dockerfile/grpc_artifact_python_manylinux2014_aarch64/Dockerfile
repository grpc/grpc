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

# The aarch64 wheels are being crosscompiled to allow running the build
# on x64 machine. The dockcross/manylinux2014-aarch64 image is a x86_64
# image with crosscompilation toolchain installed.
# Use an older version of dockcross image that has gcc4.9.4 because it was built
# before https://github.com/dockcross/dockcross/pull/449
FROM dockcross/manylinux2014-aarch64:20200929-608e6ac

###################################
# Install Python build requirements
RUN /opt/python/cp35-cp35m/bin/pip install --upgrade cython
RUN /opt/python/cp36-cp36m/bin/pip install --upgrade cython
RUN /opt/python/cp37-cp37m/bin/pip install --upgrade cython
RUN /opt/python/cp38-cp38/bin/pip install --upgrade cython
RUN /opt/python/cp39-cp39/bin/pip install --upgrade cython

# the dockcross docker image sets variables like CC, CXX etc.
# to point to the crosscompilation toolchain, but doesn't set corresponding
# variables for the "strip" and "objcopy" tools.
# see https://github.com/dockcross/dockcross/blob/4349cb4999401cbf22a90f46f5052d29be240e50/manylinux2014-aarch64/Dockerfile.in#L23
ENV STRIP=${CROSS_ROOT}/bin/${CROSS_TRIPLE}-strip \
    OBJCOPY=${CROSS_ROOT}/bin/${CROSS_TRIPLE}-objcopy
