# Base Dockerfile for GRPC Python.
#
# Includes Python environment and installation dependencies.
FROM grpc/base

# Allows 'source' to work
RUN rm /bin/sh && ln -s /bin/bash /bin/sh

# Install Python development
RUN apt-get update && apt-get install -y \
    python-all-dev \
    python3-all-dev \
    python-pip \
    python-virtualenv

# Install Python packages from PyPI
RUN pip install futures==2.2.0 enum34==1.0.4 protobuf==3.0.0-alpha-1

# Get the GRPC source from GitHub
RUN git clone --recursive git@github.com:grpc/grpc.git /var/local/git/grpc
