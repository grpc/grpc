# Dockerfile for gRPC C++
FROM grpc/base

# Start the daemon that allows access to the protected git-on-borg repos
RUN /var/local/git/gcompute-tools/git-cookie-authdaemon

RUN git clone https://team.googlesource.com/one-platform-grpc-team/grpc /var/local/git/grpc
RUN cd /var/local/git/grpc \
  && git pull --recurse-submodules \
  && git submodule update --init --recursive

RUN make install -C /var/local/git/grpc

# Define the default command.
CMD ["bash"]