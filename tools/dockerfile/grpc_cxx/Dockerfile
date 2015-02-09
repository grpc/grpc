# Dockerfile for gRPC C++
FROM grpc/base

RUN apt-get update && apt-get -y install libgflags-dev libgtest-dev

# Get the source from GitHub
RUN git clone git@github.com:google/grpc.git /var/local/git/grpc
RUN cd /var/local/git/grpc && \
  git pull --recurse-submodules && \
  git submodule update --init --recursive

# Build the protobuf library; then the C core.
RUN cd /var/local/git/grpc/third_party/protobuf && \
  ./autogen.sh && \
  ./configure --prefix=/usr && \
  make -j12 && make check && make install && make clean

RUN cd /var/local/git/grpc && ls \
  && make clean \
  && make gens/test/cpp/util/messages.pb.cc \
  && make interop_client \
  && make interop_server

ADD service_account service_account
ADD cacerts cacerts
ENV GRPC_DEFAULT_SSL_ROOTS_FILE_PATH /cacerts/roots.pem

CMD ["/var/local/git/grpc/bins/opt/interop_server", "--enable_ssl", "--port=8010"]
