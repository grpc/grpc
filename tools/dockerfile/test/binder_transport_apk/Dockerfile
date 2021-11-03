# Copyright 2021 gRPC authors.
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
# Image(c7f1523ebd92) is built on Jul 29, 2021
FROM gcr.io/oss-fuzz-base/base-builder@sha256:c7f1523ebd9234b9ff57e5240f8c06569143373be019c92f1e6df18a1e048f37

# This Dockerfile creates the environment for compiling and e2e testing of Android binder transport implementation

# Install basic packages and Bazel dependencies.
RUN apt-get update && apt-get install -y software-properties-common python-software-properties
RUN add-apt-repository ppa:webupd8team/java
RUN apt-get update && apt-get -y install \
  autoconf \
  build-essential \
  curl \
  wget \
  libtool \
  make \
  openjdk-8-jdk \
  vim

#========================
# Android SDK/NDK installation
ENV SDK_ROOT=/opt/android-sdk
RUN mkdir -p ${SDK_ROOT}
RUN wget -O cmd.zip dl.google.com/android/repository/commandlinetools-linux-7302050_latest.zip && \
    unzip cmd.zip && \
    yes | ./cmdline-tools/bin/sdkmanager --sdk_root=${SDK_ROOT} 'tools' && \
    yes | ./cmdline-tools/bin/sdkmanager --sdk_root=${SDK_ROOT} 'platform-tools' && \
    yes | ./cmdline-tools/bin/sdkmanager --sdk_root=${SDK_ROOT} 'build-tools;30.0.3' && \
    yes | ./cmdline-tools/bin/sdkmanager --sdk_root=${SDK_ROOT} 'platforms;android-29' && \
    yes | ./cmdline-tools/bin/sdkmanager --sdk_root=${SDK_ROOT} 'ndk-bundle' && \
    yes | ./cmdline-tools/bin/sdkmanager --sdk_root=${SDK_ROOT} 'ndk;21.4.7075529'

RUN rm cmd.zip

# Set environment variables for Bazel rules
ENV ANDROID_HOME=/opt/android-sdk
ENV ANDROID_NDK_HOME=/opt/android-sdk/ndk/21.4.7075529

#========================
# Bazel installation

# Must be in sync with tools/bazel
ENV BAZEL_VERSION 4.2.1

# The correct bazel version is already preinstalled, no need to use //tools/bazel wrapper.
ENV DISABLE_BAZEL_WRAPPER 1

RUN apt-get update && apt-get install -y wget && apt-get clean
RUN wget "https://github.com/bazelbuild/bazel/releases/download/$BAZEL_VERSION/bazel-$BAZEL_VERSION-installer-linux-x86_64.sh" && \
  bash ./bazel-$BAZEL_VERSION-installer-linux-x86_64.sh && \
  rm bazel-$BAZEL_VERSION-installer-linux-x86_64.sh


RUN mkdir -p /var/local/jenkins

# Define the default command.
CMD ["bash"]
