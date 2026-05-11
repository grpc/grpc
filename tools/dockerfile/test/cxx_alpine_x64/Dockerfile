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

FROM alpine:3.21

# Install Git and basic packages.
RUN apk update && apk add \
  autoconf \
  automake \
  bash \
  bzip2 \
  build-base \
  cmake \
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

#=================
# Setup git to access working directory across docker boundary.
# This avoids the "fatal: detected dubious ownership in repository XYZ"
# git error.

RUN git config --global --add safe.directory '*'
RUN git config --global protocol.file.allow always


# Google Cloud Platform API libraries
# These are needed for uploading test results to BigQuery (e.g. by tools/run_tests scripts)
RUN python3 -m pip install --break-system-packages --upgrade google-auth==1.24.0 google-api-python-client==1.12.8 oauth2client==4.1.0

RUN python3 -m pip install --break-system-packages --upgrade --ignore-installed PyYAML==6.0.1 --user
RUN python3 -m pip install --break-system-packages twisted

#=================
# Install ccache

# Install ccache from source since ccache 3.x packaged with most linux distributions
# does not support Redis backend for caching.
RUN curl -sSL --fail --retry 5 -o ccache.tar.gz https://github.com/ccache/ccache/releases/download/v4.11.3/ccache-4.11.3.tar.gz \
    && tar -zxf ccache.tar.gz \
    && cd ccache-4.11.3 \
    && mkdir build && cd build \
    && cmake -DCMAKE_BUILD_TYPE=Release -DZSTD_FROM_INTERNET=ON -DHIREDIS_FROM_INTERNET=ON .. \
    && make -j4 && make install \
    && cd ../.. \
    && rm -rf ccache-4.11.3 ccache.tar.gz


RUN mkdir /var/local/jenkins



# Define the default command.
CMD ["bash"]
