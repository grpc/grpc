# Copyright 2015, Google Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#     * Neither the name of Google Inc. nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

# Base Dockerfile for gRPC PHP.
#
# Includes PHP installation dependencies, things that are unlikely to vary.
FROM grpc/base

# Install RVM dependencies and other packages
RUN apt-get update && apt-get install -y \
    autoconf \
    automake \
    bison \
    curl \
    g++ \
    gawk \
    gcc \
    groff \
    libc6-dev \
    libffi-dev \
    libgdbm-dev \
    libncurses5-dev \
    libreadline6-dev \
    libsqlite3-dev \
    libssl-dev \
    libtool \
    libyaml-dev \
    make \
    patch \
    procps \
# TODO(mlumish): Uncomment these lines when building against them works
#    php5-common \
#    php5-cli \
#    php5-dev \
#    php-pear \
    pkg-config \
    procps \
    sqlite3 \
    zlib1g-dev

# Install the version of PHP gRPC is tested against
ENV DEBIAN_FRONTEND noniteractive
RUN apt-get update && apt-get install -y libxml2 libxml2-dev  # used by PHP
RUN cd /var/local \
  && curl -o php-5.5.17.tar.gz http://php.net/distributions/php-5.5.17.tar.gz \
  && tar -xf php-5.5.17.tar.gz \
  && cd php-5.5.17 \
  && ./configure --with-zlib=/usr --with-libxml-dir=ext/libxml \
  && make -j12 && make install

# Download the patched PHP protobuf so that PHP gRPC clients can be generated
# from proto3 schemas.
RUN git clone https://github.com/murgatroid99/Protobuf-PHP.git /var/local/git/protobuf-php

# Install ruby (via RVM) as ruby tools are dependencies for building Protobuf
# PHP extensions.
RUN gpg --keyserver hkp://keys.gnupg.net --recv-keys D39DC0E3  # Needed for RVM
RUN \curl -sSL https://get.rvm.io | bash -s stable --ruby
ENV PATH /usr/local/rvm/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin

# ronn: a ruby tool used to convert markdown to man pages, used during the
# install of Protobuf extensions
#
# rake: a ruby version of make used to build the PHP Protobuf extension
RUN rvm all do gem install ronn rake

# Get the source from GitHub, this gets the protobuf library as well
RUN git clone https://github.com/grpc/grpc.git /var/local/git/grpc
RUN cd /var/local/git/grpc && \
  git pull --recurse-submodules && \
  git submodule update --init --recursive

# Build and install the protobuf library
RUN cd /var/local/git/grpc/third_party/protobuf && \
  ./autogen.sh && \
  ./configure --prefix=/usr && \
  make -j12 && make check && make install && make clean

# Install the patched PHP protobuf so that PHP gRPC clients can be generated
# from proto3 schemas.
RUN cd /var/local/git/protobuf-php \
  && rvm all do rake pear:package version=1.0 \
  && pear install Protobuf-1.0.tgz

# Install PHPUnit, used to run the PHP unit tests
RUN wget https://phar.phpunit.de/phpunit.phar \
  && chmod +x phpunit.phar \
  && mv phpunit.phar /usr/local/bin/phpunit


# TODO: pre-building seems unnecessary, because we need to run make clean
# anyway to prevent build from crashing if header files are added/removed.
# Build the C core
RUN make static_c shared_c -j12 -C /var/local/git/grpc

# Define the default command.
CMD ["bash"]
