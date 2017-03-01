# Copyright 2015, gRPC authors
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

FROM ubuntu:wily
RUN apt-get update
RUN apt-get -y install software-properties-common python-software-properties
RUN add-apt-repository ppa:webupd8team/java
RUN apt-get update
RUN apt-get -y install \
	vim            \
	wget           \
	openjdk-8-jdk  \
	pkg-config     \
	zip            \
	g++            \
	zlib1g-dev     \
	unzip          \
	git

RUN git clone https://github.com/bazelbuild/bazel.git /bazel
RUN cd /bazel && ./compile.sh

RUN ln -s /bazel/output/bazel /bin/

# ensure the installation has been extracted
RUN bazel
