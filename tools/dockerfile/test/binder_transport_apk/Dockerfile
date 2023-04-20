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
# Image(a4ad7a70fe10) is built on Apr 19, 2023
FROM gcr.io/oss-fuzz-base/base-builder@sha256:a4ad7a70fe10674c880ca8618a564860114a31dcdd6f9896aac89f0b2f86112a

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
ENV BAZEL_VERSION 6.1.2

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
RUN apt-get install -y openjdk-8-jdk

#========================
# Android SDK/NDK installation
ENV SDK_ROOT=/opt/android-sdk
RUN mkdir -p $SDK_ROOT
RUN wget -O cmd.zip dl.google.com/android/repository/commandlinetools-linux-7302050_latest.zip &&     unzip cmd.zip && rm cmd.zip &&     yes | ./cmdline-tools/bin/sdkmanager --sdk_root=$SDK_ROOT 'tools' &&     yes | ./cmdline-tools/bin/sdkmanager --sdk_root=$SDK_ROOT 'platform-tools' &&     yes | ./cmdline-tools/bin/sdkmanager --sdk_root=$SDK_ROOT 'build-tools;30.0.3' &&     yes | ./cmdline-tools/bin/sdkmanager --sdk_root=$SDK_ROOT 'platforms;android-29' &&     yes | ./cmdline-tools/bin/sdkmanager --sdk_root=$SDK_ROOT 'ndk-bundle' &&     yes | ./cmdline-tools/bin/sdkmanager --sdk_root=$SDK_ROOT 'ndk;21.4.7075529'

# Set environment variables for Bazel rules
ENV ANDROID_HOME=/opt/android-sdk
ENV ANDROID_NDK_HOME=/opt/android-sdk/ndk/21.4.7075529

RUN mkdir -p /var/local/jenkins

# Define the default command.
CMD ["bash"]
