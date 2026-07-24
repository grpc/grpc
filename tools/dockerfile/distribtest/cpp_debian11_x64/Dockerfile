# Copyright 2020 The gRPC Authors
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
# Install cmake from kitware binary release to get a newer version (3.22+)
RUN apt-get update && apt-get install -y build-essential autoconf libtool pkg-config wget && apt-get clean
RUN wget -qO- https://github.com/Kitware/CMake/releases/download/v3.28.1/cmake-3.28.1-linux-x86_64.tar.gz | tar --strip-components=1 -xz -C /usr/local

# C++ distribtests are setup in a way that requires git
RUN apt-get update && apt-get install -y git && apt-get clean

RUN git config --global --add safe.directory /var/local/jenkins/grpc
RUN git config --global protocol.file.allow always


CMD ["bash"]
