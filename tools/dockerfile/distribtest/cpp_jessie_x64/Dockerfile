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

FROM debian:jessie
RUN sed -i '/deb http:\/\/deb.debian.org\/debian jessie-updates main/d' /etc/apt/sources.list

# Install packages needed for gRPC and protobuf
RUN apt-get update && apt-get install -y \
      autoconf \
      automake \
      build-essential \
      curl \
      git \
      g++ \
      libtool \
      make \
      pkg-config \
      unzip && apt-get clean

RUN apt-get update && apt-get install -y golang && apt-get clean

RUN echo "deb http://archive.debian.org/debian jessie-backports main" | tee /etc/apt/sources.list.d/jessie-backports.list
RUN echo 'Acquire::Check-Valid-Until "false";' > /etc/apt/apt.conf
RUN sed -i '/deb http:\/\/deb.debian.org\/debian jessie-updates main/d' /etc/apt/sources.list
RUN apt-get update && apt-get install -t jessie-backports -y cmake && apt-get clean

CMD ["bash"]
