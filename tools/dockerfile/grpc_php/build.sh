#!/bin/bash

cp -R /var/local/git-clone/grpc /var/local/git

make clean -C /var/local/git/grpc

make install_c -j12 -C /var/local/git/grpc

cd /var/local/git/grpc/src/php/ext/grpc && git pull && phpize

cd /var/local/git/grpc/src/php/ext/grpc \
  && ./configure \
  && make

cd /var/local/git/grpc/src/php && composer install

cd /var/local/git/grpc/src/php && protoc-gen-php -i tests/interop/ -o tests/interop/ tests/interop/test.proto

