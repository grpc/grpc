@rem Generate the C# code for .proto files

setlocal

@rem enter this directory
cd /d %~dp0

set TOOLS_PATH=packages\Grpc.Tools.0.14.0\tools\windows_x86

%TOOLS_PATH%\protoc.exe -I../../protos --csharp_out Greeter  ../../protos/helloworld.proto --grpc_out Greeter --plugin=protoc-gen-grpc=%TOOLS_PATH%\grpc_csharp_plugin.exe

endlocal