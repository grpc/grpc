# Copyright 2016, Google Inc.
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

# performance scenario configuration for various languages

class CXXLanguage:

  def __init__(self):
    self.safename = 'cxx'

  def worker_cmdline(self):
    return ['bins/opt/qps_worker']

  def worker_port_offset(self):
    return 0

  def scenarios(self):
    # TODO(jtattermusch): add more scenarios
    return {
            # Scenario 1: generic async streaming ping-pong (contentionless latency)
            'cpp_async_generic_streaming_ping_pong': [
                '--rpc_type=STREAMING',
                '--client_type=ASYNC_CLIENT',
                '--server_type=ASYNC_GENERIC_SERVER',
                '--outstanding_rpcs_per_channel=1',
                '--client_channels=1',
                '--bbuf_req_size=0',
                '--bbuf_resp_size=0',
                '--async_client_threads=1',
                '--async_server_threads=1',
                '--secure_test=true',
                '--num_servers=1',
                '--num_clients=1',
                '--server_core_limit=0',
                '--client_core_limit=0'],
            # Scenario 5: Sync unary ping-pong with protobufs
            'cpp_sync_unary_ping_pong_protobuf': [
                '--rpc_type=UNARY',
                '--client_type=SYNC_CLIENT',
                '--server_type=SYNC_SERVER',
                '--outstanding_rpcs_per_channel=1',
                '--client_channels=1',
                '--simple_req_size=0',
                '--simple_resp_size=0',
                '--secure_test=true',
                '--num_servers=1',
                '--num_clients=1',
                '--server_core_limit=0',
                '--client_core_limit=0']}

  def __str__(self):
    return 'c++'


class CSharpLanguage:

  def __init__(self):
    self.safename = str(self)

  def worker_cmdline(self):
    return ['tools/run_tests/performance/run_worker_csharp.sh']

  def worker_port_offset(self):
    return 100

  def scenarios(self):
    # TODO(jtattermusch): add more scenarios
    return {
            # Scenario 1: generic async streaming ping-pong (contentionless latency)
            'csharp_async_generic_streaming_ping_pong': [
                '--rpc_type=STREAMING',
                '--client_type=ASYNC_CLIENT',
                '--server_type=ASYNC_GENERIC_SERVER',
                '--outstanding_rpcs_per_channel=1',
                '--client_channels=1',
                '--bbuf_req_size=0',
                '--bbuf_resp_size=0',
                '--async_client_threads=1',
                '--async_server_threads=1',
                '--secure_test=true',
                '--num_servers=1',
                '--num_clients=1',
                '--server_core_limit=0',
                '--client_core_limit=0']}

  def __str__(self):
    return 'csharp'


class NodeLanguage:

  def __init__(self):
    pass
    self.safename = str(self)

  def worker_cmdline(self):
    return ['tools/run_tests/performance/run_worker_node.sh']

  def worker_port_offset(self):
    return 200

  def scenarios(self):
    # TODO(jtattermusch): add more scenarios
    return {
             'node_sync_unary_ping_pong_protobuf': [
                '--rpc_type=UNARY',
                '--client_type=ASYNC_CLIENT',
                '--server_type=ASYNC_SERVER',
                '--outstanding_rpcs_per_channel=1',
                '--client_channels=1',
                '--simple_req_size=0',
                '--simple_resp_size=0',
                '--secure_test=false',
                '--num_servers=1',
                '--num_clients=1',
                '--server_core_limit=0',
                '--client_core_limit=0']}

  def __str__(self):
    return 'node'


LANGUAGES = {
    'c++' : CXXLanguage(),
    'csharp' : CSharpLanguage(),
    'node' : NodeLanguage(),
}
