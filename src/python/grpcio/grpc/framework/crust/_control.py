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

"""State and behavior for translating between sync and async control flow."""

import collections
import enum
import sys
import threading
import time

from grpc.framework.foundation import abandonment
from grpc.framework.foundation import callable_util
from grpc.framework.foundation import future
from grpc.framework.foundation import stream
from grpc.framework.interfaces.base import base
from grpc.framework.interfaces.base import utilities
from grpc.framework.interfaces.face import face

_DONE_CALLBACK_LOG_MESSAGE = 'Exception calling Future "done" callback!'
_INTERNAL_ERROR_LOG_MESSAGE = ':-( RPC Framework (Crust) Internal Error! )-:'

_CANNOT_SET_INITIAL_METADATA = (
    'Could not set initial metadata - has it already been set, or has a ' +
    'payload already been sent?')
_CANNOT_SET_TERMINAL_METADATA = (
    'Could not set terminal metadata - has it already been set, or has RPC ' +
    'completion already been indicated?')
_CANNOT_SET_CODE = (
    'Could not set code - has it already been set, or has RPC completion ' +
    'already been indicated?')
_CANNOT_SET_DETAILS = (
    'Could not set details - has it already been set, or has RPC completion ' +
    'already been indicated?')


class _DummyOperator(base.Operator):

  def advance(
      self, initial_metadata=None, payload=None, completion=None,
      allowance=None):
    pass

_DUMMY_OPERATOR = _DummyOperator()


class _Awaited(
    collections.namedtuple('_Awaited', ('kind', 'value',))):

  @enum.unique
  class Kind(enum.Enum):
    NOT_YET_ARRIVED = 'not yet arrived'
    ARRIVED = 'arrived'

_NOT_YET_ARRIVED = _Awaited(_Awaited.Kind.NOT_YET_ARRIVED, None)
_ARRIVED_AND_NONE = _Awaited(_Awaited.Kind.ARRIVED, None)


class _Transitory(
    collections.namedtuple('_Transitory', ('kind', 'value',))):

  @enum.unique
  class Kind(enum.Enum):
    NOT_YET_SEEN = 'not yet seen'
    PRESENT = 'present'
    GONE = 'gone'

_NOT_YET_SEEN = _Transitory(_Transitory.Kind.NOT_YET_SEEN, None)
_GONE = _Transitory(_Transitory.Kind.GONE, None)


class _Termination(
    collections.namedtuple(
        '_Termination', ('terminated', 'abortion', 'abortion_error',))):
  """Values indicating whether and how an RPC has terminated.

  Attributes:
    terminated: A boolean indicating whether or not the RPC has terminated.
    abortion: A face.Abortion value describing the RPC's abortion or None if the
      RPC did not abort.
    abortion_error: A face.AbortionError describing the RPC's abortion or None
      if the RPC did not abort.
  """

_NOT_TERMINATED = _Termination(False, None, None)

_OPERATION_OUTCOME_KIND_TO_TERMINATION_CONSTRUCTOR = {
    base.Outcome.Kind.COMPLETED: lambda *unused_args: _Termination(
        True, None, None),
    base.Outcome.Kind.CANCELLED: lambda *args: _Termination(
        True, face.Abortion(face.Abortion.Kind.CANCELLED, *args),
        face.CancellationError(*args)),
    base.Outcome.Kind.EXPIRED: lambda *args: _Termination(
        True, face.Abortion(face.Abortion.Kind.EXPIRED, *args),
        face.ExpirationError(*args)),
    base.Outcome.Kind.LOCAL_SHUTDOWN: lambda *args: _Termination(
        True, face.Abortion(face.Abortion.Kind.LOCAL_SHUTDOWN, *args),
        face.LocalShutdownError(*args)),
    base.Outcome.Kind.REMOTE_SHUTDOWN: lambda *args: _Termination(
        True, face.Abortion(face.Abortion.Kind.REMOTE_SHUTDOWN, *args),
        face.RemoteShutdownError(*args)),
    base.Outcome.Kind.RECEPTION_FAILURE: lambda *args: _Termination(
        True, face.Abortion(face.Abortion.Kind.NETWORK_FAILURE, *args),
        face.NetworkError(*args)),
    base.Outcome.Kind.TRANSMISSION_FAILURE: lambda *args: _Termination(
        True, face.Abortion(face.Abortion.Kind.NETWORK_FAILURE, *args),
        face.NetworkError(*args)),
    base.Outcome.Kind.LOCAL_FAILURE: lambda *args: _Termination(
        True, face.Abortion(face.Abortion.Kind.LOCAL_FAILURE, *args),
        face.LocalError(*args)),
    base.Outcome.Kind.REMOTE_FAILURE: lambda *args: _Termination(
        True, face.Abortion(face.Abortion.Kind.REMOTE_FAILURE, *args),
        face.RemoteError(*args)),
}


def _wait_once_until(condition, until):
  if until is None:
    condition.wait()
  else:
    remaining = until - time.time()
    if remaining < 0:
      raise future.TimeoutError()
    else:
      condition.wait(timeout=remaining)


def _done_callback_as_operation_termination_callback(
    done_callback, rendezvous):
  def operation_termination_callback(operation_outcome):
    rendezvous.set_outcome(operation_outcome)
    done_callback(rendezvous)
  return operation_termination_callback


def _abortion_callback_as_operation_termination_callback(
    rpc_abortion_callback, rendezvous_set_outcome):
  def operation_termination_callback(operation_outcome):
    termination = rendezvous_set_outcome(operation_outcome)
    if termination.abortion is not None:
      rpc_abortion_callback(termination.abortion)
  return operation_termination_callback


class Rendezvous(base.Operator, future.Future, stream.Consumer, face.Call):
  """A rendez-vous for the threads of an operation.

  Instances of this object present iterator and stream.Consumer interfaces for
  interacting with application code and present a base.Operator interface and
  maintain a base.Operator internally for interacting with base interface code.
  """

  def __init__(self, operator, operation_context):
    self._condition = threading.Condition()

    self._operator = operator
    self._operation_context = operation_context

    self._protocol_context = _NOT_YET_ARRIVED

    self._up_initial_metadata = _NOT_YET_ARRIVED
    self._up_payload = None
    self._up_allowance = 1
    self._up_completion = _NOT_YET_ARRIVED
    self._down_initial_metadata = _NOT_YET_SEEN
    self._down_payload = None
    self._down_allowance = 1
    self._down_terminal_metadata = _NOT_YET_SEEN
    self._down_code = _NOT_YET_SEEN
    self._down_details = _NOT_YET_SEEN

    self._termination = _NOT_TERMINATED

    # The semantics of future.Future.cancel and future.Future.cancelled are
    # slightly wonky, so they have to be tracked separately from the rest of the
    # result of the RPC. This field tracks whether cancellation was requested
    # prior to termination of the RPC
    self._cancelled = False

  def set_operator_and_context(self, operator, operation_context):
    with self._condition:
      self._operator = operator
      self._operation_context = operation_context

  def _down_completion(self):
    if self._down_terminal_metadata.kind is _Transitory.Kind.NOT_YET_SEEN:
      terminal_metadata = None
      self._down_terminal_metadata = _GONE
    elif self._down_terminal_metadata.kind is _Transitory.Kind.PRESENT:
      terminal_metadata = self._down_terminal_metadata.value
      self._down_terminal_metadata = _GONE
    else:
      terminal_metadata = None
    if self._down_code.kind is _Transitory.Kind.NOT_YET_SEEN:
      code = None
      self._down_code = _GONE
    elif self._down_code.kind is _Transitory.Kind.PRESENT:
      code = self._down_code.value
      self._down_code = _GONE
    else:
      code = None
    if self._down_details.kind is _Transitory.Kind.NOT_YET_SEEN:
      details = None
      self._down_details = _GONE
    elif self._down_details.kind is _Transitory.Kind.PRESENT:
      details = self._down_details.value
      self._down_details = _GONE
    else:
      details = None
    return utilities.completion(terminal_metadata, code, details)

  def _set_outcome(self, outcome):
    if not self._termination.terminated:
      self._operator = _DUMMY_OPERATOR
      self._operation_context = None
      self._down_initial_metadata = _GONE
      self._down_payload = None
      self._down_terminal_metadata = _GONE
      self._down_code = _GONE
      self._down_details = _GONE

      if self._up_initial_metadata.kind is _Awaited.Kind.NOT_YET_ARRIVED:
        initial_metadata = None
      else:
        initial_metadata = self._up_initial_metadata.value
      if self._up_completion.kind is _Awaited.Kind.NOT_YET_ARRIVED:
        terminal_metadata = None
      else:
        terminal_metadata = self._up_completion.value.terminal_metadata
      if outcome.kind is base.Outcome.Kind.COMPLETED:
        code = self._up_completion.value.code
        details = self._up_completion.value.message
      else:
        code = outcome.code
        details = outcome.details
      self._termination = _OPERATION_OUTCOME_KIND_TO_TERMINATION_CONSTRUCTOR[
          outcome.kind](initial_metadata, terminal_metadata, code, details)

      self._condition.notify_all()

    return self._termination

  def advance(
      self, initial_metadata=None, payload=None, completion=None,
      allowance=None):
    with self._condition:
      if initial_metadata is not None:
        self._up_initial_metadata = _Awaited(
            _Awaited.Kind.ARRIVED, initial_metadata)
      if payload is not None:
        if self._up_initial_metadata.kind is _Awaited.Kind.NOT_YET_ARRIVED:
          self._up_initial_metadata = _ARRIVED_AND_NONE
        self._up_payload = payload
        self._up_allowance -= 1
      if completion is not None:
        if self._up_initial_metadata.kind is _Awaited.Kind.NOT_YET_ARRIVED:
          self._up_initial_metadata = _ARRIVED_AND_NONE
        self._up_completion = _Awaited(
            _Awaited.Kind.ARRIVED, completion)
      if allowance is not None:
        if self._down_payload is not None:
          self._operator.advance(payload=self._down_payload)
          self._down_payload = None
          self._down_allowance += allowance - 1
        else:
          self._down_allowance += allowance
      self._condition.notify_all()

  def cancel(self):
    with self._condition:
      if self._operation_context is not None:
        self._operation_context.cancel()
        self._cancelled = True
      return False

  def cancelled(self):
    with self._condition:
      return self._cancelled

  def running(self):
    with self._condition:
      return not self._termination.terminated

  def done(self):
    with self._condition:
      return self._termination.terminated

  def result(self, timeout=None):
    until = None if timeout is None else time.time() + timeout
    with self._condition:
      while True:
        if self._termination.terminated:
          if self._termination.abortion is None:
            return self._up_payload
          elif self._termination.abortion.kind is face.Abortion.Kind.CANCELLED:
            raise future.CancelledError()
          else:
            raise self._termination.abortion_error  # pylint: disable=raising-bad-type
        else:
          _wait_once_until(self._condition, until)

  def exception(self, timeout=None):
    until = None if timeout is None else time.time() + timeout
    with self._condition:
      while True:
        if self._termination.terminated:
          if self._termination.abortion is None:
            return None
          else:
            return self._termination.abortion_error
        else:
          _wait_once_until(self._condition, until)

  def traceback(self, timeout=None):
    until = None if timeout is None else time.time() + timeout
    with self._condition:
      while True:
        if self._termination.terminated:
          if self._termination.abortion_error is None:
            return None
          else:
            abortion_error = self._termination.abortion_error
            break
        else:
          _wait_once_until(self._condition, until)

    try:
      raise abortion_error
    except face.AbortionError:
      return sys.exc_info()[2]

  def add_done_callback(self, fn):
    with self._condition:
      if self._operation_context is not None:
        outcome = self._operation_context.add_termination_callback(
            _done_callback_as_operation_termination_callback(fn, self))
        if outcome is None:
          return
        else:
          self._set_outcome(outcome)

    fn(self)

  def consume(self, value):
    with self._condition:
      while True:
        if self._termination.terminated:
          return
        elif 0 < self._down_allowance:
          self._operator.advance(payload=value)
          self._down_allowance -= 1
          return
        else:
          self._condition.wait()

  def terminate(self):
    with self._condition:
      if self._termination.terminated:
        return
      elif self._down_code.kind is _Transitory.Kind.GONE:
        # Conform to specified idempotence of terminate by ignoring extra calls.
        return
      else:
        completion = self._down_completion()
        self._operator.advance(completion=completion)

  def consume_and_terminate(self, value):
    with self._condition:
      while True:
        if self._termination.terminated:
          return
        elif 0 < self._down_allowance:
          completion = self._down_completion()
          self._operator.advance(payload=value, completion=completion)
          return
        else:
          self._condition.wait()

  def __iter__(self):
    return self

  def __next__(self):
    return self.next()

  def next(self):
    with self._condition:
      while True:
        if self._termination.abortion_error is not None:
          raise self._termination.abortion_error
        elif self._up_payload is not None:
          payload = self._up_payload
          self._up_payload = None
          if self._up_completion.kind is _Awaited.Kind.NOT_YET_ARRIVED:
            self._operator.advance(allowance=1)
          return payload
        elif self._up_completion.kind is _Awaited.Kind.ARRIVED:
          raise StopIteration()
        else:
          self._condition.wait()

  def is_active(self):
    with self._condition:
      return not self._termination.terminated

  def time_remaining(self):
    if self._operation_context is None:
      return 0
    else:
      return self._operation_context.time_remaining()

  def add_abortion_callback(self, abortion_callback):
    with self._condition:
      if self._operation_context is None:
        return self._termination.abortion
      else:
        outcome = self._operation_context.add_termination_callback(
            _abortion_callback_as_operation_termination_callback(
                abortion_callback, self.set_outcome))
        if outcome is not None:
          return self._set_outcome(outcome).abortion
        else:
          return self._termination.abortion

  def protocol_context(self):
    with self._condition:
      while True:
        if self._protocol_context.kind is _Awaited.Kind.ARRIVED:
          return self._protocol_context.value
        elif self._termination.abortion_error is not None:
          raise self._termination.abortion_error
        else:
          self._condition.wait()

  def initial_metadata(self):
    with self._condition:
      while True:
        if self._up_initial_metadata.kind is _Awaited.Kind.ARRIVED:
          return self._up_initial_metadata.value
        elif self._termination.terminated:
          return None
        else:
          self._condition.wait()

  def terminal_metadata(self):
    with self._condition:
      while True:
        if self._up_completion.kind is _Awaited.Kind.ARRIVED:
          return self._up_completion.value.terminal_metadata
        elif self._termination.terminated:
          return None
        else:
          self._condition.wait()

  def code(self):
    with self._condition:
      while True:
        if self._up_completion.kind is _Awaited.Kind.ARRIVED:
          return self._up_completion.value.code
        elif self._termination.terminated:
          return None
        else:
          self._condition.wait()

  def details(self):
    with self._condition:
      while True:
        if self._up_completion.kind is _Awaited.Kind.ARRIVED:
          return self._up_completion.value.message
        elif self._termination.terminated:
          return None
        else:
          self._condition.wait()

  def set_initial_metadata(self, initial_metadata):
    with self._condition:
      if (self._down_initial_metadata.kind is not
          _Transitory.Kind.NOT_YET_SEEN):
        raise ValueError(_CANNOT_SET_INITIAL_METADATA)
      else:
        self._down_initial_metadata = _GONE
        self._operator.advance(initial_metadata=initial_metadata)

  def set_terminal_metadata(self, terminal_metadata):
    with self._condition:
      if (self._down_terminal_metadata.kind is not
          _Transitory.Kind.NOT_YET_SEEN):
        raise ValueError(_CANNOT_SET_TERMINAL_METADATA)
      else:
        self._down_terminal_metadata = _Transitory(
            _Transitory.Kind.PRESENT, terminal_metadata)

  def set_code(self, code):
    with self._condition:
      if self._down_code.kind is not _Transitory.Kind.NOT_YET_SEEN:
        raise ValueError(_CANNOT_SET_CODE)
      else:
        self._down_code = _Transitory(_Transitory.Kind.PRESENT, code)

  def set_details(self, details):
    with self._condition:
      if self._down_details.kind is not _Transitory.Kind.NOT_YET_SEEN:
        raise ValueError(_CANNOT_SET_DETAILS)
      else:
        self._down_details = _Transitory(_Transitory.Kind.PRESENT, details)

  def set_protocol_context(self, protocol_context):
    with self._condition:
      self._protocol_context = _Awaited(
          _Awaited.Kind.ARRIVED, protocol_context)
      self._condition.notify_all()

  def set_outcome(self, outcome):
    with self._condition:
      return self._set_outcome(outcome)


class _ProtocolReceiver(base.ProtocolReceiver):

  def __init__(self, rendezvous):
    self._rendezvous = rendezvous

  def context(self, protocol_context):
    self._rendezvous.set_protocol_context(protocol_context)


def protocol_receiver(rendezvous):
  return _ProtocolReceiver(rendezvous)


def pool_wrap(behavior, operation_context):
  """Wraps an operation-related behavior so that it may be called in a pool.

  Args:
    behavior: A callable related to carrying out an operation.
    operation_context: A base_interfaces.OperationContext for the operation.

  Returns:
    A callable that when called carries out the behavior of the given callable
      and handles whatever exceptions it raises appropriately.
  """
  def translation(*args):
    try:
      behavior(*args)
    except (
        abandonment.Abandoned,
        face.CancellationError,
        face.ExpirationError,
        face.LocalShutdownError,
        face.RemoteShutdownError,
        face.NetworkError,
        face.RemoteError,
    ) as e:
      if operation_context.outcome() is None:
        operation_context.fail(e)
    except Exception as e:
      operation_context.fail(e)
  return callable_util.with_exceptions_logged(
      translation, _INTERNAL_ERROR_LOG_MESSAGE)
