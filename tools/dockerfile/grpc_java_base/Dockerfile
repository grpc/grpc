# Copyright 2015, Google Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#     * Neither the name of Google Inc. nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

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
RUN wget -O - https://github.com/google/protobuf/archive/v3.0.0-alpha-2.tar.gz | \
  tar xz && \
  cd protobuf-3.0.0-alpha-2 && \
  ./autogen.sh && \
  ./configure --prefix=/usr && \
  make -j12 && make check && make install && \
  cd java && mvn install && cd .. && \
  cd javanano && mvn install && cd .. && \
  rm -r "$(pwd)"

# Trigger download of as many Maven and Gradle artifacts as possible. We don't build grpc-java
# because we don't want to install netty
RUN git clone --recursive --depth 1 https://github.com/grpc/grpc-java.git && \
  cd grpc-java/lib/netty && \
  mvn -pl codec-http2 -am -DskipTests verify && \
  cd ../.. && \
  ./gradlew && \
  rm -r "$(pwd)"
