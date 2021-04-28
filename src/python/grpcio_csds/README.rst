gRPC Python Client Status Discovery Service package
===================================================

CSDS is part of the Envoy xDS protocol:
https://www.envoyproxy.io/docs/envoy/latest/api-v3/service/status/v3/csds.proto.
It allows the gRPC application to programmatically expose the received traffic
configuration (xDS resources). It's best to use with CLI tool "grpcdebug":
https://github.com/grpc-ecosystem/grpcdebug.
