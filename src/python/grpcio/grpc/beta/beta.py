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

"""Entry points into gRPC Python Beta."""

import enum

from grpc._adapter import _low
from grpc._adapter import _types
from grpc.beta import _connectivity_channel

_CHANNEL_SUBSCRIPTION_CALLBACK_ERROR_LOG_MESSAGE = (
    'Exception calling channel subscription callback!')


@enum.unique
class ChannelConnectivity(enum.Enum):
  """Mirrors grpc_connectivity_state in the gRPC Core.

  Attributes:
    IDLE: The channel is idle.
    CONNECTING: The channel is connecting.
    READY: The channel is ready to conduct RPCs.
    TRANSIENT_FAILURE: The channel has seen a failure from which it expects to
      recover.
    FATAL_FAILURE: The channel has seen a failure from which it cannot recover.
  """

  IDLE = (_types.ConnectivityState.IDLE, 'idle',)
  CONNECTING = (_types.ConnectivityState.CONNECTING, 'connecting',)
  READY = (_types.ConnectivityState.READY, 'ready',)
  TRANSIENT_FAILURE = (
      _types.ConnectivityState.TRANSIENT_FAILURE, 'transient failure',)
  FATAL_FAILURE = (_types.ConnectivityState.FATAL_FAILURE, 'fatal failure',)

_LOW_CONNECTIVITY_STATE_TO_CHANNEL_CONNECTIVITY = {
    state: connectivity for state, connectivity in zip(
        _types.ConnectivityState, ChannelConnectivity)
}


class Channel(object):
  """A channel to a remote host through which RPCs may be conducted.

  Only the "subscribe" and "unsubscribe" methods are supported for application
  use. This class' instance constructor and all other attributes are
  unsupported.
  """

  def __init__(self, low_channel):
    self._connectivity_channel = _connectivity_channel.ConnectivityChannel(
        low_channel, _LOW_CONNECTIVITY_STATE_TO_CHANNEL_CONNECTIVITY)

  def subscribe(self, callback, try_to_connect=None):
    """Subscribes to this Channel's connectivity.

    Args:
      callback: A callable to be invoked and passed this Channel's connectivity.
        The callable will be invoked immediately upon subscription and again for
        every change to this Channel's connectivity thereafter until it is
        unsubscribed.
      try_to_connect: A boolean indicating whether or not this Channel should
        attempt to connect if it is not already connected and ready to conduct
        RPCs.
    """
    self._connectivity_channel.subscribe(callback, try_to_connect)

  def unsubscribe(self, callback):
    """Unsubscribes a callback from this Channel's connectivity.

    Args:
      callback: A callable previously registered with this Channel from having
        been passed to its "subscribe" method.
    """
    self._connectivity_channel.unsubscribe(callback)


def create_insecure_channel(host, port):
  """Creates an insecure Channel to a remote host.

  Args:
    host: The name of the remote host to which to connect.
    port: The port of the remote host to which to connect.

  Returns:
    A Channel to the remote host through which RPCs may be conducted.
  """
  return Channel(_low.Channel('%s:%d' % (host, port), ()))
