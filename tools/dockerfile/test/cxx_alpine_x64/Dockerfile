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
  ccache \
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



# Add Python user install directory to PATH
ENV PATH="$HOME/.local/bin:$PATH"
RUN pip install --no-cache-dir --break-system-packages --user \
  google-api-python-client==1.12.8 \
  google-auth==1.24.0 \
  oauth2client==4.1.0 \
  PyYAML \
  twisted \
  && :



RUN mkdir /var/local/jenkins



# Define the default command.
CMD ["bash"]
