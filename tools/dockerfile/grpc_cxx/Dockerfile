# Dockerfile for gRPC C++
FROM grpc/base

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
RUN make install -C /var/local/git/grpc

# Define the default command.
CMD ["bash"]
