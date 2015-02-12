# Base Dockerfile for the gRPC Java dev image
FROM grpc/base

# Install JDK 8
#
# TODO(temiola): simplify this if/when a simpler process is available.
#
RUN echo oracle-java8-installer shared/accepted-oracle-license-v1-1 select true | /usr/bin/debconf-set-selections
RUN echo "deb http://ppa.launchpad.net/webupd8team/java/ubuntu trusty main" | tee /etc/apt/sources.list.d/webupd8team-java.list
RUN echo "deb-src http://ppa.launchpad.net/webupd8team/java/ubuntu trusty main" | tee -a /etc/apt/sources.list.d/webupd8team-java.list
RUN apt-key adv --keyserver hkp://keyserver.ubuntu.com:80 --recv-keys EEA14886
RUN apt-get update && apt-get -y install oracle-java8-installer && \
  apt-get clean && rm -r /var/cache/oracle-jdk8-installer/

# Install maven
RUN wget -O - http://mirror.olnevhost.net/pub/apache/maven/binaries/apache-maven-3.2.1-bin.tar.gz | \
  tar xz -C /var/local

ENV JAVA_HOME /usr/lib/jvm/java-8-oracle
ENV M2_HOME /var/local/apache-maven-3.2.1
ENV PATH $PATH:$JAVA_HOME/bin:$M2_HOME/bin
ENV LD_LIBRARY_PATH /usr/local/lib

# Get the protobuf source from GitHub and install it
RUN wget -O - https://github.com/google/protobuf/archive/master.tar.gz | \
  tar xz && \
  cd protobuf-master && \
  ./autogen.sh && \
  ./configure --prefix=/usr && \
  make -j12 && make check && make install && \
  cd java && mvn install && cd .. && \
  rm -r "$(pwd)"

# Install a GitHub SSH service credential that gives access to the GitHub repo while it's private
# TODO: remove this once the repo is public
COPY .ssh/github.rsa /root/.ssh/id_rsa
RUN echo 'Host github.com\nStrictHostKeyChecking no' > /root/.ssh/config

# Trigger download of as many Maven and Gradle artifacts as possible. We don't build grpc-java
# because we don't want to install netty
RUN git clone --recursive --depth 1 git@github.com:grpc/grpc-java.git && \
  cd grpc-java/lib/netty && \
  mvn -pl codec-http2 -am -DskipTests verify && \
  cd ../.. && \
  ./gradlew && \
  rm -r "$(pwd)"
