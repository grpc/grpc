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

FROM golang:1.19

RUN apt-get update && apt-get install -y \
  dnsutils \
  git \
  vim \
  curl \
  python3 \
  python3-pip \
  python3-yaml \
  make && apt-get clean

RUN ln -s /usr/local/go/bin/go /usr/local/bin

# Install Python packages from PyPI
RUN python3 -m pip install --break-system-packages virtualenv
RUN python3 -m pip install --break-system-packages futures enum34 protobuf six twisted

# Define the default command.
CMD ["bash"]
