# Copyright 2016, Google Inc.
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

FROM debian:jessie

#=================
# PHP7 dependencies

# Install Git and basic packages.
RUN apt-get update && apt-get install -y \
  autoconf \
  automake \
  build-essential \
  ccache \
  curl \
  git \
  libcurl4-openssl-dev \
  libgmp-dev \
  libgmp3-dev \
  libssl-dev \
  libtool \
  libxml2-dev \
  pkg-config \
  re2c \
  time \
  unzip \
  wget \
  zip && apt-get clean

# Install other dependencies
RUN ln -sf /usr/include/x86_64-linux-gnu/gmp.h /usr/include/gmp.h
RUN wget http://ftp.gnu.org/gnu/bison/bison-2.6.4.tar.gz -O /var/local/bison-2.6.4.tar.gz
RUN cd /var/local \
  && tar -zxvf bison-2.6.4.tar.gz \
  && cd /var/local/bison-2.6.4 \
  && ./configure \
  && make \
  && make install

# Compile PHP7 from source
RUN git clone https://github.com/php/php-src /var/local/git/php-src
RUN cd /var/local/git/php-src \
  && git checkout PHP-7.0.9 \
  && ./buildconf --force \
  && ./configure \
  --with-gmp \
  --with-openssl \
  --with-zlib \
  && make \
  && make install

#==================
# Ruby dependencies

# Install rvm
RUN gpg --keyserver hkp://keys.gnupg.net --recv-keys 409B6B1796C275462A1703113804BB82D39DC0E3
RUN \curl -sSL https://get.rvm.io | bash -s stable

# Install Ruby 2.1
RUN /bin/bash -l -c "rvm install ruby-2.1"
RUN /bin/bash -l -c "rvm use --default ruby-2.1"
RUN /bin/bash -l -c "echo 'gem: --no-ri --no-rdoc' > ~/.gemrc"
RUN /bin/bash -l -c "echo 'export PATH=/usr/local/rvm/bin:$PATH' >> ~/.bashrc"
RUN /bin/bash -l -c "echo 'rvm --default use ruby-2.1' >> ~/.bashrc"
RUN /bin/bash -l -c "gem install bundler --no-ri --no-rdoc"

# Prepare ccache
RUN ln -s /usr/bin/ccache /usr/local/bin/gcc
RUN ln -s /usr/bin/ccache /usr/local/bin/g++
RUN ln -s /usr/bin/ccache /usr/local/bin/cc
RUN ln -s /usr/bin/ccache /usr/local/bin/c++
RUN ln -s /usr/bin/ccache /usr/local/bin/clang
RUN ln -s /usr/bin/ccache /usr/local/bin/clang++


RUN mkdir /var/local/jenkins

# ronn: a ruby tool used to convert markdown to man pages, used during the
# install of Protobuf extensions
#
# rake: a ruby version of make used to build the PHP Protobuf extension
RUN /bin/bash -l -c "rvm all do gem install ronn rake"

# Install composer
RUN curl -sS https://getcomposer.org/installer | php
RUN mv composer.phar /usr/local/bin/composer

# Download the patched PHP protobuf so that PHP gRPC clients can be generated
# from proto3 schemas.
RUN git clone https://github.com/stanley-cheung/Protobuf-PHP.git /var/local/git/protobuf-php

RUN /bin/bash -l -c "rvm use ruby-2.1 \
  && cd /var/local/git/protobuf-php \
  && rvm all do rake pear:package version=1.0 \
  && pear install Protobuf-1.0.tgz"

# Define the default command.
CMD ["bash"]

