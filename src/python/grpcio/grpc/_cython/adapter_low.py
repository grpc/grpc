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


# Adapter from grpc._cython.types to the surface expected by
# grpc._adapter._intermediary_low.
#
# TODO(atash): Once this is plugged into grpc._adapter._intermediary_low, remove
# both grpc._adapter._intermediary_low and this file. The fore and rear links in
# grpc._adapter should be able to use grpc._cython.types directly.

from grpc._adapter import _types as type_interfaces
from grpc._cython import cygrpc


class ClientCredentials(object):
  def __init__(self):
    raise NotImplementedError()

  @staticmethod
  def google_default():
    raise NotImplementedError()

  @staticmethod
  def ssl():
    raise NotImplementedError()

  @staticmethod
  def composite():
    raise NotImplementedError()

  @staticmethod
  def compute_engine():
    raise NotImplementedError()

  @staticmethod
  def jwt():
    raise NotImplementedError()

  @staticmethod
  def refresh_token():
    raise NotImplementedError()

  @staticmethod
  def iam():
    raise NotImplementedError()


class ServerCredentials(object):
  def __init__(self):
    raise NotImplementedError()

  @staticmethod
  def ssl():
    raise NotImplementedError()


class CompletionQueue(type_interfaces.CompletionQueue):
  def __init__(self):
    raise NotImplementedError()


class Call(type_interfaces.Call):
  def __init__(self):
    raise NotImplementedError()


class Channel(type_interfaces.Channel):
  def __init__(self):
    raise NotImplementedError()


class Server(type_interfaces.Server):
  def __init__(self):
    raise NotImplementedError()

