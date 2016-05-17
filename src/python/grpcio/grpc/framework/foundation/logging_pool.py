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

"""A thread pool that logs exceptions raised by tasks executed within it."""

import logging

from concurrent import futures


def _wrap(behavior):
  """Wraps an arbitrary callable behavior in exception-logging."""
  def _wrapping(*args, **kwargs):
    try:
      return behavior(*args, **kwargs)
    except Exception as e:
      logging.exception(
          'Unexpected exception from %s executed in logging pool!', behavior)
      raise
  return _wrapping


class _LoggingPool(object):
  """An exception-logging futures.ThreadPoolExecutor-compatible thread pool."""

  def __init__(self, backing_pool):
    self._backing_pool = backing_pool

  def __enter__(self):
    return self

  def __exit__(self, exc_type, exc_val, exc_tb):
    self._backing_pool.shutdown(wait=True)

  def submit(self, fn, *args, **kwargs):
    return self._backing_pool.submit(_wrap(fn), *args, **kwargs)

  def map(self, func, *iterables, **kwargs):
    return self._backing_pool.map(
        _wrap(func), *iterables, timeout=kwargs.get('timeout', None))

  def shutdown(self, wait=True):
    self._backing_pool.shutdown(wait=wait)


def pool(max_workers):
  """Creates a thread pool that logs exceptions raised by the tasks within it.

  Args:
    max_workers: The maximum number of worker threads to allow the pool.

  Returns:
    A futures.ThreadPoolExecutor-compatible thread pool that logs exceptions
      raised by the tasks executed within it.
  """
  return _LoggingPool(futures.ThreadPoolExecutor(max_workers))
