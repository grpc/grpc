# Dockerfile to build Debian packages for gRPC C core.
FROM grpc/base

# Install dependencies
RUN apt-get update && apt-get install -y lintian

# Get the source from GitHub
RUN git clone git@github.com:grpc/grpc.git /var/local/git/grpc
RUN cd /var/local/git/grpc && \
  git pull --recurse-submodules && \
  git submodule update --init --recursive

RUN /bin/bash -l -c 'cd /var/local/git/grpc/tools/distpackages && ./build_deb_packages.sh'
