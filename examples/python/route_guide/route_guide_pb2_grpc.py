import grpc
from grpc.framework.common import cardinality
from grpc.framework.interfaces.face import utilities as face_utilities

import route_guide_pb2 as route__guide__pb2


class RouteGuideStub(object):
  """Interface exported by the server.
  """

  def __init__(self, channel):
    """Constructor.

    Args:
      channel: A grpc.Channel.
    """
    self.GetFeature = channel.unary_unary(
        '/routeguide.RouteGuide/GetFeature',
        request_serializer=route__guide__pb2.Point.SerializeToString,
        response_deserializer=route__guide__pb2.Feature.FromString,
        )
    self.ListFeatures = channel.unary_stream(
        '/routeguide.RouteGuide/ListFeatures',
        request_serializer=route__guide__pb2.Rectangle.SerializeToString,
        response_deserializer=route__guide__pb2.Feature.FromString,
        )
    self.RecordRoute = channel.stream_unary(
        '/routeguide.RouteGuide/RecordRoute',
        request_serializer=route__guide__pb2.Point.SerializeToString,
        response_deserializer=route__guide__pb2.RouteSummary.FromString,
        )
    self.RouteChat = channel.stream_stream(
        '/routeguide.RouteGuide/RouteChat',
        request_serializer=route__guide__pb2.RouteNote.SerializeToString,
        response_deserializer=route__guide__pb2.RouteNote.FromString,
        )


class RouteGuideServicer(object):
  """Interface exported by the server.
  """

  def GetFeature(self, request, context):
    """A simple RPC.

    Obtains the feature at a given position.

    A feature with an empty name is returned if there's no feature at the given
    position.
    """
    context.set_code(grpc.StatusCode.UNIMPLEMENTED)
    context.set_details('Method not implemented!')
    raise NotImplementedError('Method not implemented!')

  def ListFeatures(self, request, context):
    """A server-to-client streaming RPC.

    Obtains the Features available within the given Rectangle.  Results are
    streamed rather than returned at once (e.g. in a response message with a
    repeated field), as the rectangle may cover a large area and contain a
    huge number of features.
    """
    context.set_code(grpc.StatusCode.UNIMPLEMENTED)
    context.set_details('Method not implemented!')
    raise NotImplementedError('Method not implemented!')

  def RecordRoute(self, request_iterator, context):
    """A client-to-server streaming RPC.

    Accepts a stream of Points on a route being traversed, returning a
    RouteSummary when traversal is completed.
    """
    context.set_code(grpc.StatusCode.UNIMPLEMENTED)
    context.set_details('Method not implemented!')
    raise NotImplementedError('Method not implemented!')

  def RouteChat(self, request_iterator, context):
    """A Bidirectional streaming RPC.

    Accepts a stream of RouteNotes sent while a route is being traversed,
    while receiving other RouteNotes (e.g. from other users).
    """
    context.set_code(grpc.StatusCode.UNIMPLEMENTED)
    context.set_details('Method not implemented!')
    raise NotImplementedError('Method not implemented!')


def add_RouteGuideServicer_to_server(servicer, server):
  rpc_method_handlers = {
      'GetFeature': grpc.unary_unary_rpc_method_handler(
          servicer.GetFeature,
          request_deserializer=route__guide__pb2.Point.FromString,
          response_serializer=route__guide__pb2.Feature.SerializeToString,
      ),
      'ListFeatures': grpc.unary_stream_rpc_method_handler(
          servicer.ListFeatures,
          request_deserializer=route__guide__pb2.Rectangle.FromString,
          response_serializer=route__guide__pb2.Feature.SerializeToString,
      ),
      'RecordRoute': grpc.stream_unary_rpc_method_handler(
          servicer.RecordRoute,
          request_deserializer=route__guide__pb2.Point.FromString,
          response_serializer=route__guide__pb2.RouteSummary.SerializeToString,
      ),
      'RouteChat': grpc.stream_stream_rpc_method_handler(
          servicer.RouteChat,
          request_deserializer=route__guide__pb2.RouteNote.FromString,
          response_serializer=route__guide__pb2.RouteNote.SerializeToString,
      ),
  }
  generic_handler = grpc.method_handlers_generic_handler(
      'routeguide.RouteGuide', rpc_method_handlers)
  server.add_generic_rpc_handlers((generic_handler,))
