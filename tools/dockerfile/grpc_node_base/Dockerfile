# Base Dockerfile for gRPC Node.
#
# Includes Node installation dependencies
FROM grpc/base

RUN curl -sL https://deb.nodesource.com/setup | bash -

RUN apt-get update && apt-get install -y nodejs

RUN npm install -g node-gyp

# Get the source from GitHub, this gets the protobuf library as well
RUN git clone git@github.com:google/grpc.git /var/local/git/grpc
RUN cd /var/local/git/grpc && \
  git pull --recurse-submodules && \
  git submodule update --init --recursive

# Define the default command.
CMD ["bash"]