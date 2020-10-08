# Copyright 2016 gRPC authors.
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

# Docker file for building gRPC artifacts.
# Updated: 2020-10-08

##################
# Base

FROM dockcross/manylinux2010-x64

# Install essential packages.
RUN yum -y install golang strace


##################
# Ruby dependencies

# Install rvm
RUN gpg --keyserver hkp://pool.sks-keyservers.net --recv-keys 409B6B1796C275462A1703113804BB82D39DC0E3 7D2BAF1CF37B13E2069D6956105BD0E739499BDB
RUN \curl -sSL https://get.rvm.io | bash -s stable

# Install Ruby 2.6
RUN /bin/bash -l -c "rvm install ruby-2.6"
RUN /bin/bash -l -c "rvm use --default ruby-2.6"
RUN /bin/bash -l -c "echo 'gem: --no-document' > ~/.gemrc"
RUN /bin/bash -l -c "echo 'export PATH=/usr/local/rvm/bin:$PATH' >> ~/.bashrc"
RUN /bin/bash -l -c "echo 'rvm --default use ruby-2.6' >> ~/.bashrc"
RUN /bin/bash -l -c "gem install bundler"

# Clean yum
RUN yum clean all

# Create default work directory.
RUN mkdir /var/local/jenkins

# Define the default command.
CMD ["bash"]
