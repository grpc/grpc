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

FROM alpine:3.15

# Install Git and basic packages.
RUN apk update && apk add \
  autoconf \
  automake \
  bzip2 \
  build-base \
  cmake \
  ccache \
  curl \
  gcc \
  git \
  libtool \
  linux-headers \
  make \
  perl \
  strace \
  python3 \
  py3-pip \
  unzip \
  wget \
  zip

# use pinned version of pip to avoid sudden breakages
RUN python3 -m pip install --upgrade pip==19.3.1

# TODO(jtattermusch): currently six is needed for tools/run_tests scripts
# but since our python2 usage is deprecated, we should get rid of it.
RUN python3 -m pip install six==1.16.0 twisted

# Google Cloud Platform API libraries
# These are needed for uploading test results to BigQuery (e.g. by tools/run_tests scripts)
RUN python3 -m pip install --upgrade google-auth==1.24.0 google-api-python-client==1.12.8 oauth2client==4.1.0

RUN python3 -m pip install --upgrade --ignore-installed PyYAML==6.0.1 --user

#=================
# Install ccache

# Install ccache from source since ccache 3.x packaged with most linux distributions
# does not support Redis backend for caching.
RUN curl -sSL -o ccache.tar.gz https://github.com/ccache/ccache/releases/download/v4.5.1/ccache-4.5.1.tar.gz \
    && tar -zxf ccache.tar.gz \
    && cd ccache-4.5.1 \
    && mkdir build && cd build \
    && cmake -DCMAKE_BUILD_TYPE=Release -DZSTD_FROM_INTERNET=ON -DHIREDIS_FROM_INTERNET=ON .. \
    && make -j4 && make install \
    && cd ../.. \
    && rm -rf ccache-4.5.1 ccache.tar.gz


RUN mkdir /var/local/jenkins

#=================
# Setup git to access working directory across docker boundary

RUN git config --global --add safe.directory /var/local/jenkins/grpc
RUN git config --global protocol.file.allow always


# Define the default command.
CMD ["bash"]
