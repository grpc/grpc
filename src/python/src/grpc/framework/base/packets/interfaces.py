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

"""Interfaces defined and used by the base layer of RPC Framework."""

import abc

# packets is referenced from specifications in this module.
from grpc.framework.base import interfaces
from grpc.framework.base.packets import packets  # pylint: disable=unused-import


class ForeLink(object):
  """Accepts back-to-front tickets and emits front-to-back tickets."""
  __metaclass__ = abc.ABCMeta

  @abc.abstractmethod
  def accept_back_to_front_ticket(self, ticket):
    """Accept a packets.BackToFrontPacket.

    Args:
      ticket: Any packets.BackToFrontPacket.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def join_rear_link(self, rear_link):
    """Mates this object with a peer with which it will exchange tickets."""
    raise NotImplementedError()


class RearLink(object):
  """Accepts front-to-back tickets and emits back-to-front tickets."""
  __metaclass__ = abc.ABCMeta

  @abc.abstractmethod
  def accept_front_to_back_ticket(self, ticket):
    """Accepts a packets.FrontToBackPacket.

    Args:
      ticket: Any packets.FrontToBackPacket.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def join_fore_link(self, fore_link):
    """Mates this object with a peer with which it will exchange tickets."""
    raise NotImplementedError()


class Front(ForeLink, interfaces.Front):
  """Clientish objects that operate by sending and receiving tickets."""
  __metaclass__ = abc.ABCMeta


class Back(RearLink, interfaces.Back):
  """Serverish objects that operate by sending and receiving tickets."""
  __metaclass__ = abc.ABCMeta
