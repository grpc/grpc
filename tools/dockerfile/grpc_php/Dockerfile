# Dockerfile for gRPC PHP
FROM grpc/php_base

RUN cd /var/local/git/grpc \
  && git pull --recurse-submodules \
  && git submodule update --init --recursive

RUN make clean -C /var/local/git/grpc
RUN make install_c -j12 -C /var/local/git/grpc

RUN cd /var/local/git/grpc/src/php/ext/grpc && git pull && phpize

# Build the grpc PHP extension
RUN cd /var/local/git/grpc/src/php/ext/grpc \
  && ./configure \
  && make
