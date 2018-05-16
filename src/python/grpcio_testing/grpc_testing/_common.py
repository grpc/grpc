# Copyright 2017 gRPC authors.
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
"""Common interfaces and implementation."""

import abc
import collections

import six


def _fuss(tuplified_metadata):
    return tuplified_metadata + ((
        'grpc.metadata_added_by_runtime',
        'gRPC is allowed to add metadata in transmission and does so.',
    ),)


FUSSED_EMPTY_METADATA = _fuss(())


def fuss_with_metadata(metadata):
    if metadata is None:
        return FUSSED_EMPTY_METADATA
    else:
        return _fuss(tuple(metadata))


def rpc_names(service_descriptors):
    rpc_names_to_descriptors = {}
    for service_descriptor in service_descriptors:
        for method_descriptor in service_descriptor.methods_by_name.values():
            rpc_name = '/{}/{}'.format(service_descriptor.full_name,
                                       method_descriptor.name)
            rpc_names_to_descriptors[rpc_name] = method_descriptor
    return rpc_names_to_descriptors


class ChannelRpcRead(
        collections.namedtuple('ChannelRpcRead', (
            'response',
            'trailing_metadata',
            'code',
            'details',
        ))):
    pass


class ChannelRpcHandler(six.with_metaclass(abc.ABCMeta)):

    @abc.abstractmethod
    def initial_metadata(self):
        raise NotImplementedError()

    @abc.abstractmethod
    def add_request(self, request):
        raise NotImplementedError()

    @abc.abstractmethod
    def close_requests(self):
        raise NotImplementedError()

    @abc.abstractmethod
    def take_response(self):
        raise NotImplementedError()

    @abc.abstractmethod
    def cancel(self, code, details):
        raise NotImplementedError()

    @abc.abstractmethod
    def termination(self):
        raise NotImplementedError()

    @abc.abstractmethod
    def is_active(self):
        raise NotImplementedError()

    @abc.abstractmethod
    def time_remaining(self):
        raise NotImplementedError()

    @abc.abstractmethod
    def add_callback(self, callback):
        raise NotImplementedError()


class ChannelHandler(six.with_metaclass(abc.ABCMeta)):

    @abc.abstractmethod
    def invoke_rpc(self, method_full_rpc_name, invocation_metadata, requests,
                   requests_closed, timeout):
        raise NotImplementedError()


class ServerRpcRead(
        collections.namedtuple('ServerRpcRead', (
            'request',
            'requests_closed',
            'terminated',
        ))):
    pass


REQUESTS_CLOSED = ServerRpcRead(None, True, False)
TERMINATED = ServerRpcRead(None, False, True)


class ServerRpcHandler(six.with_metaclass(abc.ABCMeta)):

    @abc.abstractmethod
    def send_initial_metadata(self, initial_metadata):
        raise NotImplementedError()

    @abc.abstractmethod
    def take_request(self):
        raise NotImplementedError()

    @abc.abstractmethod
    def add_response(self, response):
        raise NotImplementedError()

    @abc.abstractmethod
    def send_termination(self, trailing_metadata, code, details):
        raise NotImplementedError()

    @abc.abstractmethod
    def add_termination_callback(self, callback):
        raise NotImplementedError()


class Serverish(six.with_metaclass(abc.ABCMeta)):

    @abc.abstractmethod
    def invoke_unary_unary(self, method_descriptor, handler,
                           invocation_metadata, request, deadline):
        raise NotImplementedError()

    @abc.abstractmethod
    def invoke_unary_stream(self, method_descriptor, handler,
                            invocation_metadata, request, deadline):
        raise NotImplementedError()

    @abc.abstractmethod
    def invoke_stream_unary(self, method_descriptor, handler,
                            invocation_metadata, deadline):
        raise NotImplementedError()

    @abc.abstractmethod
    def invoke_stream_stream(self, method_descriptor, handler,
                             invocation_metadata, deadline):
        raise NotImplementedError()
