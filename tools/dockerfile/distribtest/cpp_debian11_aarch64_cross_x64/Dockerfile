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

FROM debian:11

# gRPC C++ dependencies based on https://github.com/grpc/grpc/blob/master/BUILDING.md
RUN apt-get update && apt-get install -y build-essential autoconf libtool pkg-config && apt-get clean

#=========================================================
# Install cmake from bullseye-backports
# Note that this step should be only used for Debian 11 (bullseye) images.

RUN echo "deb http://archive.debian.org/debian bullseye-backports main" > /etc/apt/sources.list.d/bullseye-backports.list && \
    apt-get update && \
    apt-get install -y -t bullseye-backports cmake

# C++ distribtests are setup in a way that requires git
RUN apt-get update && apt-get install -y git && apt-get clean

# aarch cross-compiler
RUN dpkg --add-architecture arm64
RUN apt-get update && apt-get install -y g++-10-aarch64-linux-gnu

RUN git config --global --add safe.directory /var/local/jenkins/grpc
RUN git config --global protocol.file.allow always

CMD ["bash"]
