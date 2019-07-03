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

FROM debian:wheezy

RUN apt-get update -y && apt-get install -y python python-pip

# Use --index-url to workaround
# https://stackoverflow.com/questions/21294997/pip-connection-failure-cannot-fetch-index-base-url-http-pypi-python-org-simpl
RUN pip install --index-url=https://pypi.python.org/simple/ virtualenv
