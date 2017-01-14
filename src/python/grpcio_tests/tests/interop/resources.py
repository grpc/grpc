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
"""Constants and functions for data used in interoperability testing."""

import argparse
import os

import pkg_resources

_ROOT_CERTIFICATES_RESOURCE_PATH = 'credentials/ca.pem'
_PRIVATE_KEY_RESOURCE_PATH = 'credentials/server1.key'
_CERTIFICATE_CHAIN_RESOURCE_PATH = 'credentials/server1.pem'


def test_root_certificates():
    return pkg_resources.resource_string(__name__,
                                         _ROOT_CERTIFICATES_RESOURCE_PATH)


def private_key():
    return pkg_resources.resource_string(__name__, _PRIVATE_KEY_RESOURCE_PATH)


def certificate_chain():
    return pkg_resources.resource_string(__name__,
                                         _CERTIFICATE_CHAIN_RESOURCE_PATH)


def parse_bool(value):
    if value == 'true':
        return True
    if value == 'false':
        return False
    raise argparse.ArgumentTypeError('Only true/false allowed')
