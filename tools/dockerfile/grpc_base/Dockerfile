# Base Dockerfile for gRPC dev images
FROM debian:latest

# Install Git.
RUN apt-get update && apt-get install -y \
  autoconf \
  autotools-dev \
  build-essential \
  bzip2 \
  curl \
  gcc \
  git \
  libc6 \
  libc6-dbg \
  libc6-dev \
  libevent-dev \
  libtool \
  make \
  strace \
  python-dev \
  python-setuptools \
  telnet \
  unzip \
  wget \
  zip && apt-get clean

# Install useful useful python modules
RUN easy_install -U pip
RUN pip install -U crcmod  # makes downloads from cloud storage faster

# Install GCloud
RUN wget https://dl.google.com/dl/cloudsdk/release/google-cloud-sdk.zip \
  && unzip google-cloud-sdk.zip && rm google-cloud-sdk.zip
ENV CLOUD_SDK /google-cloud-sdk
RUN $CLOUD_SDK/install.sh --usage-reporting=true --path-update=true --bash-completion=true --rc-path=/.bashrc --disable-installation-options
ENV PATH $CLOUD_SDK/bin:$PATH

# Install gcompute-tools to allow access to private git-on-borg repos
RUN git clone https://gerrit.googlesource.com/gcompute-tools /var/local/git/gcompute-tools

# Start the daemon that allows access to private git-on-borg repos
RUN /var/local/git/gcompute-tools/git-cookie-authdaemon

# Install the grpc-tools scripts dir from git
RUN git clone https://team.googlesource.com/one-platform-grpc-team/grpc-tools /var/local/git/grpc-tools

# Install the grpc-protobuf dir that has the protoc patch
RUN git clone https://team.googlesource.com/one-platform-grpc-team/protobuf /var/local/git/protobuf

# Install the patched version of protoc
RUN cd /var/local/git/protobuf && \
  ./autogen.sh && \
  ./configure --prefix=/usr && \
  make && make check && make install && make clean

# Define the default command.
CMD ["bash"]
