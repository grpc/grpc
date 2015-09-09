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

from grpc.framework.common import cardinality
from grpc.framework.face import exceptions as face_exceptions
from grpc.framework.face import interfaces as face_interfaces
from grpc.framework.foundation import future
from grpc.framework.alpha import exceptions
from grpc.framework.alpha import interfaces

_EARLY_ADOPTER_CARDINALITY_TO_COMMON_CARDINALITY = {
    interfaces.Cardinality.UNARY_UNARY: cardinality.Cardinality.UNARY_UNARY,
    interfaces.Cardinality.UNARY_STREAM: cardinality.Cardinality.UNARY_STREAM,
    interfaces.Cardinality.STREAM_UNARY: cardinality.Cardinality.STREAM_UNARY,
    interfaces.Cardinality.STREAM_STREAM: cardinality.Cardinality.STREAM_STREAM,
}

_ABORTION_REEXPORT = {
    face_interfaces.Abortion.CANCELLED: interfaces.Abortion.CANCELLED,
    face_interfaces.Abortion.EXPIRED: interfaces.Abortion.EXPIRED,
    face_interfaces.Abortion.NETWORK_FAILURE:
        interfaces.Abortion.NETWORK_FAILURE,
    face_interfaces.Abortion.SERVICED_FAILURE:
        interfaces.Abortion.SERVICED_FAILURE,
    face_interfaces.Abortion.SERVICER_FAILURE:
        interfaces.Abortion.SERVICER_FAILURE,
}


class _RpcError(exceptions.RpcError):
  pass


def _reexport_error(face_rpc_error):
  if isinstance(face_rpc_error, face_exceptions.CancellationError):
    return exceptions.CancellationError()
  elif isinstance(face_rpc_error, face_exceptions.ExpirationError):
    return exceptions.ExpirationError()
  else:
    return _RpcError()


def _as_face_abortion_callback(abortion_callback):
  def face_abortion_callback(face_abortion):
    abortion_callback(_ABORTION_REEXPORT[face_abortion])
  return face_abortion_callback


class _ReexportedFuture(future.Future):

  def __init__(self, face_future):
    self._face_future = face_future

  def cancel(self):
    return self._face_future.cancel()

  def cancelled(self):
    return self._face_future.cancelled()

  def running(self):
    return self._face_future.running()

  def done(self):
    return self._face_future.done()

  def result(self, timeout=None):
    try:
      return self._face_future.result(timeout=timeout)
    except face_exceptions.RpcError as e:
      raise _reexport_error(e)

  def exception(self, timeout=None):
    face_error = self._face_future.exception(timeout=timeout)
    return None if face_error is None else _reexport_error(face_error)

  def traceback(self, timeout=None):
    return self._face_future.traceback(timeout=timeout)

  def add_done_callback(self, fn):
    self._face_future.add_done_callback(lambda unused_face_future: fn(self))


def _call_reexporting_errors(behavior, *args, **kwargs):
  try:
    return behavior(*args, **kwargs)
  except face_exceptions.RpcError as e:
    raise _reexport_error(e)


def _reexported_future(face_future):
  return _ReexportedFuture(face_future)


class _CancellableIterator(interfaces.CancellableIterator):

  def __init__(self, face_cancellable_iterator):
    self._face_cancellable_iterator = face_cancellable_iterator

  def __iter__(self):
    return self

  def next(self):
    return _call_reexporting_errors(self._face_cancellable_iterator.next)

  def cancel(self):
    self._face_cancellable_iterator.cancel()


class _RpcContext(interfaces.RpcContext):

  def __init__(self, face_rpc_context):
    self._face_rpc_context = face_rpc_context

  def is_active(self):
    return self._face_rpc_context.is_active()

  def time_remaining(self):
    return self._face_rpc_context.time_remaining()

  def add_abortion_callback(self, abortion_callback):
    self._face_rpc_context.add_abortion_callback(
        _as_face_abortion_callback(abortion_callback))


class _UnaryUnarySyncAsync(interfaces.UnaryUnarySyncAsync):

  def __init__(self, face_unary_unary_multi_callable):
    self._underlying = face_unary_unary_multi_callable

  def __call__(self, request, timeout):
    return _call_reexporting_errors(
        self._underlying, request, timeout)

  def async(self, request, timeout):
    return _ReexportedFuture(self._underlying.future(request, timeout))


class _StreamUnarySyncAsync(interfaces.StreamUnarySyncAsync):

  def __init__(self, face_stream_unary_multi_callable):
    self._underlying = face_stream_unary_multi_callable

  def __call__(self, request_iterator, timeout):
    return _call_reexporting_errors(
        self._underlying, request_iterator, timeout)

  def async(self, request_iterator, timeout):
    return _ReexportedFuture(self._underlying.future(request_iterator, timeout))


def common_cardinality(early_adopter_cardinality):
  return _EARLY_ADOPTER_CARDINALITY_TO_COMMON_CARDINALITY[
      early_adopter_cardinality]


def common_cardinalities(early_adopter_cardinalities):
  common_cardinalities = {}
  for name, early_adopter_cardinality in early_adopter_cardinalities.iteritems():
    common_cardinalities[name] = _EARLY_ADOPTER_CARDINALITY_TO_COMMON_CARDINALITY[
        early_adopter_cardinality]
  return common_cardinalities


def rpc_context(face_rpc_context):
  return _RpcContext(face_rpc_context)


def cancellable_iterator(face_cancellable_iterator):
  return _CancellableIterator(face_cancellable_iterator)


def unary_unary_sync_async(face_unary_unary_multi_callable):
  return _UnaryUnarySyncAsync(face_unary_unary_multi_callable)


def stream_unary_sync_async(face_stream_unary_multi_callable):
  return _StreamUnarySyncAsync(face_stream_unary_multi_callable)
