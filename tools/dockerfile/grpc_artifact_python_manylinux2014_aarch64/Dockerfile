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
RUN /opt/python/cp36-cp36m/bin/pip install --upgrade 'cython<3.0.0rc1'
RUN /opt/python/cp37-cp37m/bin/pip install --upgrade 'cython<3.0.0rc1'
RUN /opt/python/cp38-cp38/bin/pip install --upgrade 'cython<3.0.0rc1'
RUN /opt/python/cp39-cp39/bin/pip install --upgrade 'cython<3.0.0rc1'
RUN /opt/python/cp310-cp310/bin/pip install --upgrade 'cython<3.0.0rc1'

#=================
# Install ccache

# Install ccache from source since ccache 3.x packaged with most linux distributions
# does not support Redis backend for caching.
RUN unset CMAKE_TOOLCHAIN_FILE && unset AS AR CC CPP CXX LD \
    && curl -sSL -o ccache.tar.gz https://github.com/ccache/ccache/releases/download/v4.5.1/ccache-4.5.1.tar.gz \
    && tar -zxf ccache.tar.gz \
    && cd ccache-4.5.1 \
    && mkdir build && cd build \
    && cmake -DCMAKE_BUILD_TYPE=Release -DZSTD_FROM_INTERNET=ON -DHIREDIS_FROM_INTERNET=ON .. \
    && make -j4 && make install \
    && cd ../.. \
    && rm -rf ccache-4.5.1 ccache.tar.gz

# the dockcross docker image sets variables like CC, CXX etc.
# to point to the crosscompilation toolchain, but doesn't set corresponding
# variables for the "strip" and "objcopy" tools.
# see https://github.com/dockcross/dockcross/blob/4349cb4999401cbf22a90f46f5052d29be240e50/manylinux2014-aarch64/Dockerfile.in#L23
ENV STRIP=${CROSS_ROOT}/bin/${CROSS_TRIPLE}-strip \
    OBJCOPY=${CROSS_ROOT}/bin/${CROSS_TRIPLE}-objcopy

# The dockcross base of this image sets CC and CXX to absolute paths, which makes it impossible to redirect their invocations
# to ccache via a symlink. Use relative paths instead.
ENV CC ${CROSS_TRIPLE}-gcc
ENV CXX ${CROSS_TRIPLE}-g++
