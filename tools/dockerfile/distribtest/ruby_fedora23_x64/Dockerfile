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

FROM fedora:23

# Make yum work properly under docker when using overlay storage driver.
# https://bugzilla.redhat.com/show_bug.cgi?id=1213602#c9
# https://github.com/docker/docker/issues/10180
RUN yum install -y yum-plugin-ovl

# distro-sync and install openssl, per https://github.com/fedora-cloud/docker-brew-fedora/issues/19
RUN yum clean all && yum update -y && yum distro-sync -y && yum install -y openssl gnupg which findutils tar procps

# Install rvm
RUN gpg --keyserver hkp://keys.gnupg.net --recv-keys 409B6B1796C275462A1703113804BB82D39DC0E3 7D2BAF1CF37B13E2069D6956105BD0E739499BDB
RUN curl -sSL https://get.rvm.io | bash -s stable

# Install Ruby 2.3
RUN /bin/bash -l -c "rvm install ruby-2.4.9"
RUN /bin/bash -l -c "rvm use --default ruby-2.4.9"
RUN /bin/bash -l -c "echo 'gem: --no-document' > ~/.gemrc"
RUN /bin/bash -l -c "echo 'export PATH=/usr/local/rvm/bin:$PATH' >> ~/.bashrc"
RUN /bin/bash -l -c "echo 'rvm --default use ruby-2.4.9' >> ~/.bashrc"
RUN /bin/bash -l -c "gem install bundler -v 1.17.3 --no-document"

RUN mkdir /var/local/jenkins

RUN /bin/bash -l -c "echo '. /etc/profile.d/rvm.sh' >> ~/.bashrc"
