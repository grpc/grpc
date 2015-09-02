@rem Generate the C# code for .proto files

setlocal

@rem enter this directory
cd /d %~dp0

packages\Google.Protobuf.3.0.0-alpha4\tools\protoc.exe -I../../protos --csharp_out RouteGuide  ../../protos/route_guide.proto --grpc_out RouteGuide --plugin=protoc-gen-grpc=packages\Grpc.Tools.0.7.0\tools\grpc_csharp_plugin.exe

endlocal