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

"""Tools for creating tests of implementations of the Face layer."""

import abc

import six

# face_interfaces and interfaces are referenced in specification in this module.
from grpc.framework.face import interfaces as face_interfaces  # pylint: disable=unused-import
from tests.unit.framework.face.testing import interfaces  # pylint: disable=unused-import


class FaceTestCase(six.with_metaclass(abc.ABCMeta)):
  """Describes a test of the Face Layer of RPC Framework.

  Concrete subclasses must also inherit from unittest.TestCase and from at least
  one class that defines test methods.
  """

  @abc.abstractmethod
  def set_up_implementation(
      self, name, methods, method_implementations,
      multi_method_implementation):
    """Instantiates the Face Layer implementation under test.

    Args:
      name: The service name to be used in the test.
      methods: A sequence of interfaces.Method objects describing the RPC
        methods that will be called during the test.
      method_implementations: A dictionary from string RPC method name to
        face_interfaces.MethodImplementation object specifying
        implementation of an RPC method.
      multi_method_implementation: An face_interfaces.MultiMethodImplementation
        or None.

    Returns:
      A sequence of length two the first element of which is a
        face_interfaces.GenericStub (backed by the given method
        implementations), and the second element of which is an arbitrary memo
        object to be kept and passed to tearDownImplementation at the conclusion
        of the test.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def tear_down_implementation(self, memo):
    """Destroys the Face layer implementation under test.

    Args:
      memo: The object from the second position of the return value of
        set_up_implementation.
    """
    raise NotImplementedError()
