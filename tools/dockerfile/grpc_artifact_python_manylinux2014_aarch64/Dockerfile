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

FROM dockcross/manylinux2014-aarch64:20210826-19322ba

# manylinux_2_17 is the preferred alias of manylinux2014
ENV AUDITWHEEL_PLAT manylinux_2_17_$AUDITWHEEL_ARCH

###################################
# Install Python build requirements
RUN /opt/python/cp36-cp36m/bin/pip install --upgrade cython
RUN /opt/python/cp37-cp37m/bin/pip install --upgrade cython
RUN /opt/python/cp38-cp38/bin/pip install --upgrade cython
RUN /opt/python/cp39-cp39/bin/pip install --upgrade cython
RUN /opt/python/cp310-cp310/bin/pip install --upgrade cython

# the dockcross docker image sets variables like CC, CXX etc.
# to point to the crosscompilation toolchain, but doesn't set corresponding
# variables for the "strip" and "objcopy" tools.
# see https://github.com/dockcross/dockcross/blob/4349cb4999401cbf22a90f46f5052d29be240e50/manylinux2014-aarch64/Dockerfile.in#L23
ENV STRIP=${CROSS_ROOT}/bin/${CROSS_TRIPLE}-strip \
    OBJCOPY=${CROSS_ROOT}/bin/${CROSS_TRIPLE}-objcopy
