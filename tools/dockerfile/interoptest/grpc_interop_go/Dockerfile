# Copyright 2021 The gRPC Authors
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

FROM golang:latest

# Using login shell removes Go from path, so we add it.
RUN ln -s /usr/local/go/bin/go /usr/local/bin

#====================
# run_tests.py python dependencies

# Basic python dependencies to be able to run tools/run_tests python scripts
# These dependencies are not sufficient to build gRPC Python, gRPC Python
# deps are defined elsewhere (e.g. python_deps.include)
# This template is similar to run_tests_python_deps.include but installs the
# dependencies in a python virtual environment. This is required from pip 23.0
# onwards as installation of system-wide packages is blocked by default.
RUN apt-get update && apt-get install -y \
  python3 \
  python3-pip \
  python3-setuptools \
  python3-yaml \
  python3-venv \
  && apt-get clean

# Create a python virtual env for installing required dependencies.
ENV PYTHON_ENV="/opt/venv/grpc-interop"
RUN python3 -m venv "$PYTHON_ENV"
RUN echo "source $PYTHON_ENV/bin/activate" >> ~/.bashrc

# Use bash explicitly as /bin/sh doesn't support the "source" command.
# use pinned version of pip to avoid sudden breakages
# TODO(#36814): currently six is needed for tools/run_tests scripts
# but since our python2 usage is deprecated, we should get rid of it.
RUN /bin/bash -c "source $PYTHON_ENV/bin/activate &&   python3 -m pip install --upgrade pip==19.3.1 &&   python3 -m pip install six==1.16.0"

# Install Google Cloud Platform API libraries
# These are needed for uploading test results to BigQuery (e.g. by tools/run_tests scripts)
# This template is similar to gcp_api_libraries.include but installs the
# dependencies in a python virtual environment. This is required from pip 23.0
# onwards as installation of system-wide packages is blocked by default.
RUN /bin/bash -c "source $PYTHON_ENV/bin/activate &&   python3 -m pip install --upgrade google-auth==1.23.0 google-api-python-client==1.12.8 oauth2client==4.1.0"


# Define the default command.
CMD ["bash"]
