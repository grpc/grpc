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

FROM alpine:3.11

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
  python2-dev \
  py2-pip \
  unzip \
  wget \
  zip

# Install Python packages from PyPI
RUN pip install --upgrade pip==19.3.1
RUN pip install virtualenv
RUN pip install futures==2.2.0 enum34==1.0.4 protobuf==3.5.2.post1 six==1.15.0

# Google Cloud platform API libraries
RUN pip install --upgrade google-auth==1.24.0 google-api-python-client==1.12.8 oauth2client==4.1.0

RUN mkdir -p /var/local/jenkins

# Define the default command.
CMD ["bash"]
