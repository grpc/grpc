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
# This base image is built on Mar 12, 2024
FROM gcr.io/oss-fuzz-base/base-builder@sha256:c3581153788bc49f3634fec3cd36a5d6dfd26632c4afc157fb6faf8ce3af732e

# -------------------------- WARNING --------------------------------------
# If you are making changes to this file, consider changing
# https://github.com/google/oss-fuzz/blob/master/projects/grpc/Dockerfile
# accordingly.
# -------------------------------------------------------------------------

# Install basic packages
RUN apt-get update && apt-get -y install \
  autoconf \
  build-essential \
  curl \
  libtool \
  make \
  vim \
  wget

# Install python 3.9 and point python3 to python3.9
RUN apt-get update && apt-get install -y --no-install-recommends \
    python3.9 \
    python3.9-venv \
    python3-pip \
    python3-setuptools \
    python3-wheel \
    # Clean up apt caches to reduce image size
    && rm -rf /var/lib/apt/lists/*

RUN update-alternatives --install /usr/bin/python3 python3 /usr/bin/python3.9 1

#========================
# Bazel installation

# Must be in sync with tools/bazel
ENV BAZEL_VERSION 8.0.1

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


RUN apt-get install -y gdb

RUN mkdir -p /var/local/jenkins

# Define the default command.
CMD ["bash"]
