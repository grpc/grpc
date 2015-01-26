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
RUN apt-get update && apt-get -y install oracle-java8-installer

# Install maven
RUN wget http://mirror.olnevhost.net/pub/apache/maven/binaries/apache-maven-3.2.1-bin.tar.gz && \
  tar xvf apache-maven-3.2.1-bin.tar.gz -C /var/local

ENV JAVA_HOME /usr/lib/jvm/java-8-oracle
ENV M2_HOME /var/local/apache-maven-3.2.1
ENV PATH $PATH:$JAVA_HOME/bin:$M2_HOME/bin
ENV LD_LIBRARY_PATH /usr/local/lib

# Install a GitHub SSH service credential that gives access to the GitHub repo while it's private
# TODO: remove this once the repo is public
ADD .ssh .ssh
RUN chmod 600 .ssh/github.rsa
RUN mkdir -p $HOME/.ssh && echo 'Host github.com' > $HOME/.ssh/config
RUN echo "    IdentityFile /.ssh/github.rsa" >> $HOME/.ssh/config
RUN echo 'StrictHostKeyChecking no' >> $HOME/.ssh/config

# Get the protobuf source from GitHub and install it
RUN git clone --recursive --branch v2.6.1 git@github.com:google/protobuf.git /var/local/git/protobuf
RUN cd /var/local/git/protobuf && \
  ./autogen.sh && \
  ./configure --prefix=/usr && \
  make -j12 && make check && make install && make clean

# Get the source from GitHub
RUN git clone --recursive git@github.com:google/grpc-java.git /var/local/git/grpc-java

RUN cd /var/local/git/grpc-java/lib/okhttp && \
  mvn -pl okhttp -am validate
RUN cd /var/local/git/grpc-java/lib/netty && \
  mvn -pl codec-http2 -am validate
RUN cd /var/local/git/grpc-java && \
  mvn validate
