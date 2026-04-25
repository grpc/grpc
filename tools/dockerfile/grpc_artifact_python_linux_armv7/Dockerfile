# Copyright 2024 The gRPC Authors
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

# AUTO-GENERATED FROM `$REPO_ROOT/templates/tools/dockerfile/grpc_artifact_python_linux_armv7/Dockerfile.inja`!!!
FROM dockcross/linux-armv7

# Configure git early so the cache reused by other py build based on the same image.
RUN git config --global protocol.file.allow always && \
  git config --global --add safe.directory /var/local/jenkins/grpc && \
  git config --global --add safe.directory /var/local/jenkins/grpc/.git && \
  git config --global --add safe.directory '/var/local/jenkins/grpc/.git/modules/third_party/*'


RUN apt update && apt install -y build-essential zlib1g-dev libncurses5-dev libgdbm-dev \
                                 libnss3-dev libssl-dev libreadline-dev libffi-dev && apt-get clean

ADD install_python_for_wheel_crosscompilation.sh /scripts/install_python_for_wheel_crosscompilation.sh

# These are synced to the versions in our grpc_artifact_python_ images based pypa/{many,musl}linux.
# grep grpc_artifact_python_ tools/bazelify_tests/dockerimage_current_versions.bzl | grep -oP '(?<=docker://)[^"]+(?=")'
# IMG=one_of_the_images_from_the_command_above
# docker run --rm $IMG bash -c 'ls -l /opt/python/cp3{9,1}*' | sed -E 's#^[^/]+##'
RUN /scripts/install_python_for_wheel_crosscompilation.sh "3.9.23" "3.9.23" /opt/python/cp39-cp39
RUN /scripts/install_python_for_wheel_crosscompilation.sh "3.10.18" "3.10.18" /opt/python/cp310-cp310
RUN /scripts/install_python_for_wheel_crosscompilation.sh "3.11.13" "3.11.13" /opt/python/cp311-cp311
RUN /scripts/install_python_for_wheel_crosscompilation.sh "3.12.11" "3.12.11" /opt/python/cp312-cp312
RUN /scripts/install_python_for_wheel_crosscompilation.sh "3.13.5" "3.13.5" /opt/python/cp313-cp313
RUN /scripts/install_python_for_wheel_crosscompilation.sh "3.14.0" "3.14.0rc1" /opt/python/cp314-cp314

ENV AUDITWHEEL_ARCH armv7l
ENV AUDITWHEEL_PLAT linux_armv7l

#=================
# Install ccache

# Install ccache from source since ccache 3.x packaged with most linux distributions
# does not support Redis backend for caching.
RUN unset CMAKE_TOOLCHAIN_FILE && unset AS AR CC CPP CXX LD STRIP OBJCOPY \
    && curl -sSL -o ccache.tar.gz https://github.com/ccache/ccache/releases/download/v4.11.3/ccache-4.11.3.tar.gz \
    && tar -zxf ccache.tar.gz \
    && cd ccache-4.11.3 \
    && mkdir build && cd build \
    && cmake -DCMAKE_BUILD_TYPE=Release -DZSTD_FROM_INTERNET=ON -DHIREDIS_FROM_INTERNET=ON .. \
    && make -j4 && make install \
    && cd ../.. \
    && rm -rf ccache-4.11.3 ccache.tar.gz


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
