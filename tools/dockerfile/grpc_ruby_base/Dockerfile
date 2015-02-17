# Base Dockerfile for gRPC Ruby.
#
# Includes Ruby installation dependencies, things that are unlikely to vary.
FROM grpc/base

# Allows 'source' to work
RUN rm /bin/sh && ln -s /bin/bash /bin/sh

# Install RVM dependencies
RUN apt-get update && apt-get install -y \
    autoconf \
    automake \
    bison \
    curl \
    g++ \
    gawk \
    gcc \
    libc6-dev \
    libffi-dev \
    libgdbm-dev \
    libncurses5-dev \
    libreadline6-dev \
    libsqlite3-dev \
    libssl-dev \
    libtool \
    libyaml-dev \
    make \
    patch \
    pkg-config \
    procps \
    sqlite3 \
    zlib1g-dev

# Install RVM, use this to install ruby
RUN gpg --keyserver hkp://keys.gnupg.net --recv-keys D39DC0E3  # Needed for RVM
RUN /bin/bash -l -c "curl -L get.rvm.io | bash -s stable"

# Install Ruby 2.1
RUN /bin/bash -l -c "rvm install ruby-2.1"
RUN /bin/bash -l -c "rvm use --default ruby-2.1"
RUN /bin/bash -l -c "echo 'gem: --no-ri --no-rdoc' > ~/.gemrc"
RUN /bin/bash -l -c "echo 'rvm --default use ruby-2.1' >> ~/.bashrc"
RUN /bin/bash -l -c "gem install bundler --no-ri --no-rdoc"

# Get the source from GitHub
RUN git clone git@github.com:grpc/grpc.git /var/local/git/grpc
RUN cd /var/local/git/grpc && \
  git pull --recurse-submodules && \
  git submodule update --init --recursive

# Build and install the protobuf library
RUN cd /var/local/git/grpc/third_party/protobuf && \
  ./autogen.sh && \
  ./configure --prefix=/usr && \
  make -j12 && make check && make install && make clean

# Build the C core
RUN make static_c shared_c -j12 -C /var/local/git/grpc
