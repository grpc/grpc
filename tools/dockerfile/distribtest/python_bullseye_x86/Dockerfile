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

FROM i386/debian:bullseye

# Install build essentials and dependencies for building python
RUN apt-get update && apt-get install -y \
    build-essential \
    libssl-dev \
    zlib1g-dev \
    libncurses5-dev \
    libreadline-dev \
    libsqlite3-dev \
    libbz2-dev \
    libffi-dev \
    wget

# Download and build Python 3.10.14
RUN wget https://www.python.org/ftp/python/3.10.14/Python-3.10.14.tar.xz \
    && tar -xf Python-3.10.14.tar.xz \
    && cd Python-3.10.14 \
    && ./configure --enable-optimizations \
    && make -j 4 \
    && make install \
    && cd .. \
    && rm -rf Python-3.10.14*

# Ensure python3 points to the new python 3.10
RUN ln -sf /usr/local/bin/python3.10 /usr/local/bin/python3

RUN python3 -m pip install virtualenv

# docker is running on a 64-bit machine, so we need to
# override "uname -m" to report i686 instead of x86_64, otherwise
# python will choose a wrong binary package to install.
ENTRYPOINT ["linux32"]
