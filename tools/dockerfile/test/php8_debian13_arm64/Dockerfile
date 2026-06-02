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

FROM arm64v8/debian:13

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
  g++ \
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
# Setup git to access working directory across docker boundary.
# This avoids the "fatal: detected dubious ownership in repository XYZ"
# git error.

RUN git config --global --add safe.directory '*'
RUN git config --global protocol.file.allow always



#====================
# run_tests.py python dependencies
RUN apt-get update && apt-get install -y \
  python3 \
  python3-pip \
  python3-setuptools \
  python3-yaml \
  && apt-get clean

# Skip pinning pip to 19.3.1 as it fails on Debian 13.
RUN python3 -m pip install --break-system-packages six==1.16.0

# Google Cloud Platform API libraries
RUN python3 -m pip install --break-system-packages --upgrade google-auth==1.23.0 google-api-python-client==1.12.8 oauth2client==4.1.0

# Install default PHP (which is 8.4 in Debian 13)
RUN apt-get update && apt-get install -y php php-dev php-pear php-mbstring && apt-get clean

# Install PHPUnit (PHPUnit 11 is the stable version that supports PHP 8.4)
RUN wget https://phar.phpunit.de/phpunit-11.phar && \
    mv phpunit-11.phar /usr/local/bin/phpunit && \
    chmod +x /usr/local/bin/phpunit

#=================
# PHP Test dependencies

 RUN apt-get update && apt-get install -y \
    valgrind
#=================
# Install cmake
# Note that this step should be only used for distributions that have new enough cmake to satisfy gRPC's cmake version requirement.

RUN apt-get update && apt-get install -y cmake && apt-get clean

RUN apt-get update && apt-get install -y ccache && apt-get clean

RUN mkdir /var/local/jenkins


# Define the default command.
CMD ["bash"]
