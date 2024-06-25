# Generated by the gRPC Python protocol compiler plugin. DO NOT EDIT!
"""Client and server classes corresponding to protobuf-defined services."""
import grpc
import warnings

import flow_control_pb2 as flow__control__pb2

GRPC_GENERATED_VERSION = '1.64.0'
GRPC_VERSION = grpc.__version__
EXPECTED_ERROR_RELEASE = '1.65.0'
SCHEDULED_RELEASE_DATE = 'June 25, 2024'
_version_not_supported = False

try:
    from grpc._utilities import first_version_is_lower
    _version_not_supported = first_version_is_lower(GRPC_VERSION, GRPC_GENERATED_VERSION)
except ImportError:
    _version_not_supported = True

if _version_not_supported:
    warnings.warn(
        f'The grpc package installed is at version {GRPC_VERSION},'
        + f' but the generated code in flow_control_pb2_grpc.py depends on'
        + f' grpcio>={GRPC_GENERATED_VERSION}.'
        + f' Please upgrade your grpc module to grpcio>={GRPC_GENERATED_VERSION}'
        + f' or downgrade your generated code using grpcio-tools<={GRPC_VERSION}.'
        + f' This warning will become an error in {EXPECTED_ERROR_RELEASE},'
        + f' scheduled for release on {SCHEDULED_RELEASE_DATE}.',
        RuntimeWarning
    )


class FlowControlStub(object):
    """Interface exported by the server.
    """

    def __init__(self, channel):
        """Constructor.

        Args:
            channel: A grpc.Channel.
        """
        self.BidiStreamingCall = channel.stream_stream(
                '/flowcontrol.FlowControl/BidiStreamingCall',
                request_serializer=flow__control__pb2.Request.SerializeToString,
                response_deserializer=flow__control__pb2.Reply.FromString,
                _registered_method=True)


class FlowControlServicer(object):
    """Interface exported by the server.
    """

    def BidiStreamingCall(self, request_iterator, context):
        """A Bidirectional streaming RPC.

        Accepts a stream of requests,
        and returns a stream of responses
        """
        context.set_code(grpc.StatusCode.UNIMPLEMENTED)
        context.set_details('Method not implemented!')
        raise NotImplementedError('Method not implemented!')


def add_FlowControlServicer_to_server(servicer, server):
    rpc_method_handlers = {
            'BidiStreamingCall': grpc.stream_stream_rpc_method_handler(
                    servicer.BidiStreamingCall,
                    request_deserializer=flow__control__pb2.Request.FromString,
                    response_serializer=flow__control__pb2.Reply.SerializeToString,
            ),
    }
    generic_handler = grpc.method_handlers_generic_handler(
            'flowcontrol.FlowControl', rpc_method_handlers)
    server.add_generic_rpc_handlers((generic_handler,))
    server.add_registered_method_handlers('flowcontrol.FlowControl', rpc_method_handlers)


 # This class is part of an EXPERIMENTAL API.
class FlowControl(object):
    """Interface exported by the server.
    """

    @staticmethod
    def BidiStreamingCall(request_iterator,
            target,
            options=(),
            channel_credentials=None,
            call_credentials=None,
            insecure=False,
            compression=None,
            wait_for_ready=None,
            timeout=None,
            metadata=None):
        return grpc.experimental.stream_stream(
            request_iterator,
            target,
            '/flowcontrol.FlowControl/BidiStreamingCall',
            flow__control__pb2.Request.SerializeToString,
            flow__control__pb2.Reply.FromString,
            options,
            channel_credentials,
            insecure,
            call_credentials,
            compression,
            wait_for_ready,
            timeout,
            metadata,
            _registered_method=True)
