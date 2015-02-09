# Dockerfile for gRPC Ruby
FROM grpc/ruby_base

# Build the C libary
RUN cd /var/local/git/grpc \
  && git pull --recurse-submodules \
  && git submodule update --init --recursive

# Build the C core.
RUN make install_c -C /var/local/git/grpc

# Build ruby gRPC and run its tests
RUN /bin/bash -l -c 'cd /var/local/git/grpc/src/ruby && bundle && rake'

# Add a cacerts directory containing the Google root pem file, allowing the
# ruby client to access the production test instance
ADD cacerts cacerts

# Add a service_account directory containing the auth creds file
ADD service_account service_account

# Specify the default command such that the interop server runs on its known
# testing port
CMD ["/bin/bash", "-l", "-c", "ruby /var/local/git/grpc/src/ruby/bin/interop/interop_server.rb --use_tls --port 8060"]
