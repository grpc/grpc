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
# Image(8a3a7b8072a9) is built on Sep 5, 2022
FROM gcr.io/oss-fuzz-base/base-builder@sha256:8a3a7b8072a93ba737b02e0c0941903f5408b4def6333fdc823b77aede5b2afd

# -------------------------- WARNING --------------------------------------
# If you are making changes to this file, consider changing
# https://github.com/google/oss-fuzz/blob/master/projects/grpc/Dockerfile
# accordingly.
# -------------------------------------------------------------------------

# Install basic packages
RUN apt-get update && apt-get -y install \
  autoconf \
  build-essential \
  curl \
  libtool \
  make \
  vim \
  wget

#========================
# Bazel installation

# Must be in sync with tools/bazel
ENV BAZEL_VERSION 5.3.0

# The correct bazel version is already preinstalled, no need to use //tools/bazel wrapper.
ENV DISABLE_BAZEL_WRAPPER 1

# Download the correct bazel version and make sure it's on path.
RUN BAZEL_ARCH_SUFFIX="$(uname -m | sed s/aarch64/arm64/)" \
  && curl -sSL --fail -o /usr/local/bin/bazel "https://github.com/bazelbuild/bazel/releases/download/$BAZEL_VERSION/bazel-$BAZEL_VERSION-linux-$BAZEL_ARCH_SUFFIX" \
  && chmod a+x /usr/local/bin/bazel

# Normally we would run "bazel --version" here to make sure bazel
# was correctly installed, but we can't do that because
# of # https://github.com/bazelbuild/bazel/issues/11379.
# We want to keep the arm64 version of the image buildable
# on x64 with use of qemu-user-static & binfmt emulation,
# but the self-extraction is broken for bazel currently.
# The binary will work correctly when run on real arm64
# hardware, when qemu-user-static isn't getting into the way.


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

RUN git config --global --add safe.directory /var/local/jenkins/grpc

# Define the default command.
CMD ["bash"]
