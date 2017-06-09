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

FROM gcr.io/oss-fuzz-base/base-builder

# Install basic packages and Bazel dependencies.
RUN apt-get update && apt-get install -y software-properties-common python-software-properties
RUN add-apt-repository ppa:webupd8team/java
RUN apt-get update && apt-get -y install \
  autoconf \
  build-essential \
  curl \
  libtool \
  make \
  openjdk-8-jdk \
  vim

#========================
# Bazel installation
RUN echo "deb [arch=amd64] http://storage.googleapis.com/bazel-apt stable jdk1.8" > /etc/apt/sources.list.d/bazel.list
RUN curl https://bazel.build/bazel-release.pub.gpg | apt-key add -
RUN apt-get -y update
RUN apt-get -y install bazel

# Pin Bazel to 0.4.4
# Installing Bazel via apt-get first is required before installing 0.4.4 to
# allow gRPC to build without errors. See https://github.com/grpc/grpc/issues/10553
RUN curl -fSsL -O https://github.com/bazelbuild/bazel/releases/download/0.4.4/bazel-0.4.4-installer-linux-x86_64.sh
RUN chmod +x ./bazel-0.4.4-installer-linux-x86_64.sh
RUN ./bazel-0.4.4-installer-linux-x86_64.sh

RUN mkdir -p /var/local/jenkins

# Define the default command.
CMD ["bash"]
