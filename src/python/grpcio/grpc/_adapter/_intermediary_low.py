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

"""Temporary old _low-like layer.

Eases refactoring burden while we overhaul the Python framework.

Plan:
    The layers used to look like:
        ...                # outside _adapter
        fore.py + rear.py  # visible outside _adapter
        _low
        _c
    The layers currently look like:
        ...                # outside _adapter
        fore.py + rear.py  # visible outside _adapter
        _low_intermediary  # adapter for new '_low' to old '_low'
        _low               # new '_low'
        _c                 # new '_c'
    We will later remove _low_intermediary after refactoring of fore.py and
    rear.py according to the ticket system refactoring and get:
        ...                # outside _adapter, refactored
        fore.py + rear.py  # visible outside _adapter, refactored
        _low               # new '_low'
        _c                 # new '_c'
"""

import collections
import enum

from grpc._adapter import _low
from grpc._adapter import _types

_IGNORE_ME_TAG = object()
Code = _types.StatusCode
WriteFlags = _types.OpWriteFlags


class Status(collections.namedtuple('Status', ['code', 'details'])):
  """Describes an RPC's overall status."""


class ServiceAcceptance(
    collections.namedtuple(
        'ServiceAcceptance', ['call', 'method', 'host', 'deadline'])):
  """Describes an RPC on the service side at the start of service."""


class Event(
    collections.namedtuple(
        'Event',
        ['kind', 'tag', 'write_accepted', 'complete_accepted',
         'service_acceptance', 'bytes', 'status', 'metadata'])):
  """Describes an event emitted from a completion queue."""

  @enum.unique
  class Kind(enum.Enum):
    """Describes the kind of an event."""

    STOP = object()
    WRITE_ACCEPTED = object()
    COMPLETE_ACCEPTED = object()
    SERVICE_ACCEPTED = object()
    READ_ACCEPTED = object()
    METADATA_ACCEPTED = object()
    FINISH = object()


class _TagAdapter(collections.namedtuple('_TagAdapter', [
      'user_tag',
      'kind'
  ])):
  pass


class Call(object):
  """Adapter from old _low.Call interface to new _low.Call."""

  def __init__(self, channel, completion_queue, method, host, deadline):
    self._internal = channel._internal.create_call(
        completion_queue._internal, method, host, deadline)
    self._metadata = []

  @staticmethod
  def _from_internal(internal):
    call = Call.__new__(Call)
    call._internal = internal
    call._metadata = []
    return call

  def invoke(self, completion_queue, metadata_tag, finish_tag):
    err = self._internal.start_batch([
          _types.OpArgs.send_initial_metadata(self._metadata)
      ], _IGNORE_ME_TAG)
    if err != _types.CallError.OK:
      return err
    err = self._internal.start_batch([
          _types.OpArgs.recv_initial_metadata()
      ], _TagAdapter(metadata_tag, Event.Kind.METADATA_ACCEPTED))
    if err != _types.CallError.OK:
      return err
    err = self._internal.start_batch([
          _types.OpArgs.recv_status_on_client()
      ], _TagAdapter(finish_tag, Event.Kind.FINISH))
    return err

  def write(self, message, tag, flags):
    return self._internal.start_batch([
          _types.OpArgs.send_message(message, flags)
      ], _TagAdapter(tag, Event.Kind.WRITE_ACCEPTED))

  def complete(self, tag):
    return self._internal.start_batch([
          _types.OpArgs.send_close_from_client()
      ], _TagAdapter(tag, Event.Kind.COMPLETE_ACCEPTED))

  def accept(self, completion_queue, tag):
    return self._internal.start_batch([
          _types.OpArgs.recv_close_on_server()
      ], _TagAdapter(tag, Event.Kind.FINISH))

  def add_metadata(self, key, value):
    self._metadata.append((key, value))

  def premetadata(self):
    result = self._internal.start_batch([
          _types.OpArgs.send_initial_metadata(self._metadata)
      ], _IGNORE_ME_TAG)
    self._metadata = []
    return result

  def read(self, tag):
    return self._internal.start_batch([
          _types.OpArgs.recv_message()
      ], _TagAdapter(tag, Event.Kind.READ_ACCEPTED))

  def status(self, status, tag):
    return self._internal.start_batch([
          _types.OpArgs.send_status_from_server(
              self._metadata, status.code, status.details)
      ], _TagAdapter(tag, Event.Kind.COMPLETE_ACCEPTED))

  def cancel(self):
    return self._internal.cancel()

  def peer(self):
    return self._internal.peer()

  def set_credentials(self, creds):
    return self._internal.set_credentials(creds)


class Channel(object):
  """Adapter from old _low.Channel interface to new _low.Channel."""

  def __init__(self, hostport, channel_credentials, server_host_override=None):
    args = []
    if server_host_override:
      args.append((_types.GrpcChannelArgumentKeys.SSL_TARGET_NAME_OVERRIDE.value, server_host_override))
    self._internal = _low.Channel(hostport, args, channel_credentials)


class CompletionQueue(object):
  """Adapter from old _low.CompletionQueue interface to new _low.CompletionQueue."""

  def __init__(self):
    self._internal = _low.CompletionQueue()

  def get(self, deadline=None):
    if deadline is None:
      ev = self._internal.next(float('+inf'))
    else:
      ev = self._internal.next(deadline)
    if ev is None:
      return None
    elif ev.tag is _IGNORE_ME_TAG:
      return self.get(deadline)
    elif ev.type == _types.EventType.QUEUE_SHUTDOWN:
      kind = Event.Kind.STOP
      tag = None
      write_accepted = None
      complete_accepted = None
      service_acceptance = None
      message_bytes = None
      status = None
      metadata = None
    elif ev.type == _types.EventType.OP_COMPLETE:
      kind = ev.tag.kind
      tag = ev.tag.user_tag
      write_accepted = ev.success if kind == Event.Kind.WRITE_ACCEPTED else None
      complete_accepted = ev.success if kind == Event.Kind.COMPLETE_ACCEPTED else None
      service_acceptance = ServiceAcceptance(Call._from_internal(ev.call), ev.call_details.method, ev.call_details.host, ev.call_details.deadline) if kind == Event.Kind.SERVICE_ACCEPTED else None
      message_bytes = ev.results[0].message if kind == Event.Kind.READ_ACCEPTED else None
      status = Status(ev.results[0].status.code, ev.results[0].status.details) if (kind == Event.Kind.FINISH and ev.results[0].status) else Status(_types.StatusCode.CANCELLED if ev.results[0].cancelled else _types.StatusCode.OK, '') if len(ev.results) > 0 and ev.results[0].cancelled is not None else None
      metadata = ev.results[0].initial_metadata if (kind in [Event.Kind.SERVICE_ACCEPTED, Event.Kind.METADATA_ACCEPTED]) else (ev.results[0].trailing_metadata if kind == Event.Kind.FINISH else None)
    else:
      raise RuntimeError('unknown event')
    result_ev = Event(kind=kind, tag=tag, write_accepted=write_accepted, complete_accepted=complete_accepted, service_acceptance=service_acceptance, bytes=message_bytes, status=status, metadata=metadata)
    return result_ev

  def stop(self):
    self._internal.shutdown()


class Server(object):
  """Adapter from old _low.Server interface to new _low.Server."""

  def __init__(self, completion_queue):
    self._internal = _low.Server(completion_queue._internal, [])
    self._internal_cq = completion_queue._internal

  def add_http2_addr(self, addr):
    return self._internal.add_http2_port(addr)

  def add_secure_http2_addr(self, addr, server_credentials):
    if server_credentials is None:
      return self._internal.add_http2_port(addr, None)
    else:
      return self._internal.add_http2_port(addr, server_credentials)

  def start(self):
    return self._internal.start()

  def service(self, tag):
    return self._internal.request_call(self._internal_cq, _TagAdapter(tag, Event.Kind.SERVICE_ACCEPTED))

  def cancel_all_calls(self):
    self._internal.cancel_all_calls()

  def stop(self):
    return self._internal.shutdown(_TagAdapter(None, Event.Kind.STOP))

