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

FROM opensuse:42.1

RUN zypper --non-interactive install curl

RUN zypper --non-interactive install tar which

RUN zypper --non-interactive install ca-certificates-mozilla

# Install rvm
RUN gpg --keyserver hkp://keys.gnupg.net --recv-keys 409B6B1796C275462A1703113804BB82D39DC0E3
RUN \curl -sSL https://get.rvm.io | bash -s stable --ruby

# OpenSUSE is a bit crazy and ignores .bashrc for login shell.
RUN /bin/bash -l -c "echo '. /etc/profile.d/rvm.sh' >> ~/.profile"

RUN /bin/bash -l -c 'gem install --update bundler'
