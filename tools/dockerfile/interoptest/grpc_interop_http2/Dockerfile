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

FROM golang:latest

# Using login shell removes Go from path, so we add it.
RUN ln -s /usr/local/go/bin/go /usr/local/bin

#====================
# Python dependencies to run tools/run_tests scripts

# Install dependencies

RUN apt-get update && apt-get install -y python3-all-dev python3-pip

# Install Python packages from PyPI
RUN python3 -m pip install --upgrade pip==19.3.1
RUN python3 -m pip install virtualenv==16.7.9
RUN python3 -m pip install futures==3.1.1 enum34==1.1.10 protobuf==3.5.2.post1 six==1.16.0 twisted==19.10.0

RUN python3 -m pip install twisted h2==2.6.1 hyper

# Define the default command.
CMD ["bash"]
