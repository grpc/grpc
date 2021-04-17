Package "xds-protos" is a collection of ProtoBuf generated Python files for xDS protos (or the `data-plane-api <https://github.com/envoyproxy/data-plane-api>`_).

You can find the source code of this project in `grpc/grpc https://github.com/grpc/grpc`_. For any question or suggestion, please post on https://github.com/grpc/grpc/issues.

Each generated Python file can be imported according to their proto package. For example, if we are trying to import a proto located in "envoy/service/status/v3/csds.proto", we can import it as:

::

  # Import the message definitions
  from envoy.service.status.v3 import csds_pb2
  # Import the gRPC service and stub
  from envoy.service.status.v3 import csds_pb2_grpc
