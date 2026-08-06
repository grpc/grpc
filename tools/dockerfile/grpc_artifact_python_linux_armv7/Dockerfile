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

#=================
# Install apt-get-install-retry wrapper script
RUN echo '#!/usr/bin/env bash' > /usr/local/bin/apt-get-install-retry && \
    echo '# Copyright 2026 The gRPC Authors' >> /usr/local/bin/apt-get-install-retry && \
    echo '#' >> /usr/local/bin/apt-get-install-retry && \
    echo '# Licensed under the Apache License, Version 2.0 (the "License");' >> /usr/local/bin/apt-get-install-retry && \
    echo '# you may not use this file except in compliance with the License.' >> /usr/local/bin/apt-get-install-retry && \
    echo '# You may obtain a copy of the License at' >> /usr/local/bin/apt-get-install-retry && \
    echo '#' >> /usr/local/bin/apt-get-install-retry && \
    echo '#     http://www.apache.org/licenses/LICENSE-2.0' >> /usr/local/bin/apt-get-install-retry && \
    echo '#' >> /usr/local/bin/apt-get-install-retry && \
    echo '# Unless required by applicable law or agreed to in writing, software' >> /usr/local/bin/apt-get-install-retry && \
    echo '# distributed under the License is distributed on an "AS IS" BASIS,' >> /usr/local/bin/apt-get-install-retry && \
    echo '# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.' >> /usr/local/bin/apt-get-install-retry && \
    echo '# See the License for the specific language governing permissions and' >> /usr/local/bin/apt-get-install-retry && \
    echo '# limitations under the License.' >> /usr/local/bin/apt-get-install-retry && \
    echo '' >> /usr/local/bin/apt-get-install-retry && \
    echo '' >> /usr/local/bin/apt-get-install-retry && \
    echo '# Ensure all tracking is registered' >> /usr/local/bin/apt-get-install-retry && \
    echo '' >> /usr/local/bin/apt-get-install-retry && \
    echo 'PS4="+ \$(date +[%H:%M:%S_%Z]) "' >> /usr/local/bin/apt-get-install-retry && \
    echo 'set -xeuo pipefail' >> /usr/local/bin/apt-get-install-retry && \
    echo '' >> /usr/local/bin/apt-get-install-retry && \
    echo '# Ensure package to install is provided' >> /usr/local/bin/apt-get-install-retry && \
    echo 'if [ $# -eq 0 ]; then' >> /usr/local/bin/apt-get-install-retry && \
    echo '  echo "Error: No package name provided to install with apt-get." >&2' >> /usr/local/bin/apt-get-install-retry && \
    echo '  exit 1' >> /usr/local/bin/apt-get-install-retry && \
    echo 'fi' >> /usr/local/bin/apt-get-install-retry && \
    echo '' >> /usr/local/bin/apt-get-install-retry && \
    echo 'PACKAGES=("$@")' >> /usr/local/bin/apt-get-install-retry && \
    echo '' >> /usr/local/bin/apt-get-install-retry && \
    echo 'MAX_RETRIES=4' >> /usr/local/bin/apt-get-install-retry && \
    echo '# Initial delay, will grow exponentially' >> /usr/local/bin/apt-get-install-retry && \
    echo 'delay=3' >> /usr/local/bin/apt-get-install-retry && \
    echo '' >> /usr/local/bin/apt-get-install-retry && \
    echo 'echo "Installing packages ${PACKAGES[*]} using apt-get"' >> /usr/local/bin/apt-get-install-retry && \
    echo '' >> /usr/local/bin/apt-get-install-retry && \
    echo 'for i in $(seq 1 $MAX_RETRIES); do' >> /usr/local/bin/apt-get-install-retry && \
    echo '  echo "Running apt-get update and install (attempt ${i})..."' >> /usr/local/bin/apt-get-install-retry && \
    echo '  if DEBIAN_FRONTEND=noninteractive apt-get update -qq && \' >> /usr/local/bin/apt-get-install-retry && \
    echo '     DEBIAN_FRONTEND=noninteractive apt-get install -qq -y \' >> /usr/local/bin/apt-get-install-retry && \
    echo '     -o Dpkg::Options::="--force-confdef" -o Dpkg::Options::="--force-confold" \' >> /usr/local/bin/apt-get-install-retry && \
    echo '      "${PACKAGES[@]}"; then' >> /usr/local/bin/apt-get-install-retry && \
    echo '    echo "apt-get succeeded on attempt ${i}.";' >> /usr/local/bin/apt-get-install-retry && \
    echo '    break;' >> /usr/local/bin/apt-get-install-retry && \
    echo '  else' >> /usr/local/bin/apt-get-install-retry && \
    echo '    if [ "${i}" -eq "${MAX_RETRIES}" ]; then' >> /usr/local/bin/apt-get-install-retry && \
    echo '      echo "Max retries reached (${MAX_RETRIES}). apt-get failed permanently." >&2' >> /usr/local/bin/apt-get-install-retry && \
    echo '      exit 1;' >> /usr/local/bin/apt-get-install-retry && \
    echo '    fi' >> /usr/local/bin/apt-get-install-retry && \
    echo '    ' >> /usr/local/bin/apt-get-install-retry && \
    echo '    echo "apt-get failed on attempt ${i}. Waiting $((delay**i)) seconds before retrying..."' >> /usr/local/bin/apt-get-install-retry && \
    echo '    sleep $((delay**i));' >> /usr/local/bin/apt-get-install-retry && \
    echo '  fi' >> /usr/local/bin/apt-get-install-retry && \
    echo 'done' >> /usr/local/bin/apt-get-install-retry && \
    chmod +x /usr/local/bin/apt-get-install-retry





# Basic C core dependencies

# C/C++ dependencies according to https://github.com/grpc/grpc/blob/master/BUILDING.md
RUN apt-get-install-retry \
  build-essential \
  autoconf \
  libtool \
  pkg-config
  

# GCC
RUN apt-get-install-retry \
  gcc \
  g++ 

# libc6
RUN apt-get-install-retry \
  libc6 \
  libc6-dbg \
  libc6-dev

# Tools
RUN apt-get-install-retry \
  bzip2 \
  curl \
  dnsutils \
  git \
  lcov \
  make \
  strace \
  time \
  unzip \
  wget \
  zip

#=================
# Setup git to access working directory across docker boundary.
# This avoids the "fatal: detected dubious ownership in repository XYZ"
# git error.

RUN git config --global --add safe.directory '*'
RUN git config --global protocol.file.allow always



# Configure git early so the cache reused by other py build based on the same image.
RUN git config --global protocol.file.allow always && \
  git config --global --add safe.directory /var/local/jenkins/grpc && \
  git config --global --add safe.directory /var/local/jenkins/grpc/.git && \
  git config --global --add safe.directory '/var/local/jenkins/grpc/.git/modules/third_party/*'


RUN apt-get-install-retry build-essential zlib1g-dev libncurses5-dev libgdbm-dev \
                                 libnss3-dev libssl-dev libreadline-dev libffi-dev && apt-get clean

ADD install_python_for_wheel_crosscompilation.sh /scripts/install_python_for_wheel_crosscompilation.sh

# These are synced to the versions in our grpc_artifact_python_ images based pypa/{many,musl}linux.
# grep grpc_artifact_python_ tools/bazelify_tests/dockerimage_current_versions.bzl | grep -oP '(?<=docker://)[^"]+(?=")'
# IMG=one_of_the_images_from_the_command_above
# docker run --rm $IMG bash -c 'ls -l /opt/python/cp3{9,1}*' | sed -E 's#^[^/]+##'
RUN /scripts/install_python_for_wheel_crosscompilation.sh "3.10.20" "3.10.20" /opt/python/cp310-cp310
RUN /scripts/install_python_for_wheel_crosscompilation.sh "3.11.15" "3.11.15" /opt/python/cp311-cp311
RUN /scripts/install_python_for_wheel_crosscompilation.sh "3.12.13" "3.12.13" /opt/python/cp312-cp312
RUN /scripts/install_python_for_wheel_crosscompilation.sh "3.13.13" "3.13.13" /opt/python/cp313-cp313
RUN /scripts/install_python_for_wheel_crosscompilation.sh "3.14.4" "3.14.4" /opt/python/cp314-cp314
RUN /scripts/install_python_for_wheel_crosscompilation.sh "3.15.0" "3.15.0b2" /opt/python/cp315-cp315

ENV AUDITWHEEL_ARCH armv7l
ENV AUDITWHEEL_PLAT linux_armv7l

#=================
# Install ccache

# Install ccache from source since ccache 3.x packaged with most linux distributions
# does not support Redis backend for caching.
RUN unset CMAKE_TOOLCHAIN_FILE && unset AS AR CC CPP CXX LD STRIP OBJCOPY \
    && curl -sSL --fail --retry 5 -o ccache.tar.gz https://github.com/ccache/ccache/releases/download/v4.11.3/ccache-4.11.3.tar.gz \
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
