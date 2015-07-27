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

"""Common lifecycle code for in-memory-ticket-exchange Face-layer tests."""

from grpc.framework.face import implementations
from grpc.framework.foundation import logging_pool
from grpc_test.framework.face.testing import base_util
from grpc_test.framework.face.testing import test_case

_TIMEOUT = 3
_MAXIMUM_POOL_SIZE = 10


class FaceTestCase(test_case.FaceTestCase):
  """Provides abstract Face-layer tests an in-memory implementation."""

  def set_up_implementation(
      self, name, methods, method_implementations,
      multi_method_implementation):
    servicer_pool = logging_pool.pool(_MAXIMUM_POOL_SIZE)
    stub_pool = logging_pool.pool(_MAXIMUM_POOL_SIZE)

    servicer = implementations.servicer(
        servicer_pool, method_implementations, multi_method_implementation)

    linked_pair = base_util.linked_pair(servicer, _TIMEOUT)
    stub = implementations.generic_stub(linked_pair.front, stub_pool)
    return stub, (servicer_pool, stub_pool, linked_pair)

  def tear_down_implementation(self, memo):
    servicer_pool, stub_pool, linked_pair = memo
    linked_pair.shut_down()
    stub_pool.shutdown(wait=True)
    servicer_pool.shutdown(wait=True)
