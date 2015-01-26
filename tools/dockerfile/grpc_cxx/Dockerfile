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

CMD ["/var/local/git/grpc/bins/opt/interop_server", "--enable_ssl", "--port=8010"]
