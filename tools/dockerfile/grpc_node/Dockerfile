# Dockerfile for gRPC Node
FROM grpc/node_base

# Update the C libary
RUN cd /var/local/git/grpc \
  && git pull --recurse-submodules \
  && git submodule update --init --recursive

# Install the C core.
RUN make install_c -C /var/local/git/grpc

RUN cd /var/local/git/grpc/src/node && npm install && node-gyp rebuild

# Add a cacerts directory containing the Google root pem file, allowing the
# ruby client to access the production test instance
ADD cacerts cacerts

# Add a service_account directory containing the auth creds file
ADD service_account service_account

CMD ["/usr/bin/nodejs", "/var/local/git/grpc/src/node/interop/interop_server.js", "--use_tls=true", "--port=8040"]