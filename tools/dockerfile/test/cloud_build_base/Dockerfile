# Copyright 2018 The gRPC Authors
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

FROM gcr.io/cloud-builders/docker
  
# Install Git and basic packages.
RUN apt-get update && apt-get install -y \
  git \
  python-dev \
  python-setuptools \
  python-yaml \
  unzip \
  wget \
  zip && apt-get clean

RUN apt-get update && apt-get install -y uuid-runtime && apt-get clean

# Install Python 2.7 (already installed!)
#RUN apt-get update && apt-get install -y python2.7 python-all-dev
RUN curl https://bootstrap.pypa.io/get-pip.py | python2.7

# Google Cloud platform API libraries
RUN pip install --upgrade google-api-python-client oauth2client

ENTRYPOINT [""]