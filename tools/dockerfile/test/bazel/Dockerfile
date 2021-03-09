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

# Pinned version of the base image is used to avoid regressions caused
# by rebuilding of this docker image. To see available versions, you can run
# "gcloud container images list-tags gcr.io/oss-fuzz-base/base-builder"
# TODO(jtattermusch): with the latest version we'd get clang12+
# which makes our build fail due to new warnings being treated
# as errors.
FROM gcr.io/oss-fuzz-base/base-builder@sha256:de220fd2433cd53bd06b215770dcd14a5e74632e0215acea7401fee8cafb18da

# -------------------------- WARNING --------------------------------------
# If you are making changes to this file, consider changing
# https://github.com/google/oss-fuzz/blob/master/projects/grpc/Dockerfile
# accordingly.
# -------------------------------------------------------------------------

# Install basic packages and Bazel dependencies.
RUN apt-get update && apt-get install -y software-properties-common python-software-properties
RUN add-apt-repository ppa:webupd8team/java
RUN apt-get update && apt-get -y install \
  autoconf \
  build-essential \
  curl \
  wget \
  libtool \
  make \
  openjdk-8-jdk \
  vim

#====================
# Python dependencies

# Install dependencies

RUN apt-get update && apt-get install -y \
    python-all-dev \
    python3-all-dev \
    python-setuptools

# Install Python packages from PyPI
RUN curl https://bootstrap.pypa.io/pip/2.7/get-pip.py | python2.7
RUN pip install --upgrade pip==19.3.1
RUN pip install virtualenv==16.7.9
RUN pip install futures==2.2.0 enum34==1.0.4 protobuf==3.5.2.post1 six==1.15.0 twisted==17.5.0


#=================
# Compile CPython 3.6.9 from source

RUN apt-get update && apt-get install -y zlib1g-dev libssl-dev
RUN apt-get update && apt-get install -y jq build-essential libffi-dev

RUN cd /tmp && \
    wget -q https://www.python.org/ftp/python/3.6.9/Python-3.6.9.tgz && \
    tar xzvf Python-3.6.9.tgz && \
    cd Python-3.6.9 && \
    ./configure && \
    make install

RUN cd /tmp && \
    echo "ff7cdaef4846c89c1ec0d7b709bbd54d Python-3.6.9.tgz" > checksum.md5 && \
    md5sum -c checksum.md5

RUN python3.6 -m ensurepip && \
    python3.6 -m pip install coverage


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


RUN mkdir -p /var/local/jenkins

# Define the default command.
CMD ["bash"]
