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

"""Entry points into the ticket-exchange-based base layer implementation."""

# base and links are referenced from specification in this module.
from grpc.framework.core import _end
from grpc.framework.interfaces.base import base  # pylint: disable=unused-import
from grpc.framework.interfaces.links import links  # pylint: disable=unused-import


def invocation_end_link():
  """Creates a base.End-links.Link suitable for operation invocation.

  Returns:
    An object that is both a base.End and a links.Link, that supports operation
      invocation, and that translates operation invocation into ticket exchange.
  """
  return _end.serviceless_end_link()


def service_end_link(servicer, default_timeout, maximum_timeout):
  """Creates a base.End-links.Link suitable for operation service.

  Args:
    servicer: A base.Servicer for servicing operations.
    default_timeout: A length of time in seconds to be used as the default
      time alloted for a single operation.
    maximum_timeout: A length of time in seconds to be used as the maximum
      time alloted for a single operation.

  Returns:
    An object that is both a base.End and a links.Link and that services
      operations that arrive at it through ticket exchange.
  """
  return _end.serviceful_end_link(servicer, default_timeout, maximum_timeout)
