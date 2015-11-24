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

from grpc import _grpcio_metadata
from grpc._adapter import _c
from grpc._adapter import _types

_USER_AGENT = 'Python-gRPC-{}'.format(_grpcio_metadata.__version__)

ChannelCredentials = _c.ChannelCredentials
CallCredentials = _c.CallCredentials
ServerCredentials = _c.ServerCredentials


class CompletionQueue(_types.CompletionQueue):

  def __init__(self):
    self.completion_queue = _c.CompletionQueue()

  def next(self, deadline=float('+inf')):
    raw_event = self.completion_queue.next(deadline)
    if raw_event is None:
      return None
    event = _types.Event(*raw_event)
    if event.call is not None:
      event = event._replace(call=Call(event.call))
    if event.call_details is not None:
      event = event._replace(call_details=_types.CallDetails(*event.call_details))
    if event.results is not None:
      new_results = [_types.OpResult(*r) for r in event.results]
      new_results = [r if r.status is None else r._replace(status=_types.Status(_types.StatusCode(r.status[0]), r.status[1])) for r in new_results]
      event = event._replace(results=new_results)
    return event

  def shutdown(self):
    self.completion_queue.shutdown()


class Call(_types.Call):

  def __init__(self, call):
    self.call = call

  def start_batch(self, ops, tag):
    return self.call.start_batch(ops, tag)

  def cancel(self, code=None, details=None):
    if code is None and details is None:
      return self.call.cancel()
    else:
      return self.call.cancel(code, details)

  def peer(self):
    return self.call.peer()

  def set_credentials(self, creds):
    return self.call.set_credentials(creds)


class Channel(_types.Channel):

  def __init__(self, target, args, creds=None):
    args = list(args) + [(_c.PRIMARY_USER_AGENT_KEY, _USER_AGENT)]
    if creds is None:
      self.channel = _c.Channel(target, args)
    else:
      self.channel = _c.Channel(target, args, creds)

  def create_call(self, completion_queue, method, host, deadline=None):
    return Call(self.channel.create_call(completion_queue.completion_queue, method, host, deadline))

  def check_connectivity_state(self, try_to_connect):
    return self.channel.check_connectivity_state(try_to_connect)

  def watch_connectivity_state(self, last_observed_state, deadline,
                               completion_queue, tag):
    self.channel.watch_connectivity_state(
        last_observed_state, deadline, completion_queue.completion_queue, tag)

  def target(self):
    return self.channel.target()


_NO_TAG = object()

class Server(_types.Server):

  def __init__(self, completion_queue, args):
    self.server = _c.Server(completion_queue.completion_queue, args)

  def add_http2_port(self, addr, creds=None):
    if creds is None:
      return self.server.add_http2_port(addr)
    else:
      return self.server.add_http2_port(addr, creds)

  def start(self):
    return self.server.start()

  def shutdown(self, tag=None):
    return self.server.shutdown(tag)

  def request_call(self, completion_queue, tag):
    return self.server.request_call(completion_queue.completion_queue, tag)

  def cancel_all_calls(self):
    return self.server.cancel_all_calls()
