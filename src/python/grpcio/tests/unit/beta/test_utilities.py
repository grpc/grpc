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

"""Test-appropriate entry points into the gRPC Python Beta API."""

import time
from oauth2client import client as oauth2client_client
from oauth2client import crypt

from grpc._adapter import _intermediary_low
from grpc.beta import implementations

# TODO(kpayson) we should just add self_sign_jwt() to the 
#ServiceAccountCredentials in the oauth2 library, this is a placeholder
class JWTCredentials:
  def __init__(self, creds):
     self.creds = creds

  def self_sign_jwt(self, service_url):
    now = int(time.time())
    payload = {
      'aud': service_url,
      'iat': now,
      'exp': now + self.creds.MAX_TOKEN_LIFETIME_SECS,
      'iss': self.creds._service_account_email,
      'sub': self.creds._service_account_email
    }

    jwt = crypt.make_signed_jwt(self.creds._signer, payload, key_id=self.creds._private_key_id)
    return jwt
  
def not_really_secure_channel(
    host, port, channel_credentials, server_host_override):
  """Creates an insecure Channel to a remote host.

  Args:
    host: The name of the remote host to which to connect.
    port: The port of the remote host to which to connect.
    channel_credentials: The implementations.ChannelCredentials with which to
      connect.
    server_host_override: The target name used for SSL host name checking.

  Returns:
    An implementations.Channel to the remote host through which RPCs may be
      conducted.
  """
  hostport = '%s:%d' % (host, port)
  intermediary_low_channel = _intermediary_low.Channel(
      hostport, channel_credentials._low_credentials,
      server_host_override=server_host_override)
  return implementations.Channel(
      intermediary_low_channel._internal, intermediary_low_channel)
