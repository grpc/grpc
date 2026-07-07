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

FROM debian:13

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


RUN apt-get update && apt-get install -y \
  python3 \
  python3-pip \
  python3-setuptools \
  python3-yaml

RUN pip install \
  --no-cache-dir \
  --break-system-packages \
  --user \
  google-auth==1.23.0 \
  google-api-python-client==1.12.8 \
  oauth2client==4.1.0 \
  twisted

#=================
# C++ dependencies

COPY --from=repo_root tools/dockerfile/helper_scripts/install-clang.sh /root/install-clang.sh

RUN /root/install-clang.sh

#=================
# Install cmake
# Note that this step should be only used for distributions that have new enough cmake to satisfy gRPC's cmake version requirement.

COPY --from=repo_root tools/dockerfile/helper_scripts/install-cmake.sh /root/install-cmake.sh

RUN /root/install-cmake.sh

RUN apt-get update && apt-get install -y ccache


RUN mkdir /var/local/jenkins



#=================
# Setup git to access working directory across docker boundary

RUN git config --global --add safe.directory "*"
RUN git config --global protocol.file.allow always


# Define the default command.
CMD ["bash"]
