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

import time
import unittest

from grpc._cython import cygrpc


class CTypeSmokeTest(unittest.TestCase):

  def testStringsInUtilitiesUpDown(self):
    self.assertEqual(0, cygrpc.StatusCode.ok)
    metadatum = cygrpc.Metadatum('a', 'b')
    self.assertEqual('a'.encode(), metadatum.key)
    self.assertEqual('b'.encode(), metadatum.value)
    metadata = cygrpc.Metadata([metadatum])
    self.assertEqual(1, len(metadata))
    self.assertEqual(metadatum.key, metadata[0].key)

  def testClientCredentialsUpDown(self):
    credentials = cygrpc.ClientCredentials.fake_transport_security()
    del credentials

  def testServerCredentialsUpDown(self):
    credentials = cygrpc.ServerCredentials.fake_transport_security()
    del credentials

  def testCompletionQueueUpDown(self):
    completion_queue = cygrpc.CompletionQueue()
    del completion_queue

  def testServerUpDown(self):
    serv = cygrpc.Server(cygrpc.ChannelArgs([]))
    del serv

  def testChannelUpDown(self):
    channel = cygrpc.Channel('[::]:0', cygrpc.ChannelArgs([]))
    del channel

  def testSecureChannelUpDown(self):
    channel = cygrpc.Channel('[::]:0', cygrpc.ChannelArgs([]), cygrpc.ClientCredentials.fake_transport_security())
    del channel


if __name__ == '__main__':
  unittest.main(verbosity=2)
