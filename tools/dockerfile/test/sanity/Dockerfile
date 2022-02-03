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

FROM silkeh/clang:13

#=================
# Basic C core dependencies

# C/C++ dependencies according to https://github.com/grpc/grpc/blob/master/BUILDING.md
RUN apt-get update && apt-get install -y \
  build-essential \
  autoconf \
  libtool \
  pkg-config \
  && apt-get clean

# GCC
RUN apt-get update && apt-get install -y \
  gcc \
  gcc-multilib \
  g++ \
  g++-multilib  \
  && apt-get clean

# libc6
RUN apt-get update && apt-get install -y \
  libc6 \
  libc6-dbg \
  libc6-dev \
  && apt-get clean

# Tools
RUN apt-get update && apt-get install -y \
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
  zip \
  && apt-get clean

#=================
# C++ dependencies
RUN apt-get update && apt-get -y install libc++-dev clang && apt-get clean


# Install Python 3.7 from source (and installed as a default python3)
# (Bullseye comes with Python 3.9 which isn't supported by pytype yet)
RUN apt update && apt install -y build-essential zlib1g-dev libncurses5-dev libgdbm-dev \
                            libnss3-dev libssl-dev libreadline-dev libffi-dev libbz2-dev
RUN curl -O https://www.python.org/ftp/python/3.7.9/Python-3.7.9.tar.xz && \
tar -xf Python-3.7.9.tar.xz && \
cd Python-3.7.9 && \
./configure && \
make -j 4 && \
make install
RUN curl https://bootstrap.pypa.io/get-pip.py | python3

# Google Cloud Platform API libraries
# These are needed for uploading test results to BigQuery (e.g. by tools/run_tests scripts)
RUN python3 -m pip install --upgrade google-auth==1.23.0 google-api-python-client==1.12.8 oauth2client==4.1.0


RUN mkdir /var/local/jenkins


#========================
# Sanity test dependencies

RUN apt-get update && apt-get install -y \
      autoconf \
      automake \
      libtool \
      curl \
      shellcheck

# otherwise clang-tidy will report missing <gtest/gtest.h> header
RUN apt-get update && apt-get install -y libgtest-dev && apt-get clean

RUN python3 -m pip install simplejson mako virtualenv==16.7.9 lxml six

# Upgrade Python's YAML library
RUN python3 -m pip install --upgrade --ignore-installed PyYAML==5.4.1 --user

# Install prerequisites for the clang-tidy script
RUN apt-get update && apt-get install -y jq git

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

# Install buildifier v0.29.0
RUN wget https://github.com/bazelbuild/buildtools/releases/download/0.29.0/buildifier
RUN chmod +x buildifier
RUN mv buildifier /usr/local/bin


# Define the default command.
CMD ["bash"]
