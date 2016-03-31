# Copyright 2015, Google Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#     * Neither the name of Google Inc. nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

# A work-in-progress Dockerfile that allows running gRPC homebrew
# installations inside docker containers
FROM debian:jessie

# Core dependencies
RUN apt-get update && apt-get install -y \
  bzip2 curl git ruby wget

# Install linuxbrew
ENV PATH /home/linuxbrew/.linuxbrew/bin:/home/linuxbrew/.linuxbrew/sbin:$PATH
RUN git clone https://github.com/Homebrew/linuxbrew.git /home/linuxbrew/.linuxbrew
RUN brew doctor || true

# Python dependency
RUN apt-get update && apt-get install -y python-dev
RUN curl https://bootstrap.pypa.io/get-pip.py | python

# NodeJS dependency
RUN touch .profile
RUN curl -o- https://raw.githubusercontent.com/creationix/nvm/v0.25.4/install.sh | bash
RUN /bin/bash -l -c "nvm install 0.12"

# Ruby dependency
RUN gpg --keyserver hkp://keys.gnupg.net --recv-keys 409B6B1796C275462A1703113804BB82D39DC0E3
RUN /bin/bash -l -c "\curl -sSL https://get.rvm.io | bash -s stable"
RUN /bin/bash -l -c "rvm install ruby-2.1"

# PHP dependency
RUN apt-get update && apt-get install -y php5 php5-dev php-pear phpunit unzip

RUN /bin/bash -l -c "echo 'export PATH=/home/linuxbrew/.linuxbrew/bin:\$PATH' >> ~/.bashrc"

CMD ["bash"]
