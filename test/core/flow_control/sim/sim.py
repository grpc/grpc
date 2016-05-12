#!/usr/bin/env python2.7
# Copyright 2016, Google Inc.
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

"""Simulate global flow control"""

import argparse
import collections
import random
import sys

pending_events = collections.defaultdict(list)
# number of timesteps to get from client to server
LATENCY = 10
PROCESSING_TIME = 1
messages_received = 0


def sched(t, latency, what):
    global pending_events
    #print t+latency, t, latency, len(pending_events[t+latency])
    pending_events[t + latency].append(what)


class Connection(object):
    def __init__(self):
        self.window = 65535
        self.bdp = 65536.0
        self.bw_avail = 0
        self.advertised_window = self.window

    def add_window(self, t, amt):
        def add(t):
            self.window += amt
        self.advertised_window += amt
        sched(t, LATENCY, add)

    def del_window(self, t, amt):
        def sub(t):
            self.advertised_window -= amt
        assert amt <= self.window
        assert amt <= self.bw_avail
        self.window -= amt
        self.bw_avail -= amt
        sched(t, LATENCY, sub)

    def tick(self, t):
        self.bw_avail = int(self.bdp / LATENCY)


class Stream(object):
    def _enter_send_state(self, state, t, immediately):
        self.send_tick = state
        if not immediately:
          sched(t, 0, lambda tt: self.tick(tt))
        else:
          self.tick(t)

    def _send_idle(self, t):
        if not self.unflowed_queue: return
        self._enter_send_state(lambda tt: self._send_hdr(tt, 5), t, True)

    def _send_hdr(self, t, n):
        send = min(n, self.window, self.connection.window, self.connection.bw_avail)
        #print send, n, self.window, self.connection.window
        if send == 0: return
        self.del_window(t, send)
        self.connection.del_window(t, send)
        n -= send
        if n == 0:
            length = self.unflowed_queue[0][0]
            sched(t, LATENCY, lambda tt: self.recv_header(tt, length))
            self._enter_send_state(lambda tt: self._send_msg(tt, length), t, True)
        else:
            self._enter_send_state(lambda tt: self._send_hdr(tt, n), t, False)

    def _send_msg(self, t, n):
        send = min(16384, n, self.window, self.connection.window, self.connection.bw_avail)
        if send == 0: return
        self.del_window(t, send)
        self.connection.del_window(t, send)
        self._update_real_buffered_bytes(send)
        n -= send
        if n == 0:
            length = self.unflowed_queue[0][0]
            self._update_real_buffered_bytes(-length)
            def recv(tt):
              global messages_received
              messages_received += 1
              self.recv_message(tt, length)
            sched(t, LATENCY, recv)
            cb = self.unflowed_queue[0][1]
            sched(t, 0, lambda tt: cb())
            self.unflowed_queue = self.unflowed_queue[1:]
            self._enter_send_state(lambda tt: self._send_idle(tt), t, True)
        else:
            self._enter_send_state(lambda tt: self._send_msg(tt, n), t, False)

    def _update_real_buffered_bytes(self, amt):
        def adj(t):
            self.real_buffered_bytes += amt
        sched(t, LATENCY if amt > 0 else LATENCY + PROCESSING_TIME, adj)

    def __init__(self, connection):
        self.connection = connection
        self.send_tick = lambda t: self._send_idle(t)
        self.unflowed_queue = []
        self.window = 65535
        self.advertised_window = self.window
        self.real_buffered_bytes = 0

    def send_message(self, size, then):
        self.unflowed_queue.append((size, then))

    def tick(self, t):
        self.send_tick(t)

    def add_window(self, t, amt):
        def add(t):
            self.window += amt
        self.advertised_window += amt
        sched(t, LATENCY, add)

    def del_window(self, t, amt):
        def sub(t):
            self.advertised_window -= amt
        self.window -= amt
        sched(t, LATENCY, sub)

    def recv_header(self, t, length):
        pass

    def recv_message(self, t, length):
        pass


# Core flow control algorithm as it currently stands
class CoreStream(Stream):
    def __init__(self, connection):
        Stream.__init__(self, connection)
        self.add_window(0, 1024 * 1024 - self.window)

    def recv_header(self, t, length):
        self.add_window(t, 5 + length)


class CoreConnection(Connection):
    def __init__(self, window_target):
        Connection.__init__(self)
        self.window_target = window_target
        self.add_window(0, self.window_target - self.window)

    def tick(self, t):
        Connection.tick(self, t)
        if self.advertised_window <= 3 * self.window_target / 4:
            self.add_window(t, self.window_target - self.advertised_window)


# Advertise allocations: must allocate any flow control window before handing it
# out; optional (flag-controlled) retractions
class AdvPool(object):
  def __init__(self, with_retraction):
    self.avail = 500*1024*1024
    self.queue = []
    self.streams = []
    self.retraction_available = with_retraction

  def join(self, stream):
    self.avail -= stream.window
    self.streams.append(stream)

  def req(self, t, n, then):
    if not self.queue and self.avail >= n:
      self.avail -= n
      then(t)
    elif self.retraction_available:
      self.retraction_available = False
      for stream in self.streams:
        stream.retract(t)
      self.req(t, n, then)
    else:
      self.queue.append((n, then))

  def done(self, t, n):
    self.avail += n
    while self.queue and self.avail > self.queue[0][0]:
      self.avail -= self.queue[0][0]
      self.queue[0][1](t)
      self.queue = self.queue[1:]


class AdvStream(Stream):
    def __init__(self, connection, coordinator, manage_con_window):
      Stream.__init__(self, connection)
      self.coordinator = coordinator
      self.coordinator.join(self)
      self.manage_con_window = manage_con_window
      self.in_message = False

    def retract(self, t):
      if not self.in_message:
        self.add_window(t, -65530)
        sched(t, 2 * LATENCY, lambda tt: self.coordinator.done(tt, 65530))

    def recv_header(self, t, length):
      def adv(tt):
        self.add_window(tt, length+5)
        if self.manage_con_window:
          self.connection.add_window(tt, length+5)
      self.coordinator.done(t, 5)
      self.in_message = True
      self.coordinator.req(t, length+5, adv)

    def recv_message(self, t, length):
      self.coordinator.done(t, length)
      self.in_message = False


Impl = collections.namedtuple('Impl', ['stream', 'connection', 'coordinator'])
impls = {
    'core': Impl(stream = lambda con, coord: CoreStream(con),
                 connection = lambda coord: CoreConnection(1024*1024),
                 coordinator = lambda: None),
    'adv':  Impl(stream = lambda con, coord: AdvStream(con, coord, True),
                 connection = lambda coord: Connection(),
                 coordinator = lambda: AdvPool(False)),
    'advR': Impl(stream = lambda con, coord: AdvStream(con, coord, True),
                 connection = lambda coord: Connection(),
                 coordinator = lambda: AdvPool(True)),
    'win':  Impl(stream = lambda con, coord: AdvStream(con, coord, False),
                 connection = lambda coord: CoreConnection(65536),
                 coordinator = lambda: AdvPool(False)),
    'winR': Impl(stream = lambda con, coord: AdvStream(con, coord, False),
                 connection = lambda coord: CoreConnection(65536),
                 coordinator = lambda: AdvPool(True)),
    'bwnR': Impl(stream = lambda con, coord: AdvStream(con, coord, False),
                 connection = lambda coord: CoreConnection(1024*1024),
                 coordinator = lambda: AdvPool(True)),
}

parser = argparse.ArgumentParser(description='sim')
parser.add_argument('--impl', default='core', choices=sorted(impls.keys()))
parser.add_argument('--connections', default=1, type=int)
parser.add_argument('--streams_per_connection', default=1000, type=int)
parser.add_argument('--message_size', default = 100 * 1000 * 1000, type=int)
parser.add_argument('--wait_for', default = 20, type=int)
args = parser.parse_args()


t = 0
streams = []
cons = []
impl = impls[args.impl]
coord = impl.coordinator()
for i in range(0, args.connections):
  con = impl.connection(coord)
  cons.append(con)
  for j in range(0, args.streams_per_connection):
      streams.append(impl.stream(con, coord))
random.shuffle(streams)

def loopy_send(stream, size):
    stream.send_message(size, lambda: loopy_send(stream, size))

for stream in streams:
    loopy_send(stream, args.message_size)

latency = []
max_mem = 0
while len(latency) < args.wait_for:
    t += 1
    for con in cons:
        con.tick(t)
    for stream in streams:
        stream.tick(t)
    streams = streams[1:] + streams[0:1]
    while True:
        events = pending_events[t]
        pending_events[t] = []
        if not events: break
        for ev in events:
            ev(t)
    buf = 0
    for stream in streams:
        buf += stream.real_buffered_bytes
    if buf > max_mem:
      max_mem = buf
    while len(latency) < messages_received:
      latency.append(t)
      print len(latency), t
    #print "%d %d %d" % (t, messages_received, buf)
print 'max_mem: ', max_mem
