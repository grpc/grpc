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

FROM centos:6

RUN yum install -y curl

RUN yum install -y tar which

# Install rvm
RUN gpg --keyserver hkp://keys.gnupg.net --recv-keys 409B6B1796C275462A1703113804BB82D39DC0E3
# Running the installation twice to work around docker issue when using overlay.
# https://github.com/docker/docker/issues/10180
RUN (curl -sSL https://get.rvm.io | bash -s stable --ruby) || (curl -sSL https://get.rvm.io | bash -s stable --ruby)

RUN /bin/bash -l -c "echo '. /etc/profile.d/rvm.sh' >> ~/.bashrc"
RUN /bin/bash -l -c "gem install --update bundler"
