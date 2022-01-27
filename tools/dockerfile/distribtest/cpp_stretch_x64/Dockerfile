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

FROM debian:stretch

# gRPC C++ dependencies based on https://github.com/grpc/grpc/blob/master/BUILDING.md
RUN apt-get update && apt-get install -y build-essential autoconf libtool pkg-config && apt-get clean

# debian stretch has cmake 3.7: https://packages.debian.org/stretch/cmake
RUN apt-get update && apt-get install -y cmake && apt-get clean

# C++ distribtests are setup in a way that requires git
RUN apt-get update && apt-get install -y git && apt-get clean

CMD ["bash"]
