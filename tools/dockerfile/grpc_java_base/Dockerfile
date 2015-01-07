# Base Dockerfile for the gRPC Java dev image
FROM grpc/base

RUN apt-get update && apt-get -y install java7-jdk

# Install maven
RUN wget http://mirror.olnevhost.net/pub/apache/maven/binaries/apache-maven-3.2.1-bin.tar.gz && \
  tar xvf apache-maven-3.2.1-bin.tar.gz -C /var/local

ENV JAVA_HOME /usr/lib/jvm/java-7-openjdk-amd64
ENV M2_HOME /var/local/apache-maven-3.2.1
ENV PATH $PATH:$JAVA_HOME/bin:$M2_HOME/bin
ENV LD_LIBRARY_PATH /usr/local/lib

# Start the daemon that allows access to the protected git-on-borg repos
RUN /var/local/git/gcompute-tools/git-cookie-authdaemon

RUN git clone --recursive https://team.googlesource.com/one-platform-grpc-team/grpc-java /var/local/git/grpc-java

RUN cd /var/local/git/grpc-java/lib/okhttp && \
  mvn -pl okhttp -am validate
RUN cd /var/local/git/grpc-java/lib/netty && \
  mvn -pl codec-http2 -am validate
RUN cd /var/local/git/grpc-java && \
  mvn validate