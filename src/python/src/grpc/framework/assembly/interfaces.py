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

# TODO(nathaniel): The assembly layer only exists to smooth out wrinkles in
# the face layer. The two should be squashed together as soon as manageable.
"""Interfaces for assembling RPC Framework values."""

import abc

# cardinality, style, and stream are referenced from specification in this
# module.
from grpc.framework.common import cardinality  # pylint: disable=unused-import
from grpc.framework.common import style  # pylint: disable=unused-import
from grpc.framework.foundation import activated
from grpc.framework.foundation import stream  # pylint: disable=unused-import


class MethodImplementation(object):
  """A sum type that describes an RPC method implementation.

  Attributes:
    cardinality: A cardinality.Cardinality value.
    style: A style.Service value.
    unary_unary_inline: The implementation of the RPC method as a callable
      value that takes a request value and a face_interfaces.RpcContext object
      and returns a response value. Only non-None if cardinality is
      cardinality.Cardinality.UNARY_UNARY and style is style.Service.INLINE.
    unary_stream_inline: The implementation of the RPC method as a callable
      value that takes a request value and a face_interfaces.RpcContext object
      and returns an iterator of response values. Only non-None if cardinality
      is cardinality.Cardinality.UNARY_STREAM and style is
      style.Service.INLINE.
    stream_unary_inline: The implementation of the RPC method as a callable
      value that takes an iterator of request values and a
      face_interfaces.RpcContext object and returns a response value. Only
      non-None if cardinality is cardinality.Cardinality.STREAM_UNARY and style
      is style.Service.INLINE.
    stream_stream_inline: The implementation of the RPC method as a callable
      value that takes an iterator of request values and a
      face_interfaces.RpcContext object and returns an iterator of response
      values. Only non-None if cardinality is
      cardinality.Cardinality.STREAM_STREAM and style is style.Service.INLINE.
    unary_unary_event: The implementation of the RPC method as a callable value
      that takes a request value, a response callback to which to pass the
      response value of the RPC, and a face_interfaces.RpcContext. Only
      non-None if cardinality is cardinality.Cardinality.UNARY_UNARY and style
      is style.Service.EVENT.
    unary_stream_event: The implementation of the RPC method as a callable
      value that takes a request value, a stream.Consumer to which to pass the
      the response values of the RPC, and a face_interfaces.RpcContext. Only
      non-None if cardinality is cardinality.Cardinality.UNARY_STREAM and style
      is style.Service.EVENT.
    stream_unary_event: The implementation of the RPC method as a callable
      value that takes a response callback to which to pass the response value
      of the RPC and a face_interfaces.RpcContext and returns a stream.Consumer
      to which the request values of the RPC should be passed. Only non-None if
      cardinality is cardinality.Cardinality.STREAM_UNARY and style is
      style.Service.EVENT.
    stream_stream_event: The implementation of the RPC method as a callable
      value that takes a stream.Consumer to which to pass the response values
      of the RPC and a face_interfaces.RpcContext and returns a stream.Consumer
      to which the request values of the RPC should be passed. Only non-None if
      cardinality is cardinality.Cardinality.STREAM_STREAM and style is
      style.Service.EVENT.
  """
  __metaclass__ = abc.ABCMeta


class Server(activated.Activated):
  """The server interface.

  Aside from being able to be activated and deactivated, objects of this type
  are able to report the port on which they are servicing RPCs.
  """
  __metaclass__ = abc.ABCMeta

  # TODO(issue 726): This is an abstraction violation; not every Server is
  # necessarily serving over a network at all.
  @abc.abstractmethod
  def port(self):
    """Identifies the port on which this Server is servicing RPCs.

    This method may only be called while the server is active.

    Returns:
      The number of the port on which this Server is servicing RPCs.
    """
    raise NotImplementedError()
