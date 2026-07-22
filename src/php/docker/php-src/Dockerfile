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

#=================
#  Create wrapper for apt-get

COPY --chmod=+x <<-"EOF" /usr/local/bin/apt-get-install-retry
#!/usr/bin/env bash
# Copyright 2026 The gRPC Authors
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


# Ensure all tracking are registered

PS4="+ \$(date +[%H:%M:%S_%Z]) "
set -xeuo pipefail

# Ensure package to install is provided
if [ $# -eq 0 ]; then
  echo "Error: No package name provided to install with apt-get." >&2
  exit 1
fi

PACKAGES=("$@")

MAX_RETRIES=4
# Initial delay, will grow exponentially
delay=3

echo "Installing packages ${PACKAGES} using apt-get"

for i in $(seq 1 $MAX_RETRIES); do
  echo "Running apt-get update and install (attempt ${i})..."
  if apt-get update && apt-get install -y "${PACKAGES[@]}"; then
    echo "apt-get succeeded on attempt ${i}.";
    break;
  else
    if [ "${i}" -eq "${MAX_RETRIES}" ]; then
      echo "Max retries reached (${MAX_RETRIES}). apt-get failed permanently." >&2
      exit 1;
    fi
    
    echo "apt-get failed on attempt ${i}. Waiting $(($delay**$i)) seconds before retrying..."
    sleep $(($delay**$i));
  fi
done

EOF


RUN apt-get -qq update && apt-get -qq -y upgrade
RUN /usr/local/bin/apt-get-retry-install \
  autoconf bison build-essential git libtool \
  libcurl4-openssl-dev libedit-dev libonig-dev libsodium-dev \
  libsqlite3-dev libssl-dev libxml2-dev \
  pkg-config re2c valgrind wget zlib1g-dev


WORKDIR /tmp

RUN wget https://phar.phpunit.de/phpunit-9.5.9.phar && \
  mv phpunit-9.5.9.phar /usr/local/bin/phpunit && \
  chmod +x /usr/local/bin/phpunit


WORKDIR /github/php-src

ARG MAKEFLAGS=-j8

RUN git clone https://github.com/php/php-src .

RUN git checkout php-8.1.27 && \
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

