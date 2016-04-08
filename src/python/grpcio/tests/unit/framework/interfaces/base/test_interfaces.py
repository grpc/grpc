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

"""Interfaces used in tests of implementations of the Base layer."""

import abc

import six

from grpc.framework.interfaces.base import base  # pylint: disable=unused-import


class Serialization(six.with_metaclass(abc.ABCMeta)):
  """Specifies serialization and deserialization of test payloads."""

  def serialize_request(self, request):
    """Serializes a request value used in a test.

    Args:
      request: A request value created by a test.

    Returns:
      A bytestring that is the serialization of the given request.
    """
    raise NotImplementedError()

  def deserialize_request(self, serialized_request):
    """Deserializes a request value used in a test.

    Args:
      serialized_request: A bytestring that is the serialization of some request
        used in a test.

    Returns:
      The request value encoded by the given bytestring.
    """
    raise NotImplementedError()

  def serialize_response(self, response):
    """Serializes a response value used in a test.

    Args:
      response: A response value created by a test.

    Returns:
      A bytestring that is the serialization of the given response.
    """
    raise NotImplementedError()

  def deserialize_response(self, serialized_response):
    """Deserializes a response value used in a test.

    Args:
      serialized_response: A bytestring that is the serialization of some
        response used in a test.

    Returns:
      The response value encoded by the given bytestring.
    """
    raise NotImplementedError()


class Implementation(six.with_metaclass(abc.ABCMeta)):
  """Specifies an implementation of the Base layer."""

  @abc.abstractmethod
  def instantiate(self, serializations, servicer):
    """Instantiates the Base layer implementation to be used in a test.

    Args:
      serializations: A dict from group-method pair to Serialization object
        specifying how to serialize and deserialize payload values used in the
        test.
      servicer: A base.Servicer object to be called to service RPCs made during
        the test.

    Returns:
      A sequence of length three the first element of which is a
        base.End to be used to invoke RPCs, the second element of which is a
        base.End to be used to service invoked RPCs, and the third element of
        which is an arbitrary memo object to be kept and passed to destantiate
        at the conclusion of the test.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def destantiate(self, memo):
    """Destroys the Base layer implementation under test.

    Args:
      memo: The object from the third position of the return value of a call to
        instantiate.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def invocation_initial_metadata(self):
    """Provides an operation's invocation-side initial metadata.

    Returns:
      A value to use for an operation's invocation-side initial metadata, or
        None.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def service_initial_metadata(self):
    """Provides an operation's service-side initial metadata.

    Returns:
      A value to use for an operation's service-side initial metadata, or
        None.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def invocation_completion(self):
    """Provides an operation's invocation-side completion.

    Returns:
      A base.Completion to use for an operation's invocation-side completion.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def service_completion(self):
    """Provides an operation's service-side completion.

    Returns:
      A base.Completion to use for an operation's service-side completion.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def metadata_transmitted(self, original_metadata, transmitted_metadata):
    """Identifies whether or not metadata was properly transmitted.

    Args:
      original_metadata: A metadata value passed to the system under test.
      transmitted_metadata: The same metadata value after having been
        transmitted through the system under test.

    Returns:
      Whether or not the metadata was properly transmitted.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def completion_transmitted(self, original_completion, transmitted_completion):
    """Identifies whether or not a base.Completion was properly transmitted.

    Args:
      original_completion: A base.Completion passed to the system under test.
      transmitted_completion: The same completion value after having been
        transmitted through the system under test.

    Returns:
      Whether or not the completion was properly transmitted.
    """
    raise NotImplementedError()
