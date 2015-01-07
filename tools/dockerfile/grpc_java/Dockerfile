# Dockerfile for the gRPC Java dev image
FROM grpc/java_base

# Start the daemon that allows access to private git-on-borg repos
RUN /var/local/git/gcompute-tools/git-cookie-authdaemon

RUN  cd /var/local/git/grpc-java/lib/okhttp && \
  mvn -pl okhttp -am install
RUN  cd /var/local/git/grpc-java/lib/netty && \
  mvn -pl codec-http2 -am -DskipTests install
RUN cd /var/local/git/grpc-java && \
  protoc --version>ver.txt && \
  mvn install

# Specify the default command such that the interop server runs on its known testing port
CMD ["/var/local/git/grpc-java/run-test-server.sh", "--transport=NETTY_TLS", "--grpc_version=2", "--port=8030"]
