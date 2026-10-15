# Copyright 2021 gRPC authors.
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

FROM debian:13

RUN apt update && apt -y upgrade && apt install -y build-essential ccache curl git time wget zip && apt clean
#=================
# Setup git to access working directory across docker boundary.
# This avoids the "fatal: detected dubious ownership in repository XYZ"
# git error.

RUN git config --global --add safe.directory '*'
RUN git config --global protocol.file.allow always

#====================
# run_tests.py python dependencies

# Basic python dependencies to be able to run tools/run_tests python scripts
# These dependencies are NOT for building gRPC Python, which are defined
# elsewhere (requirements.txt, individual docker images, build scripts, etc).
RUN apt-get update && apt-get install -y \
  python3 \
  python3-pip \
  python3-venv \
  && apt-get clean

# Use system python to bootstrap virtualenv and activate it
ENV VIRTUAL_ENV=/opt/venv
RUN python3 -m venv $VIRTUAL_ENV
ENV PATH="$VIRTUAL_ENV/bin:$PATH"

# Persist this to /etc/profile.d because some of our tests
# (e.g. tools/run_tests/dockerize/build_and_run_docker.sh)
# use `bash -l` to launch tests and override Dockerfile's
# ENV directives.
RUN echo "export PATH=\"$VIRTUAL_ENV/bin:\$PATH\"" > /etc/profile.d/venv.sh

# Install/upgrade basic requirements inside the virtualenv
RUN pip install --upgrade pip setuptools pyyaml

# Google Cloud Platform API libraries
# These are needed for uploading test results to BigQuery (e.g. by tools/run_tests scripts)
RUN python3 -m pip install --upgrade google-auth==1.23.0 google-api-python-client==1.12.8 oauth2client==4.1.0


#=================
# C++ dependencies
RUN apt-get update && apt-get -y install libc++-dev clang && apt-get clean

RUN clang --version | grep -Eq "clang version 19\.[0-9]+"
# Some cxx tests depend on the twisted package
RUN python3 -m pip install twisted

#=================
# Install cmake
# Note that this step should be only used for distributions that have new enough cmake to satisfy gRPC's cmake version requirement.

RUN apt-get update && apt-get install -y cmake && apt-get clean


RUN mkdir /var/local/jenkins


# Define the default command.
CMD ["bash"]
