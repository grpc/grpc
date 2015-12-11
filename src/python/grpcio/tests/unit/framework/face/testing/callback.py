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

"""A utility useful in tests of asynchronous, event-driven interfaces."""

import threading

from grpc.framework.foundation import stream


class Callback(stream.Consumer):
  """A utility object useful in tests of asynchronous code."""

  def __init__(self):
    self._condition = threading.Condition()
    self._unary_response = None
    self._streamed_responses = []
    self._completed = False
    self._abortion = None

  def abort(self, abortion):
    with self._condition:
      self._abortion = abortion
      self._condition.notify_all()

  def complete(self, unary_response):
    with self._condition:
      self._unary_response = unary_response
      self._completed = True
      self._condition.notify_all()

  def consume(self, streamed_response):
    with self._condition:
      self._streamed_responses.append(streamed_response)

  def terminate(self):
    with self._condition:
      self._completed = True
      self._condition.notify_all()

  def consume_and_terminate(self, streamed_response):
    with self._condition:
      self._streamed_responses.append(streamed_response)
      self._completed = True
      self._condition.notify_all()

  def block_until_terminated(self):
    with self._condition:
      while self._abortion is None and not self._completed:
        self._condition.wait()

  def response(self):
    with self._condition:
      if self._abortion is None:
        return self._unary_response
      else:
        raise AssertionError('Aborted with abortion "%s"!' % self._abortion)

  def responses(self):
    with self._condition:
      if self._abortion is None:
        return list(self._streamed_responses)
      else:
        raise AssertionError('Aborted with abortion "%s"!' % self._abortion)

  def abortion(self):
    with self._condition:
      return self._abortion
