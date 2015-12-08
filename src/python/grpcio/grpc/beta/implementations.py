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

"""Entry points into the Beta API of gRPC Python."""

# threading is referenced from specification in this module.
import abc
import enum
import threading  # pylint: disable=unused-import

# cardinality and face are referenced from specification in this module.
from grpc._adapter import _intermediary_low
from grpc._adapter import _low
from grpc._adapter import _types
from grpc.beta import _connectivity_channel
from grpc.beta import _server
from grpc.beta import _stub
from grpc.beta import interfaces
from grpc.framework.common import cardinality  # pylint: disable=unused-import
from grpc.framework.interfaces.face import face  # pylint: disable=unused-import

_CHANNEL_SUBSCRIPTION_CALLBACK_ERROR_LOG_MESSAGE = (
    'Exception calling channel subscription callback!')


class ChannelCredentials(object):
  """A value encapsulating the data required to create a secure Channel.

  This class and its instances have no supported interface - it exists to define
  the type of its instances and its instances exist to be passed to other
  functions.
  """

  def __init__(self, low_credentials):
    self._low_credentials = low_credentials


def ssl_channel_credentials(root_certificates, private_key, certificate_chain):
  """Creates a ChannelCredentials for use with an SSL-enabled Channel.

  Args:
    root_certificates: The PEM-encoded root certificates or None to ask for
      them to be retrieved from a default location.
    private_key: The PEM-encoded private key to use or None if no private key
      should be used.
    certificate_chain: The PEM-encoded certificate chain to use or None if no
      certificate chain should be used.

  Returns:
    A ChannelCredentials for use with an SSL-enabled Channel.
  """
  return ChannelCredentials(_low.channel_credentials_ssl(
      root_certificates, private_key, certificate_chain))


class CallCredentials(object):
  """A value encapsulating data asserting an identity over an *established*
  channel. May be composed with ChannelCredentials to always assert identity for
  every call over that channel.

  This class and its instances have no supported interface - it exists to define
  the type of its instances and its instances exist to be passed to other
  functions.
  """

  def __init__(self, low_credentials):
    self._low_credentials = low_credentials


def metadata_call_credentials(metadata_plugin, name=None):
  """Construct CallCredentials from an interfaces.GRPCAuthMetadataPlugin.

  Args:
    metadata_plugin: An interfaces.GRPCAuthMetadataPlugin to use in constructing
      the CallCredentials object.

  Returns:
    A CallCredentials object for use in a GRPCCallOptions object.
  """
  if name is None:
    name = metadata_plugin.__name__
  return CallCredentials(
      _low.call_credentials_metadata_plugin(metadata_plugin, name))

def composite_call_credentials(call_credentials, additional_call_credentials):
  """Compose two CallCredentials to make a new one.

  Args:
    call_credentials: A CallCredentials object.
    additional_call_credentials: Another CallCredentials object to compose on
      top of call_credentials.

  Returns:
    A CallCredentials object for use in a GRPCCallOptions object.
  """
  return CallCredentials(
      _low.call_credentials_composite(
          call_credentials._low_credentials,
          additional_call_credentials._low_credentials))

def composite_channel_credentials(channel_credentials,
                                 additional_call_credentials):
  """Compose ChannelCredentials on top of client credentials to make a new one.

  Args:
    channel_credentials: A ChannelCredentials object.
    additional_call_credentials: A CallCredentials object to compose on
      top of channel_credentials.

  Returns:
    A ChannelCredentials object for use in a GRPCCallOptions object.
  """
  return ChannelCredentials(
      _low.channel_credentials_composite(
          channel_credentials._low_credentials,
          additional_call_credentials._low_credentials))


class Channel(object):
  """A channel to a remote host through which RPCs may be conducted.

  Only the "subscribe" and "unsubscribe" methods are supported for application
  use. This class' instance constructor and all other attributes are
  unsupported.
  """

  def __init__(self, low_channel, intermediary_low_channel):
    self._low_channel = low_channel
    self._intermediary_low_channel = intermediary_low_channel
    self._connectivity_channel = _connectivity_channel.ConnectivityChannel(
        low_channel)

  def subscribe(self, callback, try_to_connect=None):
    """Subscribes to this Channel's connectivity.

    Args:
      callback: A callable to be invoked and passed an
        interfaces.ChannelConnectivity identifying this Channel's connectivity.
        The callable will be invoked immediately upon subscription and again for
        every change to this Channel's connectivity thereafter until it is
        unsubscribed.
      try_to_connect: A boolean indicating whether or not this Channel should
        attempt to connect if it is not already connected and ready to conduct
        RPCs.
    """
    self._connectivity_channel.subscribe(callback, try_to_connect)

  def unsubscribe(self, callback):
    """Unsubscribes a callback from this Channel's connectivity.

    Args:
      callback: A callable previously registered with this Channel from having
        been passed to its "subscribe" method.
    """
    self._connectivity_channel.unsubscribe(callback)


def insecure_channel(host, port):
  """Creates an insecure Channel to a remote host.

  Args:
    host: The name of the remote host to which to connect.
    port: The port of the remote host to which to connect.

  Returns:
    A Channel to the remote host through which RPCs may be conducted.
  """
  intermediary_low_channel = _intermediary_low.Channel(
      '%s:%d' % (host, port), None)
  return Channel(intermediary_low_channel._internal, intermediary_low_channel)  # pylint: disable=protected-access


def secure_channel(host, port, channel_credentials):
  """Creates a secure Channel to a remote host.

  Args:
    host: The name of the remote host to which to connect.
    port: The port of the remote host to which to connect.
    channel_credentials: A ChannelCredentials.

  Returns:
    A secure Channel to the remote host through which RPCs may be conducted.
  """
  intermediary_low_channel = _intermediary_low.Channel(
      '%s:%d' % (host, port), channel_credentials._low_credentials)
  return Channel(intermediary_low_channel._internal, intermediary_low_channel)  # pylint: disable=protected-access


class StubOptions(object):
  """A value encapsulating the various options for creation of a Stub.

  This class and its instances have no supported interface - it exists to define
  the type of its instances and its instances exist to be passed to other
  functions.
  """

  def __init__(
      self, host, request_serializers, response_deserializers,
      metadata_transformer, thread_pool, thread_pool_size):
    self.host = host
    self.request_serializers = request_serializers
    self.response_deserializers = response_deserializers
    self.metadata_transformer = metadata_transformer
    self.thread_pool = thread_pool
    self.thread_pool_size = thread_pool_size

_EMPTY_STUB_OPTIONS = StubOptions(
    None, None, None, None, None, None)


def stub_options(
    host=None, request_serializers=None, response_deserializers=None,
    metadata_transformer=None, thread_pool=None, thread_pool_size=None):
  """Creates a StubOptions value to be passed at stub creation.

  All parameters are optional and should always be passed by keyword.

  Args:
    host: A host string to set on RPC calls.
    request_serializers: A dictionary from service name-method name pair to
      request serialization behavior.
    response_deserializers: A dictionary from service name-method name pair to
      response deserialization behavior.
    metadata_transformer: A callable that given a metadata object produces
      another metadata object to be used in the underlying communication on the
      wire.
    thread_pool: A thread pool to use in stubs.
    thread_pool_size: The size of thread pool to create for use in stubs;
      ignored if thread_pool has been passed.

  Returns:
    A StubOptions value created from the passed parameters.
  """
  return StubOptions(
      host, request_serializers, response_deserializers,
      metadata_transformer, thread_pool, thread_pool_size)


def generic_stub(channel, options=None):
  """Creates a face.GenericStub on which RPCs can be made.

  Args:
    channel: A Channel for use by the created stub.
    options: A StubOptions customizing the created stub.

  Returns:
    A face.GenericStub on which RPCs can be made.
  """
  effective_options = _EMPTY_STUB_OPTIONS if options is None else options
  return _stub.generic_stub(
      channel._intermediary_low_channel, effective_options.host,  # pylint: disable=protected-access
      effective_options.metadata_transformer,
      effective_options.request_serializers,
      effective_options.response_deserializers, effective_options.thread_pool,
      effective_options.thread_pool_size)


def dynamic_stub(channel, service, cardinalities, options=None):
  """Creates a face.DynamicStub with which RPCs can be invoked.

  Args:
    channel: A Channel for the returned face.DynamicStub to use.
    service: The package-qualified full name of the service.
    cardinalities: A dictionary from RPC method name to cardinality.Cardinality
      value identifying the cardinality of the RPC method.
    options: An optional StubOptions value further customizing the functionality
      of the returned face.DynamicStub.

  Returns:
    A face.DynamicStub with which RPCs can be invoked.
  """
  effective_options = StubOptions() if options is None else options
  return _stub.dynamic_stub(
      channel._intermediary_low_channel, effective_options.host, service,  # pylint: disable=protected-access
      cardinalities, effective_options.metadata_transformer,
      effective_options.request_serializers,
      effective_options.response_deserializers, effective_options.thread_pool,
      effective_options.thread_pool_size)


class ServerCredentials(object):
  """A value encapsulating the data required to open a secure port on a Server.

  This class and its instances have no supported interface - it exists to define
  the type of its instances and its instances exist to be passed to other
  functions.
  """

  def __init__(self, low_credentials):
    self._low_credentials = low_credentials


def ssl_server_credentials(
    private_key_certificate_chain_pairs, root_certificates=None,
    require_client_auth=False):
  """Creates a ServerCredentials for use with an SSL-enabled Server.

  Args:
    private_key_certificate_chain_pairs: A nonempty sequence each element of
      which is a pair the first element of which is a PEM-encoded private key
      and the second element of which is the corresponding PEM-encoded
      certificate chain.
    root_certificates: PEM-encoded client root certificates to be used for
      verifying authenticated clients. If omitted, require_client_auth must also
      be omitted or be False.
    require_client_auth: A boolean indicating whether or not to require clients
      to be authenticated. May only be True if root_certificates is not None.

  Returns:
    A ServerCredentials for use with an SSL-enabled Server.
  """
  if len(private_key_certificate_chain_pairs) == 0:
    raise ValueError(
        'At least one private key-certificate chain pairis required!')
  elif require_client_auth and root_certificates is None:
    raise ValueError(
        'Illegal to require client auth without providing root certificates!')
  else:
    return ServerCredentials(_low.server_credentials_ssl(
        root_certificates, private_key_certificate_chain_pairs,
        require_client_auth))


class ServerOptions(object):
  """A value encapsulating the various options for creation of a Server.

  This class and its instances have no supported interface - it exists to define
  the type of its instances and its instances exist to be passed to other
  functions.
  """

  def __init__(
      self, multi_method_implementation, request_deserializers,
      response_serializers, thread_pool, thread_pool_size, default_timeout,
      maximum_timeout):
    self.multi_method_implementation = multi_method_implementation
    self.request_deserializers = request_deserializers
    self.response_serializers = response_serializers
    self.thread_pool = thread_pool
    self.thread_pool_size = thread_pool_size
    self.default_timeout = default_timeout
    self.maximum_timeout = maximum_timeout

_EMPTY_SERVER_OPTIONS = ServerOptions(
    None, None, None, None, None, None, None)


def server_options(
    multi_method_implementation=None, request_deserializers=None,
    response_serializers=None, thread_pool=None, thread_pool_size=None,
    default_timeout=None, maximum_timeout=None):
  """Creates a ServerOptions value to be passed at server creation.

  All parameters are optional and should always be passed by keyword.

  Args:
    multi_method_implementation: A face.MultiMethodImplementation to be called
      to service an RPC if the server has no specific method implementation for
      the name of the RPC for which service was requested.
    request_deserializers: A dictionary from service name-method name pair to
      request deserialization behavior.
    response_serializers: A dictionary from service name-method name pair to
      response serialization behavior.
    thread_pool: A thread pool to use in stubs.
    thread_pool_size: The size of thread pool to create for use in stubs;
      ignored if thread_pool has been passed.
    default_timeout: A duration in seconds to allow for RPC service when
      servicing RPCs that did not include a timeout value when invoked.
    maximum_timeout: A duration in seconds to allow for RPC service when
      servicing RPCs no matter what timeout value was passed when the RPC was
      invoked.

  Returns:
    A StubOptions value created from the passed parameters.
  """
  return ServerOptions(
      multi_method_implementation, request_deserializers, response_serializers,
      thread_pool, thread_pool_size, default_timeout, maximum_timeout)


def server(service_implementations, options=None):
  """Creates an interfaces.Server with which RPCs can be serviced.

  Args:
    service_implementations: A dictionary from service name-method name pair to
      face.MethodImplementation.
    options: An optional ServerOptions value further customizing the
      functionality of the returned Server.

  Returns:
    An interfaces.Server with which RPCs can be serviced.
  """
  effective_options = _EMPTY_SERVER_OPTIONS if options is None else options
  return _server.server(
      service_implementations, effective_options.multi_method_implementation,
      effective_options.request_deserializers,
      effective_options.response_serializers, effective_options.thread_pool,
      effective_options.thread_pool_size, effective_options.default_timeout,
      effective_options.maximum_timeout)
