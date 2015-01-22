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
  libgtest-dev \
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

# Install a GitHub SSH service credential that gives access to the GitHub repo while it's private
# TODO: remove this once the repo is public
ADD .ssh .ssh
RUN chmod 600 .ssh/github.rsa
RUN mkdir -p $HOME/.ssh && echo 'Host github.com' > $HOME/.ssh/config
RUN echo "    IdentityFile /.ssh/github.rsa" >> $HOME/.ssh/config
RUN echo 'StrictHostKeyChecking no' >> $HOME/.ssh/config

# Define the default command.
CMD ["bash"]
