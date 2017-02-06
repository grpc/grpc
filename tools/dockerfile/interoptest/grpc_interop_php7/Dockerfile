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

# Prepare ccache
RUN ln -s /usr/bin/ccache /usr/local/bin/gcc
RUN ln -s /usr/bin/ccache /usr/local/bin/g++
RUN ln -s /usr/bin/ccache /usr/local/bin/cc
RUN ln -s /usr/bin/ccache /usr/local/bin/c++
RUN ln -s /usr/bin/ccache /usr/local/bin/clang
RUN ln -s /usr/bin/ccache /usr/local/bin/clang++


RUN mkdir /var/local/jenkins

# Install composer
RUN curl -sS https://getcomposer.org/installer | php
RUN mv composer.phar /usr/local/bin/composer

# Define the default command.
CMD ["bash"]

