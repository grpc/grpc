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

FROM debian:bullseye
  
# Install Git and basic packages.
RUN apt-get update && apt-get install -y \
  autoconf \
  autotools-dev \
  build-essential \
  bzip2 \
  ccache \
  curl \
  dnsutils \
  gcc \
  gcc-multilib \
  git \
  golang \
  gyp \
  lcov \
  libc6 \
  libc6-dbg \
  libc6-dev \
  libgtest-dev \
  libtool \
  make \
  perl \
  strace \
  telnet \
  unzip \
  wget \
  zip && apt-get clean

#================
# Build profiling
RUN apt-get update && apt-get install -y time && apt-get clean

# Install Python 3.7 from source (and installed as a default python3)
# (Bullseye comes with Python 3.9 which isn't supported by pytype yet)
RUN apt update && apt install -y build-essential zlib1g-dev libncurses5-dev libgdbm-dev \
                                 libnss3-dev libssl-dev libreadline-dev libffi-dev
RUN curl -O https://www.python.org/ftp/python/3.7.9/Python-3.7.9.tar.xz && \
    tar -xf Python-3.7.9.tar.xz && \
    cd Python-3.7.9 && \
    ./configure && \
    make -j 4 && \
    make install
RUN curl https://bootstrap.pypa.io/get-pip.py | python3

# Install Python 2.7
RUN apt-get update && apt-get install -y python2 python2-dev
RUN ln -s /usr/bin/python2 /usr/bin/python
RUN curl https://bootstrap.pypa.io/pip/2.7/get-pip.py | python2

# Google Cloud platform API libraries
RUN pip install --upgrade google-auth==1.24.0 google-api-python-client==1.12.8 oauth2client==4.1.0


RUN mkdir /var/local/jenkins


#=================
# C++ dependencies
RUN apt-get update && apt-get -y install libgflags-dev libgtest-dev libc++-dev clang && apt-get clean


#========================
# Sanity test dependencies

RUN apt-get update && apt-get install -y \
      autoconf \
      automake \
      libtool \
      curl \
      shellcheck
RUN python2 -m pip install simplejson mako virtualenv==16.7.9 lxml
RUN python3 -m pip install simplejson mako virtualenv==16.7.9 lxml six

# Upgrade Python's YAML library
RUN python2 -m pip install --upgrade --ignore-installed PyYAML==5.4.1 --user
RUN python3 -m pip install --upgrade --ignore-installed PyYAML==5.4.1 --user

# Install clang, clang-format, and clang-tidy
RUN apt-get update && apt-get install -y clang clang-format-11 clang-tidy-11 jq
ENV CLANG_FORMAT=clang-format-11
ENV CLANG_TIDY=clang-tidy-11


#========================
# Bazel installation

# Must be in sync with tools/bazel
ENV BAZEL_VERSION 3.7.1

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
