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

#========================
# Bazel installation

# Must be in sync with tools/bazel
ENV BAZEL_VERSION 7.1.0

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


#========================
# Java
RUN apt-get install -y openjdk-21-jdk

#========================
# Android SDK/NDK installation
ENV ANDROID_SDK_VERSION 11076708
ENV ANDROID_NDK_VERSION 26.2.11394342

ENV SDK_ROOT /opt/android-sdk

RUN mkdir -p $SDK_ROOT
RUN cd $SDK_ROOT &&     wget -O cmd.zip https://dl.google.com/android/repository/commandlinetools-linux-${ANDROID_SDK_VERSION}_latest.zip &&     unzip -q cmd.zip &&     rm cmd.zip

RUN yes | $SDK_ROOT/cmdline-tools/bin/sdkmanager --sdk_root=$SDK_ROOT --licenses  # accept all licenses

# This is not required but desirable to reduce the time to download and the chance of download failure.
RUN mkdir -p ~/.android && touch ~/.android/repositories.cfg

RUN $SDK_ROOT/cmdline-tools/bin/sdkmanager --sdk_root=$SDK_ROOT "ndk;$ANDROID_NDK_VERSION" "platforms;android-33" "build-tools;34.0.0"

# Set environment variables for Bazel rules
ENV ANDROID_HOME $SDK_ROOT
ENV ANDROID_NDK_HOME $SDK_ROOT/ndk/$ANDROID_NDK_VERSION

# Define the default command.
CMD ["bash"]
