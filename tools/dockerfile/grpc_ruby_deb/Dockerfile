# Dockerfile for gRPC Ruby, but using Debian packages for gRPC C core.
FROM grpc/ruby_base

# Pull the latest sources
RUN cd /var/local/git/grpc \
  && git pull --recurse-submodules \
  && git submodule update --init --recursive

# Make sure we don't rely on things that shouldn't be there.
RUN make clean -C /var/local/git/grpc

# Debian packages need to be supplied externally
ADD libgrpc_amd64.deb libgrpc_amd64.deb
ADD libgrpc-dev_amd64.deb libgrpc-dev_amd64.deb

# Install the C core .deb packages
RUN /bin/bash -l -c 'dpkg -i libgrpc_amd64.deb libgrpc-dev_amd64.deb'

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
