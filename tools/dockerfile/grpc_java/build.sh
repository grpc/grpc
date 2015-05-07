#!/bin/bash
rm -rf /var/local/git
cp -R /var/local/git-clone /var/local/git
cd /var/local/git/grpc-java/lib/netty && \
  mvn -pl codec-http2 -am -DskipTests install clean
cd /var/local/git/grpc-java && \
  ./gradlew build installDist

echo 'build finished'
