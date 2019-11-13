FROM debian:buster

ENV BORINGSSL_COMMIT=master

RUN apt-get update && apt-get install -y \
  autoconf \
  cmake \
  curl \
  g++ \
  gcc \
  git \
  gnupg \
  golang \
  perl

COPY generate_boringssl_prefix_header.sh /

VOLUME /output

WORKDIR /

ENTRYPOINT ["/generate_boringssl_prefix_header.sh"]
