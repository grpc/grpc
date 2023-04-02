gRPC Python Admin Interface Package
===================================

Debugging gRPC library can be a complex task. There are many configurations and
internal states, which will affect the behavior of the library. This Python
package will be the collection of admin services that are exposing debug
information. Currently, it includes:

* Channel tracing metrics (grpcio-channelz)
* Client Status Discovery Service (grpcio-csds)

Here is a snippet to create an admin server on "localhost:50051":

    server = grpc.server(ThreadPoolExecutor())
    port = server.add_insecure_port('localhost:50051')
    grpc_admin.add_admin_servicers(self._server)
    server.start()

Welcome to explore the admin services with CLI tool "grpcdebug":
https://github.com/grpc-ecosystem/grpcdebug.

For any issues or suggestions, please send to
https://github.com/grpc/grpc/issues.
