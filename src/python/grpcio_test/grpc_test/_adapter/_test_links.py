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

"""Links suitable for use in tests."""

import threading

from grpc.framework.base import interfaces


class ForeLink(interfaces.ForeLink):
  """A ForeLink suitable for use in tests of RearLinks."""

  def __init__(self, action, rear_link):
    self.condition = threading.Condition()
    self.tickets = []
    self.action = action
    self.rear_link = rear_link

  def accept_back_to_front_ticket(self, ticket):
    with self.condition:
      self.tickets.append(ticket)
      self.condition.notify_all()
      action, rear_link = self.action, self.rear_link

    if action is not None:
      action(ticket, rear_link)

  def join_rear_link(self, rear_link):
    with self.condition:
      self.rear_link = rear_link


class RearLink(interfaces.RearLink):
  """A RearLink suitable for use in tests of ForeLinks."""

  def __init__(self, action, fore_link):
    self.condition = threading.Condition()
    self.tickets = []
    self.action = action
    self.fore_link = fore_link

  def accept_front_to_back_ticket(self, ticket):
    with self.condition:
      self.tickets.append(ticket)
      self.condition.notify_all()
      action, fore_link = self.action, self.fore_link

    if action is not None:
      action(ticket, fore_link)

  def join_fore_link(self, fore_link):
    with self.condition:
      self.fore_link = fore_link
