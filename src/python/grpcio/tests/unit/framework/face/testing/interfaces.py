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

"""Interfaces implemented by data sets used in Face-layer tests."""

import abc

import six

# cardinality is referenced from specification in this module.
from grpc.framework.common import cardinality  # pylint: disable=unused-import


class Method(six.with_metaclass(abc.ABCMeta)):
  """An RPC method to be used in tests of RPC implementations."""

  @abc.abstractmethod
  def name(self):
    """Identify the name of the method.

    Returns:
      The name of the method.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def cardinality(self):
    """Identify the cardinality of the method.

    Returns:
      A cardinality.Cardinality value describing the streaming semantics of the
        method.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def request_class(self):
    """Identify the class used for the method's request objects.

    Returns:
      The class object of the class to which the method's request objects
        belong.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def response_class(self):
    """Identify the class used for the method's response objects.

    Returns:
      The class object of the class to which the method's response objects
        belong.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def serialize_request(self, request):
    """Serialize the given request object.

    Args:
      request: A request object appropriate for this method.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def deserialize_request(self, serialized_request):
    """Synthesize a request object from a given bytestring.

    Args:
      serialized_request: A bytestring deserializable into a request object
        appropriate for this method.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def serialize_response(self, response):
    """Serialize the given response object.

    Args:
      response: A response object appropriate for this method.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def deserialize_response(self, serialized_response):
    """Synthesize a response object from a given bytestring.

    Args:
      serialized_response: A bytestring deserializable into a response object
        appropriate for this method.
    """
    raise NotImplementedError()
