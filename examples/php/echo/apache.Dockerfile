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

FROM composer:1.8.6 as composer


FROM grpc-php/base as grpc-base


FROM php:8.1-apache-buster

RUN apt-get -qq update && apt-get -qq install -y git


COPY --from=composer /usr/bin/composer /usr/bin/composer

COPY --from=grpc-base /github/grpc/cmake/build/third_party/protobuf/protoc \
  /usr/local/bin/protoc

COPY --from=grpc-base /github/grpc/cmake/build/grpc_php_plugin \
  /usr/local/bin/protoc-gen-grpc

COPY --from=grpc-base \
  /usr/local/lib/php/extensions/no-debug-non-zts-20190902/grpc.so \
  /usr/local/lib/php/extensions/no-debug-non-zts-20190902/grpc.so


RUN docker-php-ext-enable grpc


WORKDIR /var/www/html

COPY client.php ./index.php
COPY composer.json .
COPY echo.proto .

RUN chmod 644 index.php

RUN protoc -I=. echo.proto --php_out=. --grpc_out=.

RUN composer install
