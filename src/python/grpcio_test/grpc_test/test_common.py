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

"""Common code used throughout tests of gRPC."""

import collections


def metadata_transmitted(original_metadata, transmitted_metadata):
  """Judges whether or not metadata was acceptably transmitted.

  gRPC is allowed to insert key-value pairs into the metadata values given by
  applications and to reorder key-value pairs with different keys but it is not
  allowed to alter existing key-value pairs or to reorder key-value pairs with
  the same key.

  Args:
    original_metadata: A metadata value used in a test of gRPC.
    transmitted_metadata: A metadata value corresponding to original_metadata
      after having been transmitted via gRPC.

  Returns:
     A boolean indicating whether transmitted_metadata accurately reflects
      original_metadata after having been transmitted via gRPC.
  """
  original = collections.defaultdict(list)
  for key, value in original_metadata:
    original[key].append(value)
  transmitted = collections.defaultdict(list)
  for key, value in transmitted_metadata:
    transmitted[key].append(value)

  for key, values in original.iteritems():
    transmitted_values = transmitted[key]
    transmitted_iterator = iter(transmitted_values)
    try:
      for value in values:
        while True:
          transmitted_value = next(transmitted_iterator)
          if value == transmitted_value:
            break
    except StopIteration:
      return False
  else:
    return True
