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

# Dockerfile for gRPC Ruby, but using Debian packages for gRPC C core.
FROM grpc/ruby_base

# Pull the latest sources
RUN cd /var/local/git/grpc \
  && git pull --recurse-submodules \
  && git submodule update --init --recursive

# Make sure we don't rely on things that shouldn't be there.
RUN make clean -C /var/local/git/grpc

# Debian packages need to be supplied externally
ADD libgrpc_amd64.deb libgrpc_amd64.deb
ADD libgrpc-dev_amd64.deb libgrpc-dev_amd64.deb

# Install the C core .deb packages
RUN /bin/bash -l -c 'dpkg -i libgrpc_amd64.deb libgrpc-dev_amd64.deb'

# Build ruby gRPC and run its tests
RUN /bin/bash -l -c 'cd /var/local/git/grpc/src/ruby && bundle && rake'

# Add a cacerts directory containing the Google root pem file, allowing the
# ruby client to access the production test instance
ADD cacerts cacerts

# Add a service_account directory containing the auth creds file
ADD service_account service_account

# Specify the default command such that the interop server runs on its known
# testing port
CMD ["/bin/bash", "-l", "-c", "ruby /var/local/git/grpc/src/ruby/bin/interop/interop_server.rb --use_tls --port 8060"]
