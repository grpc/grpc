# Copyright 2019 gRPC authors.
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

FROM debian:buster

RUN apt-get -qq update && apt-get -qq -y upgrade && apt-get -qq install -y \
  autoconf bison build-essential git libtool \
  libcurl4-openssl-dev libedit-dev libonig-dev libsodium-dev \
  libsqlite3-dev libssl-dev libxml2-dev \
  pkg-config re2c valgrind wget zlib1g-dev


WORKDIR /tmp

RUN wget https://phar.phpunit.de/phpunit-8.5.13.phar && \
  mv phpunit-8.5.13.phar /usr/local/bin/phpunit && \
  chmod +x /usr/local/bin/phpunit


WORKDIR /github/php-src

ARG MAKEFLAGS=-j8

RUN git clone https://github.com/php/php-src .

RUN git checkout php-7.4.33 && \
  ./buildconf --force && \
  ./configure --build=x86_64-linux-gnu --enable-option-checking=fatal \
    --enable-debug --enable-pcntl \
    --enable-ftp --enable-mbstring --enable-mysqlnd \
    --with-curl --with-libedit --with-mhash --with-openssl \
    --with-pear --with-sodium=shared --with-zlib && \
  make && make install


WORKDIR /github/grpc

COPY . .

RUN pear package && \
  find . -name grpc-*.tgz | xargs -I{} pecl install {}


CMD ["/github/grpc/src/php/bin/run_tests.sh", "--skip-persistent-channel-tests", \
     "--ignore-valgrind-undef-errors"]

