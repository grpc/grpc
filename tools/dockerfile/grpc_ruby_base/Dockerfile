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


# Start the daemon that allows access to the protected git-on-borg repos
RUN /var/local/git/gcompute-tools/git-cookie-authdaemon

# Download the patched Ruby protobuf (beefcake) so that Ruby gRPC clients can
# be generated from proto3 schemas.
RUN git clone https://team.googlesource.com/one-platform-grpc-team/grpc-ruby-beefcake \
              /var/local/git/beefcake

# Install RVM, use this to install ruby
RUN gpg --keyserver hkp://keys.gnupg.net --recv-keys D39DC0E3  # Needed for RVM
RUN /bin/bash -l -c "curl -L get.rvm.io | bash -s stable"

# Install Ruby 2.1
RUN /bin/bash -l -c "rvm install ruby-2.1"
RUN /bin/bash -l -c "rvm use --default ruby-2.1"
RUN /bin/bash -l -c "echo 'gem: --no-ri --no-rdoc' > ~/.gemrc"
RUN /bin/bash -l -c "echo 'source /home/grpc_ruby/.rvm/scripts/rvm' >> ~/.bashrc"
RUN /bin/bash -l -c "echo 'rvm --default use ruby-2.1' >> ~/.bashrc"
RUN /bin/bash -l -c "gem install bundler --no-ri --no-rdoc"

RUN git clone https://team.googlesource.com/one-platform-grpc-team/grpc /var/local/git/grpc
RUN cd /var/local/git/grpc \
  && git submodule update --init --recursive

RUN make static_c shared_c -C /var/local/git/grpc