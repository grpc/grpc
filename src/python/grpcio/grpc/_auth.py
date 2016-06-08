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

"""GRPCAuthMetadataPlugins for standard authentication."""

from concurrent import futures

import grpc


def _sign_request(callback, token, error):
  metadata = (('authorization', 'Bearer {}'.format(token)),)
  callback(metadata, error)


class GoogleCallCredentials(grpc.AuthMetadataPlugin):
  """Metadata wrapper for GoogleCredentials from the oauth2client library."""

  def __init__(self, credentials):
    self._credentials = credentials
    self._pool = futures.ThreadPoolExecutor(max_workers=1)

  def __call__(self, context, callback):
    # MetadataPlugins cannot block (see grpc.beta.interfaces.py)
    future = self._pool.submit(self._credentials.get_access_token)
    future.add_done_callback(lambda x: self._get_token_callback(callback, x))

  def _get_token_callback(self, callback, future):
    try:
      access_token = future.result().access_token
    except Exception as e:
      _sign_request(callback, None, e)
    else:
      _sign_request(callback, access_token, None)

  def __del__(self):
    self._pool.shutdown(wait=False)


class AccessTokenCallCredentials(grpc.AuthMetadataPlugin):
  """Metadata wrapper for raw access token credentials."""

  def __init__(self, access_token):
    self._access_token = access_token

  def __call__(self, context, callback):
    _sign_request(callback, self._access_token, None)
