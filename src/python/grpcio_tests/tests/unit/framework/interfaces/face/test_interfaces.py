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
"""Interfaces used in tests of implementations of the Face layer."""

import abc

import six

from grpc.framework.common import cardinality  # pylint: disable=unused-import
from grpc.framework.interfaces.face import face  # pylint: disable=unused-import


class Method(six.with_metaclass(abc.ABCMeta)):
    """Specifies a method to be used in tests."""

    @abc.abstractmethod
    def group(self):
        """Identify the group of the method.

    Returns:
      The group of the method.
    """
        raise NotImplementedError()

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


class Implementation(six.with_metaclass(abc.ABCMeta)):
    """Specifies an implementation of the Face layer."""

    @abc.abstractmethod
    def instantiate(self, methods, method_implementations,
                    multi_method_implementation):
        """Instantiates the Face layer implementation to be used in a test.

    Args:
      methods: A sequence of Method objects describing the methods available to
        be called during the test.
      method_implementations: A dictionary from group-name pair to
        face.MethodImplementation object specifying implementation of a method.
      multi_method_implementation: A face.MultiMethodImplementation or None.

    Returns:
      A sequence of length three the first element of which is a
        face.GenericStub, the second element of which is dictionary from groups
        to face.DynamicStubs affording invocation of the group's methods, and
        the third element of which is an arbitrary memo object to be kept and
        passed to destantiate at the conclusion of the test. The returned stubs
        must be backed by the provided implementations.
    """
        raise NotImplementedError()

    @abc.abstractmethod
    def destantiate(self, memo):
        """Destroys the Face layer implementation under test.

    Args:
      memo: The object from the third position of the return value of a call to
        instantiate.
    """
        raise NotImplementedError()

    @abc.abstractmethod
    def invocation_metadata(self):
        """Provides the metadata to be used when invoking a test RPC.

    Returns:
      An object to use as the supplied-at-invocation-time metadata in a test
        RPC.
    """
        raise NotImplementedError()

    @abc.abstractmethod
    def initial_metadata(self):
        """Provides the metadata for use as a test RPC's first servicer metadata.

    Returns:
      An object to use as the from-the-servicer-before-responses metadata in a
        test RPC.
    """
        raise NotImplementedError()

    @abc.abstractmethod
    def terminal_metadata(self):
        """Provides the metadata for use as a test RPC's second servicer metadata.

    Returns:
      An object to use as the from-the-servicer-after-all-responses metadata in
        a test RPC.
    """
        raise NotImplementedError()

    @abc.abstractmethod
    def code(self):
        """Provides the value for use as a test RPC's code.

    Returns:
      An object to use as the from-the-servicer code in a test RPC.
    """
        raise NotImplementedError()

    @abc.abstractmethod
    def details(self):
        """Provides the value for use as a test RPC's details.

    Returns:
      An object to use as the from-the-servicer details in a test RPC.
    """
        raise NotImplementedError()

    @abc.abstractmethod
    def metadata_transmitted(self, original_metadata, transmitted_metadata):
        """Identifies whether or not metadata was properly transmitted.

    Args:
      original_metadata: A metadata value passed to the Face interface
        implementation under test.
      transmitted_metadata: The same metadata value after having been
        transmitted via an RPC performed by the Face interface implementation
          under test.

    Returns:
      Whether or not the metadata was properly transmitted by the Face interface
        implementation under test.
    """
        raise NotImplementedError()
