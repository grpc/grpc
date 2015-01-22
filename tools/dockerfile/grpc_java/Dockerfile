# Dockerfile for the gRPC Java dev image
FROM grpc/java_base

RUN  cd /var/local/git/grpc-java/lib/okhttp && \
  mvn -pl okhttp -am install
RUN  cd /var/local/git/grpc-java/lib/netty && \
  mvn -pl codec-http2 -am -DskipTests install
RUN cd /var/local/git/grpc-java && \
  protoc --version>ver.txt && \
  mvn install

# Specify the default command such that the interop server runs on its known testing port
CMD ["/var/local/git/grpc-java/run-test-server.sh", "--transport=HTTP2_NETTY_TLS", "--grpc_version=2", "--port=8030"]
