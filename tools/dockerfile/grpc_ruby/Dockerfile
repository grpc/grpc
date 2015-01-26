# Dockerfile for gRPC Ruby
FROM grpc/ruby_base

# Build the C libary
RUN cd /var/local/git/grpc \
  && git pull --recurse-submodules \
  && git submodule update --init --recursive

# Build the C core.
RUN make install_c -C /var/local/git/grpc

# Install the grpc gem locally with its dependencies and build the extension
RUN /bin/bash -l -c 'cd /var/local/git/grpc/src/ruby && bundle && rake compile:grpc && gem build grpc.gemspec && gem install grpc'

# TODO add a command to run the unittest tests when the bug below is fixed
# - the tests fail due to an error in the C threading library:
#   they fail with 'ruby: __pthread_mutex_cond_lock_adjust for unknown reasons' at the end of a testcase
# - however, the interop server and client run OK, so this bug can be investigated
# RUN /bin/bash -l -c 'cd /var/local/git/grpc/src/ruby && bundle && rake'

# Specify the default command such that the interop server runs on its known testing port
CMD ["/bin/bash", "-l", "-c", "ruby /var/local/git/grpc/src/ruby/bin/interop/interop_server.rb --port 8060"]
