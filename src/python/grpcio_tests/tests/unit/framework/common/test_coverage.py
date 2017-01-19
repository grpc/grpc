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
"""Governs coverage for tests of RPCs throughout RPC Framework."""

import abc

import six

# This code is designed for use with the unittest module.
# pylint: disable=invalid-name


class Coverage(six.with_metaclass(abc.ABCMeta)):
    """Specification of test coverage."""

    @abc.abstractmethod
    def testSuccessfulUnaryRequestUnaryResponse(self):
        raise NotImplementedError()

    @abc.abstractmethod
    def testSuccessfulUnaryRequestStreamResponse(self):
        raise NotImplementedError()

    @abc.abstractmethod
    def testSuccessfulStreamRequestUnaryResponse(self):
        raise NotImplementedError()

    @abc.abstractmethod
    def testSuccessfulStreamRequestStreamResponse(self):
        raise NotImplementedError()

    @abc.abstractmethod
    def testSequentialInvocations(self):
        raise NotImplementedError()

    @abc.abstractmethod
    def testParallelInvocations(self):
        raise NotImplementedError()

    @abc.abstractmethod
    def testWaitingForSomeButNotAllParallelInvocations(self):
        raise NotImplementedError()

    @abc.abstractmethod
    def testCancelledUnaryRequestUnaryResponse(self):
        raise NotImplementedError()

    @abc.abstractmethod
    def testCancelledUnaryRequestStreamResponse(self):
        raise NotImplementedError()

    @abc.abstractmethod
    def testCancelledStreamRequestUnaryResponse(self):
        raise NotImplementedError()

    @abc.abstractmethod
    def testCancelledStreamRequestStreamResponse(self):
        raise NotImplementedError()

    @abc.abstractmethod
    def testExpiredUnaryRequestUnaryResponse(self):
        raise NotImplementedError()

    @abc.abstractmethod
    def testExpiredUnaryRequestStreamResponse(self):
        raise NotImplementedError()

    @abc.abstractmethod
    def testExpiredStreamRequestUnaryResponse(self):
        raise NotImplementedError()

    @abc.abstractmethod
    def testExpiredStreamRequestStreamResponse(self):
        raise NotImplementedError()

    @abc.abstractmethod
    def testFailedUnaryRequestUnaryResponse(self):
        raise NotImplementedError()

    @abc.abstractmethod
    def testFailedUnaryRequestStreamResponse(self):
        raise NotImplementedError()

    @abc.abstractmethod
    def testFailedStreamRequestUnaryResponse(self):
        raise NotImplementedError()

    @abc.abstractmethod
    def testFailedStreamRequestStreamResponse(self):
        raise NotImplementedError()
