# Copyright 2015 gRPC authors.
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

# Pinned version of the base image is used to avoid regressions caused
# by rebuilding of this docker image. To see available versions, you can run
# "gcloud container images list-tags gcr.io/oss-fuzz-base/base-builder"
# This base image is built on Jul 16, 2025.
FROM gcr.io/oss-fuzz-base/base-builder@sha256:4f3ca10accd14292556601d70e457fa85ad57180c913484427869d3379a07684

# -------------------------- WARNING --------------------------------------
# If you are making changes to this file, consider changing
# https://github.com/google/oss-fuzz/blob/master/projects/grpc/Dockerfile
# accordingly.
# -------------------------------------------------------------------------
# Install apt-get-install-retry wrapper script
#=================
#  Create wrapper for apt-get

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
        echo '    if apt-get update && apt-get install -y "${PACKAGES[@]}"; then' >> /usr/local/bin/apt-get-install-retry && \
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


# Install basic packages
RUN apt-get-install-retry \
  autoconf \
  build-essential \
  curl \
  libtool \
  make \
  vim \
  wget


#========================
# Bazel installation

# Must be in sync with tools/bazel
ENV BAZEL_VERSION 8.7.0

# The correct bazel version is already preinstalled, no need to use //tools/bazel wrapper.
ENV DISABLE_BAZEL_WRAPPER 1

# Download the correct bazel version and make sure it's on path.
RUN BAZEL_ARCH_SUFFIX="$(uname -m | sed s/aarch64/arm64/)" \
  && curl -sSL --fail -o /usr/local/bin/bazel "https://github.com/bazelbuild/bazel/releases/download/$BAZEL_VERSION/bazel-$BAZEL_VERSION-linux-$BAZEL_ARCH_SUFFIX" \
  && chmod a+x /usr/local/bin/bazel

# Normally we would run "bazel --version" here to make sure bazel
# was correctly installed, but we can't do that because
# of # https://github.com/bazelbuild/bazel/issues/11379.
# We want to keep the arm64 version of the image buildable
# on x64 with use of qemu-user-static & binfmt emulation,
# but the self-extraction is broken for bazel currently.
# The binary will work correctly when run on real arm64
# hardware, when qemu-user-static isn't getting into the way.

#=================
# Setup git to access working directory across docker boundary.
# This avoids the "fatal: detected dubious ownership in repository XYZ"
# git error.

RUN git config --global --add safe.directory '*'
RUN git config --global protocol.file.allow always

#=================
# Setup git to access working directory across docker boundary.
# This avoids the "fatal: detected dubious ownership in repository XYZ"
# git error. Using "--system" makes the setting work
# for all users or even current user is not properly defined
# (which can happen e.g. inside execution environment
# of a bazel action)

RUN git config --system --add safe.directory '*'
RUN git config --system protocol.file.allow always


RUN apt-get-install-retry gdb && apt-get clean

RUN mkdir -p /var/local/jenkins

# Define the default command.
CMD ["bash"]
