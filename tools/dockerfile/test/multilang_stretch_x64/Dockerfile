# Copyright 2018 The gRPC Authors
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

FROM debian:stretch

# Install Git and basic packages.
RUN apt-get update && apt-get install -y \
  autoconf \
  autotools-dev \
  build-essential \
  bzip2 \
  ccache \
  curl \
  dnsutils \
  gcc \
  gcc-multilib \
  git \
  golang \
  gyp \
  lcov \
  libc6 \
  libc6-dbg \
  libc6-dev \
  libgtest-dev \
  libtool \
  make \
  perl \
  strace \
  python-dev \
  python-setuptools \
  python-yaml \
  telnet \
  unzip \
  wget \
  zip && apt-get clean

#================
# Build profiling
RUN apt-get update && apt-get install -y time && apt-get clean

# Google Cloud platform API libraries
RUN apt-get update && apt-get install -y python-pip && apt-get clean
RUN pip install --upgrade google-api-python-client oauth2client

#================
# C# dependencies

# Install dependencies
RUN apt-get update && apt-get install -y     mono-devel     ca-certificates-mono     nuget     cmake     && apt-get clean

RUN nuget update -self

# Install .NET CLI dependencies
RUN apt-get update     && apt-get install -y --no-install-recommends         libc6         libgcc1         libgssapi-krb5-2         libicu57         liblttng-ust0         libssl1.0.2         libstdc++6         zlib1g     && rm -rf /var/lib/apt/lists/*

ENV DOTNET_SDK_VERSION 2.1.500

RUN curl -SL --output dotnet.tar.gz https://dotnetcli.blob.core.windows.net/dotnet/Sdk/$DOTNET_SDK_VERSION/dotnet-sdk-$DOTNET_SDK_VERSION-linux-x64.tar.gz     && dotnet_sha512='85055728e2433dfde41d15c85475f2dc6cfdd30242b4b23065b63cb12cc846acb93c09c000b02b722890ceac8ac382b40871c78660716ca2339c71052fe52f4e'     && sha512sum dotnet.tar.gz     && echo "$dotnet_sha512 dotnet.tar.gz" | sha512sum -c -     && mkdir -p /usr/share/dotnet     && tar -zxf dotnet.tar.gz -C /usr/share/dotnet     && rm dotnet.tar.gz     && ln -s /usr/share/dotnet/dotnet /usr/bin/dotnet

# Trigger the population of the local package cache
ENV NUGET_XMLDOC_MODE skip
RUN mkdir warmup     && cd warmup     && dotnet new     && cd ..     && rm -rf warmup

#=================
# C++ dependencies
RUN apt-get update && apt-get -y install libgflags-dev libgtest-dev libc++-dev clang && apt-get clean

#==================
# Node dependencies

# Install nvm
RUN touch .profile
RUN curl -o- https://raw.githubusercontent.com/creationix/nvm/v0.25.4/install.sh | bash
# Install all versions of node that we want to test
RUN /bin/bash -l -c "nvm install 4 && npm config set cache /tmp/npm-cache"
RUN /bin/bash -l -c "nvm install 5 && npm config set cache /tmp/npm-cache"
RUN /bin/bash -l -c "nvm install 6 && npm config set cache /tmp/npm-cache"
RUN /bin/bash -l -c "nvm install 8 && npm config set cache /tmp/npm-cache"
RUN /bin/bash -l -c "nvm install 9 && npm config set cache /tmp/npm-cache"
RUN /bin/bash -l -c "nvm install 10 && npm config set cache /tmp/npm-cache"
RUN /bin/bash -l -c "nvm alias default 10"
#=================
# PHP dependencies

# Install dependencies

RUN apt-get update && apt-get install -y     git php php-all-dev phpunit unzip

#==================
# Ruby dependencies

RUN apt-get install -y ruby ruby-dev
RUN echo 'gem: --no-ri --no-rdoc' > ~/.gemrc
RUN gem update --system
RUN gem install bundler --no-ri --no-rdoc

# Install Python 2.7
RUN apt-get update && apt-get install -y python2.7 python-all-dev
RUN curl https://bootstrap.pypa.io/get-pip.py | python2.7

# Install Python 3.7
# Add Debian 'testing' repository
RUN echo 'deb http://ftp.de.debian.org/debian testing main' >> /etc/apt/sources.list
RUN echo 'APT::Default-Release "stable";' | tee -a /etc/apt/apt.conf.d/00local

RUN apt-get update && apt-get -t testing install -y python3.7 python3-all-dev
RUN curl https://bootstrap.pypa.io/get-pip.py | python3.7


# Install coverage for Python test coverage reporting
RUN pip install coverage
ENV PATH ~/.local/bin:$PATH

# Install Mako to generate files in grpc/grpc-node
RUN pip install Mako


RUN mkdir /var/local/jenkins

# Define the default command.
CMD ["bash"]
