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

# Dockerfile for GRPC Python
FROM grpc/python_base

# Pull the latest sources
RUN cd /var/local/git/grpc \
  && git pull --recurse-submodules \
  && git submodule update --init --recursive

# Build the C core.
RUN make install_c -j12 -C /var/local/git/grpc

# Build Python GRPC
RUN cd /var/local/git/grpc \
  && pip install src/python/src \
  && pip install src/python/interop

# Run Python GRPC's tests
# TODO(nathaniel): It would be nice for these to be auto-discoverable?
RUN cd /var/local/git/grpc \
  && python2.7 -B -m grpc._adapter._blocking_invocation_inline_service_test \
  && python2.7 -B -m grpc._adapter._c_test \
  && python2.7 -B -m grpc._adapter._event_invocation_synchronous_event_service_test \
  && python2.7 -B -m grpc._adapter._future_invocation_asynchronous_event_service_test \
  && python2.7 -B -m grpc._adapter._links_test \
  && python2.7 -B -m grpc._adapter._lonely_rear_link_test \
  && python2.7 -B -m grpc._adapter._low_test \
  && python2.7 -B -m grpc.early_adopter.implementations_test \
  && python2.7 -B -m grpc.framework.base.packets.implementations_test \
  && python2.7 -B -m grpc.framework.face.blocking_invocation_inline_service_test \
  && python2.7 -B -m grpc.framework.face.event_invocation_synchronous_event_service_test \
  && python2.7 -B -m grpc.framework.face.future_invocation_asynchronous_event_service_test \
  && python2.7 -B -m grpc.framework.foundation._later_test \
  && python2.7 -B -m grpc.framework.foundation._logging_pool_test \
  && python2.7 -B -m interop._insecure_interop_test \
  && python2.7 -B -m interop._secure_interop_test

# Add a cacerts directory containing the Google root pem file, allowing the interop client to access the production test instance
ADD cacerts cacerts

# Specify the default command such that the interop server runs on its known testing port
CMD ["/bin/bash", "-l", "-c", "python2.7 -m interop.server --use_tls --port 8050"]
