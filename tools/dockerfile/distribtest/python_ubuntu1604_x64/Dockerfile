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

FROM ubuntu:16.04

RUN apt-get update -y && apt-get install -y python python-pip

# Necessary to fix virtualenv compatibility problems with python2.7
RUN pip install --upgrade pip==19.3.1

# Ubuntu's python-pip package installs pip to /usr/bin, whereas the upgraded
# pip lives at /usr/local/bin/pip. We'll use the absolute path for now, since
# this will all be replaced with python3.
RUN /usr/local/bin/pip install virtualenv
