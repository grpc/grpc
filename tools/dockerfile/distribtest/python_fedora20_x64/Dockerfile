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

FROM fedora:20

RUN yum clean all && yum update -y && yum install -y python python-pip

# Upgrading six would fail because of docker issue when using overlay.
# Trying twice makes it work fine.
# https://github.com/docker/docker/issues/10180
RUN pip install --upgrade six || pip install --upgrade six

RUN pip install virtualenv
