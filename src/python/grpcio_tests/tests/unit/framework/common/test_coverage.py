# Copyright 2015 gRPC authors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
"""Governs coverage for tests of RPCs throughout RPC Framework."""

import abc

# This code is designed for use with the unittest module.
# pylint: disable=invalid-name


class Coverage(abc.ABC):
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
