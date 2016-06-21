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

"""Test making many calls and immediately cancelling most of them."""
"""
import unittest
import time

from grpc._cython import cygrpc


_INFINITE_FUTURE = cygrpc.Timespec(float('+inf'))
_EMPTY_FLAGS = 0
_EMPTY_METADATA = cygrpc.Metadata(())


class CancelManyCallsTest(unittest.TestCase):

  def testCancelThenCall(self):
    server_completion_queue = cygrpc.CompletionQueue()
    server = cygrpc.Server()
    server.register_completion_queue(server_completion_queue)
    port = server.add_http2_port('[::]:0')
    server.start()
    channel = cygrpc.Channel('localhost:{}'.format(port))

    client_completion_queue = cygrpc.CompletionQueue()
    client_call = channel.create_call(
      None, _EMPTY_FLAGS, client_completion_queue, b'/twinkies', None,
      _INFINITE_FUTURE)


    metadata_tag = 'client_send_metadata'
    operations = (
      cygrpc.operation_send_initial_metadata(
        _EMPTY_METADATA, _EMPTY_FLAGS),
      cygrpc.operation_recv_close_
     )
    client_call.cancel()
    client_call.start_batch(cygrpc.Operations(operations), metadata_tag)
    client_completion_queue.poll()

    server.request_call(server_completion_queue, server_completion_queue, None)
    event = server_completion_queue.poll()

    #time.sleep(5)

    #client_call.cancel()
    
    #message_tag = 'client_send_message'
    #operations = (cygrpc.operation_send_message(b'\x45\x56', _EMPTY_FLAGS),)
    #client_call.start_batch(cygrpc.Operations(operations), message_tag)

    #event = client_completion_queue.poll()
    #self.assertEquals(event.tag, metadata_tag)
    #self.assertEquals(event.success, False)
    #event = client_completion_queue.poll()
    #self.assertEquals(event.tag, message_tag)
    #self.assertEquals(event.success, False)


if __name__ == '__main__':
  unittest.main(verbosity=2)
"""
