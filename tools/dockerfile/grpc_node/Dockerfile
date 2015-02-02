# Dockerfile for gRPC Node
FROM grpc/node_base

# Update the C libary
RUN cd /var/local/git/grpc \
  && git pull --recurse-submodules \
  && git submodule update --init --recursive

# Install the C core.
RUN make clean -C /var/local/git/grpc
RUN make install_c -C /var/local/git/grpc

RUN cd /var/local/git/grpc/src/node && npm install && node-gyp rebuild

CMD ["/usr/bin/nodejs", "/var/local/git/grpc/src/node/interop/interop_server.js", "--use_tls=true", "--port 8040"]