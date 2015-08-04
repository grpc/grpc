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

"""Exceptions used in the Face layer of RPC Framework."""

import abc


class NoSuchMethodError(Exception):
  """Raised by customer code to indicate an unrecognized RPC method name.

  Attributes:
    name: The unrecognized name.
  """

  def __init__(self, name):
    """Constructor.

    Args:
      name: The unrecognized RPC method name.
    """
    super(NoSuchMethodError, self).__init__()
    self.name = name


class RpcError(Exception):
  """Common super type for all exceptions raised by the Face layer.

  Only RPC Framework should instantiate and raise these exceptions.
  """
  __metaclass__ = abc.ABCMeta


class CancellationError(RpcError):
  """Indicates that an RPC has been cancelled."""


class ExpirationError(RpcError):
  """Indicates that an RPC has expired ("timed out")."""


class NetworkError(RpcError):
  """Indicates that some error occurred on the network."""


class ServicedError(RpcError):
  """Indicates that the Serviced failed in the course of an RPC."""


class ServicerError(RpcError):
  """Indicates that the Servicer failed in the course of servicing an RPC."""
