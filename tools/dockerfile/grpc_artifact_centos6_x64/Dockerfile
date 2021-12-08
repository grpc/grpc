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
# Updated: 2021-08-23

##################
# Base

FROM dockcross/manylinux2010-x64:20210210-84c47e5

# Install essential packages.
RUN yum -y install strace && yum clean all

##################
# Ruby dependencies

# Install rvm
RUN curl -sSL https://rvm.io/mpapis.asc | gpg --import -
RUN curl -sSL https://rvm.io/pkuczynski.asc | gpg --import -
# Use "--insecure" to avoid cert expiration error
RUN curl -sSL --insecure https://get.rvm.io | bash -s stable

# Install Ruby 2.6
RUN /bin/bash -l -c "rvm install ruby-2.6"
RUN /bin/bash -l -c "rvm use --default ruby-2.6"
RUN /bin/bash -l -c "echo 'gem: --no-document' > ~/.gemrc"
RUN /bin/bash -l -c "echo 'export PATH=/usr/local/rvm/bin:$PATH' >> ~/.bashrc"
RUN /bin/bash -l -c "echo 'rvm --default use ruby-2.6' >> ~/.bashrc"
RUN /bin/bash -l -c "gem install bundler"

# Create default work directory.
RUN mkdir /var/local/jenkins

# Define the default command.
CMD ["bash"]
