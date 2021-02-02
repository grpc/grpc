# Copyright 2015 gRPC authors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Pinned version of the base image is used to avoid regressions caused
# by rebuilding of this docker image. To see available versions, you can run
# "gcloud container images list-tags gcr.io/oss-fuzz-base/base-builder"
# TODO(jtattermusch): with the latest version we'd get clang12+
# which makes our build fail due to new warnings being treated
# as errors.
FROM gcr.io/oss-fuzz-base/base-builder@sha256:de220fd2433cd53bd06b215770dcd14a5e74632e0215acea7401fee8cafb18da

# -------------------------- WARNING --------------------------------------
# If you are making changes to this file, consider changing
# https://github.com/google/oss-fuzz/blob/master/projects/grpc/Dockerfile
# accordingly.
# -------------------------------------------------------------------------

# Install basic packages and Bazel dependencies.
RUN apt-get update && apt-get install -y software-properties-common python-software-properties
RUN add-apt-repository ppa:webupd8team/java
RUN apt-get update && apt-get -y install \
  autoconf \
  build-essential \
  curl \
  wget \
  libtool \
  make \
  openjdk-8-jdk \
  vim

#====================
# Python dependencies

# Install dependencies

RUN apt-get update && apt-get install -y \
    python-all-dev \
    python3-all-dev \
    python-setuptools

# Install Python packages from PyPI
RUN curl https://bootstrap.pypa.io/2.7/get-pip.py | python2.7
RUN pip install --upgrade pip==19.3.1
RUN pip install virtualenv==16.7.9
RUN pip install futures==2.2.0 enum34==1.0.4 protobuf==3.5.2.post1 six==1.15.0 twisted==17.5.0


#=================
# Compile CPython 3.6.9 from source

RUN apt-get update && apt-get install -y zlib1g-dev libssl-dev
RUN apt-get update && apt-get install -y jq build-essential libffi-dev

RUN cd /tmp && \
    wget -q https://www.python.org/ftp/python/3.6.9/Python-3.6.9.tgz && \
    tar xzvf Python-3.6.9.tgz && \
    cd Python-3.6.9 && \
    ./configure && \
    make install

RUN cd /tmp && \
    echo "ff7cdaef4846c89c1ec0d7b709bbd54d Python-3.6.9.tgz" > checksum.md5 && \
    md5sum -c checksum.md5

RUN python3.6 -m ensurepip && \
    python3.6 -m pip install coverage


#========================
# Bazel installation

# Must be in sync with tools/bazel
ENV BAZEL_VERSION 3.7.1

# The correct bazel version is already preinstalled, no need to use //tools/bazel wrapper.
ENV DISABLE_BAZEL_WRAPPER 1

RUN apt-get update && apt-get install -y wget && apt-get clean
RUN wget "https://github.com/bazelbuild/bazel/releases/download/$BAZEL_VERSION/bazel-$BAZEL_VERSION-installer-linux-x86_64.sh" && \
  bash ./bazel-$BAZEL_VERSION-installer-linux-x86_64.sh && \
  rm bazel-$BAZEL_VERSION-installer-linux-x86_64.sh


#=================
# Make it possible to run processes inside docker under current user's UID and GID.
#
# Normally, processes under a docker container run as root, which makes using docker for testing harder.
# Ideally, we'd like to make it easy to share files (source code, build outputs) between the docker container and the parent test script
# by mounting the workspace directory as a docker container's volume, but when docker container is running as root, files created
# by the docker container will be also owned by root, which pollutes the workspace.
# A simple solution is to use "docker run --user UID:GID", but with that setup, the UID under the docker container
# becomes HOME-less and nameless (as there is no /etc/passwd entry for given UID), which isn't ideal for running
# some tests (e.g. python tests have trouble running under nameless user).
# The solution is to setup an entrypoint script that creates the corresponding user and group
# and then executes a command under given UID and GID.
# Note that similar trick is employed by other convenience tools for building/testing with docker.
# (e.g. https://github.com/dockcross/dockcross/)

# The regular "su" command doesn't work for us because it has limitations in terms of handling TTY.
ENV GOSU_VERSION=1.10
RUN curl -o /usr/local/bin/gosu -SL "https://github.com/tianon/gosu/releases/download/$GOSU_VERSION/gosu-amd64"
RUN chmod +x /usr/local/bin/gosu

# Create nonroot_entrypoint.sh script. The script will look for BUILDER_UID and BUILDER_GID environment variables
# and if set, it will create a new user BUILDER_USER and a group BUILDER_GROUP inside the docker container.
# The command will be run under this newly created user.
RUN echo >/nonroot_entrypoint.sh '#!/bin/bash' \
    && echo >>/nonroot_entrypoint.sh 'set -e' \
    && echo >>/nonroot_entrypoint.sh 'if [[ -n $BUILDER_UID ]] && [[ -n $BUILDER_GID ]]' \
    && echo >>/nonroot_entrypoint.sh 'then' \
    && echo >>/nonroot_entrypoint.sh '    groupadd -o -g $BUILDER_GID $BUILDER_GROUP 2> /dev/null' \
    && echo >>/nonroot_entrypoint.sh '    useradd -o -m -g $BUILDER_GID -u $BUILDER_UID $BUILDER_USER 2> /dev/null' \
    && echo >>/nonroot_entrypoint.sh '    # Enable passwordless sudo capabilities for the user' \
    && echo >>/nonroot_entrypoint.sh '    chown root:$BUILDER_GID $(which gosu)' \
    && echo >>/nonroot_entrypoint.sh '    chmod +s $(which gosu); sync' \
    && echo >>/nonroot_entrypoint.sh '    # Run the command as the specified user/group.' \
    && echo >>/nonroot_entrypoint.sh '    exec gosu $BUILDER_UID:$BUILDER_GID "$@"' \
    && echo >>/nonroot_entrypoint.sh 'else' \
    && echo >>/nonroot_entrypoint.sh '    # Just run the command as root.' \
    && echo >>/nonroot_entrypoint.sh '    exec "$@"' \
    && echo >>/nonroot_entrypoint.sh 'fi'
RUN chmod u+x /nonroot_entrypoint.sh
ENTRYPOINT ["/nonroot_entrypoint.sh"]


RUN mkdir -p /var/local/jenkins

# Define the default command.
CMD ["bash"]
