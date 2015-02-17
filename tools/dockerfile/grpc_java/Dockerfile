# Dockerfile for the gRPC Java dev image
FROM grpc/java_base

RUN git clone --recursive --depth 1 git@github.com:grpc/grpc-java.git /var/local/git/grpc-java
RUN cd /var/local/git/grpc-java/lib/netty && \
  mvn -pl codec-http2 -am -DskipTests install clean
RUN cd /var/local/git/grpc-java && \
  ./gradlew build

# Specify the default command such that the interop server runs on its known testing port
CMD ["/var/local/git/grpc-java/run-test-server.sh", "--use_tls=true", "--port=8030"]
