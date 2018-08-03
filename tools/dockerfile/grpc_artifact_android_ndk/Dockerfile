# Copyright 2016 gRPC authors.
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

# Docker file for building gRPC artifacts for Android.

FROM debian:sid

RUN apt-get update && apt-get install -y debian-keyring && apt-key update

# Install Git and basic packages.
RUN apt-get update && apt-key update && apt-get install -y \
  autoconf \
  autotools-dev \
  build-essential \
  bzip2 \
  clang \
  curl \
  gcc \
  gcc-multilib \
  git \
  golang \
  libc6 \
  libc6-dbg \
  libc6-dev \
  libgtest-dev \
  libtool \
  make \
  perl \
  strace \
  python-dev \
  python-setuptools \
  python-yaml \
  telnet \
  unzip \
  wget \
  zip && apt-get clean

# golang needed to build BoringSSL with cmake
RUN apt-get update && apt-get install -y golang && apt-get clean

# Java required by Android SDK
RUN apt-get update && apt-get -y install openjdk-8-jdk && apt-get clean

# Install Android SDK
ENV ANDROID_SDK_VERSION 4333796
RUN mkdir -p /opt/android-sdk && cd /opt/android-sdk && \
    wget -q https://dl.google.com/android/repository/sdk-tools-linux-${ANDROID_SDK_VERSION}.zip && \
    unzip -q sdk-tools-linux-${ANDROID_SDK_VERSION}.zip && \
    rm sdk-tools-linux-${ANDROID_SDK_VERSION}.zip
ENV ANDROID_SDK_PATH /opt/android-sdk

# Install Android NDK and cmake using sdkmanager
RUN mkdir -p ~/.android && touch ~/.android/repositories.cfg
RUN yes | ${ANDROID_SDK_PATH}/tools/bin/sdkmanager --licenses  # accept all licenses
RUN ${ANDROID_SDK_PATH}/tools/bin/sdkmanager ndk-bundle 'cmake;3.6.4111459'
ENV ANDROID_NDK_PATH ${ANDROID_SDK_PATH}/ndk-bundle
ENV ANDROID_SDK_CMAKE ${ANDROID_SDK_PATH}/cmake/3.6.4111459/bin/cmake

RUN mkdir /var/local/jenkins

# Define the default command.
CMD ["bash"]
