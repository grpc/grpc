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

# face_interfaces and interfaces are referenced in specification in this module.
from _framework.face import interfaces as face_interfaces  # pylint: disable=unused-import
from _framework.face.testing import interfaces  # pylint: disable=unused-import


class FaceTestCase(object):
  """Describes a test of the Face Layer of RPC Framework.

  Concrete subclasses must also inherit from unittest.TestCase and from at least
  one class that defines test methods.
  """
  __metaclass__ = abc.ABCMeta

  @abc.abstractmethod
  def set_up_implementation(
      self,
      name,
      methods,
      inline_value_in_value_out_methods,
      inline_value_in_stream_out_methods,
      inline_stream_in_value_out_methods,
      inline_stream_in_stream_out_methods,
      event_value_in_value_out_methods,
      event_value_in_stream_out_methods,
      event_stream_in_value_out_methods,
      event_stream_in_stream_out_methods,
      multi_method):
    """Instantiates the Face Layer implementation under test.

    Args:
      name: The service name to be used in the test.
      methods: A sequence of interfaces.Method objects describing the RPC
        methods that will be called during the test.
      inline_value_in_value_out_methods: A dictionary from string method names
        to face_interfaces.InlineValueInValueOutMethod implementations of those
        methods.
      inline_value_in_stream_out_methods: A dictionary from string method names
        to face_interfaces.InlineValueInStreamOutMethod implementations of those
        methods.
      inline_stream_in_value_out_methods: A dictionary from string method names
        to face_interfaces.InlineStreamInValueOutMethod implementations of those
        methods.
      inline_stream_in_stream_out_methods: A dictionary from string method names
        to face_interfaces.InlineStreamInStreamOutMethod implementations of
        those methods.
      event_value_in_value_out_methods: A dictionary from string method names
        to face_interfaces.EventValueInValueOutMethod implementations of those
        methods.
      event_value_in_stream_out_methods: A dictionary from string method names
        to face_interfaces.EventValueInStreamOutMethod implementations of those
        methods.
      event_stream_in_value_out_methods: A dictionary from string method names
        to face_interfaces.EventStreamInValueOutMethod implementations of those
        methods.
      event_stream_in_stream_out_methods: A dictionary from string method names
        to face_interfaces.EventStreamInStreamOutMethod implementations of those
        methods.
      multi_method: An face_interfaces.MultiMethod, or None.

    Returns:
      A sequence of length three the first element of which is a
        face_interfaces.Server, the second element of which is a
        face_interfaces.Stub, (both of which are backed by the given method
        implementations), and the third element of which is an arbitrary memo
        object to be kept and passed to tearDownImplementation at the conclusion
        of the test.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def tear_down_implementation(self, memo):
    """Destroys the Face layer implementation under test.

    Args:
      memo: The object from the third position of the return value of
        set_up_implementation.
    """
    raise NotImplementedError()
