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

FROM debian:12

#=================
# Install apt-get-install-retry wrapper script
#=================
#  Create wrapper for apt-get

RUN /bin/bash -c 'echo -e "#!/usr/bin/env bash\n\
# Copyright 2026 The gRPC Authors\n\
\n\
PS4=\"+ \$(date +[%H:%M:%S_%Z]) \"\n\
set -xeuo pipefail\n\
\n\
MAX_RETRIES=4\n\
delay=3\n\
\n\
for i in \$(seq 1 \$MAX_RETRIES); do\n\
  if apt-get update && apt-get install -y \"\$@\"; then\n\
    break\n\
  fi\n\
  sleep \$((delay**i))\n\
done\n\
" > /usr/local/bin/apt-get-install-retry'
RUN chmod +x /usr/local/bin/apt-get-install-retry


# Basic C core dependencies

# C/C++ dependencies according to https://github.com/grpc/grpc/blob/master/BUILDING.md
RUN apt-get-install-retry \
  build-essential \
  autoconf \
  libtool \
  pkg-config
  

# GCC
RUN apt-get-install-retry \
  gcc \
  g++ 

# libc6
RUN apt-get-install-retry \
  libc6 \
  libc6-dbg \
  libc6-dev

# Tools
RUN apt-get-install-retry \
  bzip2 \
  curl \
  dnsutils \
  git \
  lcov \
  make \
  strace \
  time \
  unzip \
  wget \
  zip

#=================
# Setup git to access working directory across docker boundary.
# This avoids the "fatal: detected dubious ownership in repository XYZ"
# git error.

RUN git config --global --add safe.directory '*'
RUN git config --global protocol.file.allow always


#====================
# run_tests.py python dependencies

# Basic python dependencies to be able to run tools/run_tests python scripts
# These dependencies are not sufficient to build gRPC Python, gRPC Python
# deps are defined elsewhere (e.g. python_deps.include)
RUN apt-get-install-retry \
  python3 \
  python3-pip \
  python3-setuptools \
  python3-yaml && apt-get clean
 

# use pinned version of pip to avoid sudden breakages
# Some newer distros that have already implemented PEP 668. Use of
# --break-system-packages is to workaround that. We should look into using
# virtualenv in Dockerfile though.
RUN python3 -m pip install --break-system-packages --upgrade pip==19.3.1

# TODO(jtattermusch): currently six is needed for tools/run_tests scripts
# but since our python2 usage is deprecated, we should get rid of it.
RUN python3 -m pip install six==1.16.0

# Google Cloud Platform API libraries
# These are needed for uploading test results to BigQuery (e.g. by tools/run_tests scripts)
RUN python3 -m pip install --upgrade google-auth==1.23.0 google-api-python-client==1.12.8 oauth2client==4.1.0


RUN apt-get-install-retry php8.2 php8.2-dev php-pear
#=================
# PHP Test dependencies

 RUN apt-get-install-retry \
    valgrind

#=================
# Install cmake
# Note that this step should be only used for distributions that have new enough cmake to satisfy gRPC's cmake version requirement.

RUN apt-get-install-retry cmake

RUN apt-get-install-retry ccache && apt-get clean

RUN mkdir /var/local/jenkins

# Define the default command.
CMD ["bash"]
