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

# Docker file for building gRPC artifacts.

# Recent enough cmake (>=3.9) needed by Android SDK
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

# Cmake for cross-compilation
RUN apt-get update && apt-get install -y cmake golang && apt-get clean

##################
# Android NDK

# Download and install Android NDK
RUN wget -q https://dl.google.com/android/repository/android-ndk-r16b-linux-x86_64.zip -O android_ndk.zip \
    && unzip -q android_ndk.zip \
    && rm android_ndk.zip \
    && mv ./android-ndk-r16b /opt
ENV ANDROID_NDK_PATH /opt/android-ndk-r16b

RUN apt-get update && apt-get install -y libpthread-stubs0-dev && apt-get clean

RUN mkdir /var/local/jenkins

# Define the default command.
CMD ["bash"]
