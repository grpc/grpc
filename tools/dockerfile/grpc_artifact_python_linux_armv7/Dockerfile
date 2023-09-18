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
# on x64 machine. The dockcross/linux-armv7 image is a x86_64
# image with crosscompilation toolchain installed
FROM dockcross/linux-armv7

RUN apt update && apt install -y build-essential zlib1g-dev libncurses5-dev libgdbm-dev \
                                 libnss3-dev libssl-dev libreadline-dev libffi-dev && apt-get clean

ADD install_python_for_wheel_crosscompilation.sh /scripts/install_python_for_wheel_crosscompilation.sh

RUN /scripts/install_python_for_wheel_crosscompilation.sh "3.6.13" "3.6.13" /opt/python/cp36-cp36m
RUN /scripts/install_python_for_wheel_crosscompilation.sh "3.7.10" "3.7.10" /opt/python/cp37-cp37m
RUN /scripts/install_python_for_wheel_crosscompilation.sh "3.8.8" "3.8.8" /opt/python/cp38-cp38
RUN /scripts/install_python_for_wheel_crosscompilation.sh "3.9.2" "3.9.2" /opt/python/cp39-cp39
RUN /scripts/install_python_for_wheel_crosscompilation.sh "3.10.0" "3.10.0rc1" /opt/python/cp310-cp310
RUN /scripts/install_python_for_wheel_crosscompilation.sh "3.11.0" "3.11.0rc1" /opt/python/cp311-cp311
RUN /scripts/install_python_for_wheel_crosscompilation.sh "3.12.0" "3.12.0rc2" /opt/python/cp312-cp312

ENV AUDITWHEEL_ARCH armv7l
ENV AUDITWHEEL_PLAT linux_armv7l

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

# The dockcross base of this image sets CC and CXX to absolute paths, which makes it impossible to redirect their invocations
# to ccache via a symlink. Use relative paths instead.
ENV CC ${CROSS_TRIPLE}-gcc
ENV CXX ${CROSS_TRIPLE}-g++

# For historical reasons, task_runner.py the script under docker container using "bash -l"
# which loads /etc/profile on startup. dockcross/linux-armv7 is based on an image where
# /etc/profile overwrites contents of PATH (for security reasons) when run as root.
# That causes the crosscompiler binaries located under /usr/xcc/armv7-unknown-linux-gnueabi/bin
# to be removed from PATH. Since in our builds we don't need the /etc/profile for anything, we can just
# truncate it.
# TODO(jtattermusch): Remove this hack when possible.
RUN echo "# file contents removed to avoid resetting PATH set by the docker image" >/etc/profile

RUN git config --global --add safe.directory /var/local/jenkins/grpc
RUN git config --global protocol.file.allow always
