// <auto-generated>
//     Generated by the protocol buffer compiler.  DO NOT EDIT!
//     source: grpc/reflection/v1alpha/reflection.proto
// </auto-generated>
// Original file comments:
// Copyright 2016 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// Service exported by server reflection
//
#pragma warning disable 0414, 1591
#region Designer generated code

using grpc = global::Grpc.Core;

namespace Grpc.Reflection.V1Alpha {
  public static partial class ServerReflection
  {
    static readonly string __ServiceName = "grpc.reflection.v1alpha.ServerReflection";

    static readonly grpc::Marshaller<global::Grpc.Reflection.V1Alpha.ServerReflectionRequest> __Marshaller_grpc_reflection_v1alpha_ServerReflectionRequest = grpc::Marshallers.Create((arg) => global::Google.Protobuf.MessageExtensions.ToByteArray(arg), global::Grpc.Reflection.V1Alpha.ServerReflectionRequest.Parser.ParseFrom);
    static readonly grpc::Marshaller<global::Grpc.Reflection.V1Alpha.ServerReflectionResponse> __Marshaller_grpc_reflection_v1alpha_ServerReflectionResponse = grpc::Marshallers.Create((arg) => global::Google.Protobuf.MessageExtensions.ToByteArray(arg), global::Grpc.Reflection.V1Alpha.ServerReflectionResponse.Parser.ParseFrom);

    static readonly grpc::Method<global::Grpc.Reflection.V1Alpha.ServerReflectionRequest, global::Grpc.Reflection.V1Alpha.ServerReflectionResponse> __Method_ServerReflectionInfo = new grpc::Method<global::Grpc.Reflection.V1Alpha.ServerReflectionRequest, global::Grpc.Reflection.V1Alpha.ServerReflectionResponse>(
        grpc::MethodType.DuplexStreaming,
        __ServiceName,
        "ServerReflectionInfo",
        __Marshaller_grpc_reflection_v1alpha_ServerReflectionRequest,
        __Marshaller_grpc_reflection_v1alpha_ServerReflectionResponse);

    /// <summary>Service descriptor</summary>
    public static global::Google.Protobuf.Reflection.ServiceDescriptor Descriptor
    {
      get { return global::Grpc.Reflection.V1Alpha.ReflectionReflection.Descriptor.Services[0]; }
    }

    /// <summary>Base class for server-side implementations of ServerReflection</summary>
    public abstract partial class ServerReflectionBase
    {
      /// <summary>
      /// The reflection service is structured as a bidirectional stream, ensuring
      /// all related requests go to a single server.
      /// </summary>
      /// <param name="requestStream">Used for reading requests from the client.</param>
      /// <param name="responseStream">Used for sending responses back to the client.</param>
      /// <param name="context">The context of the server-side call handler being invoked.</param>
      /// <returns>A task indicating completion of the handler.</returns>
      public virtual global::System.Threading.Tasks.Task ServerReflectionInfo(grpc::IAsyncStreamReader<global::Grpc.Reflection.V1Alpha.ServerReflectionRequest> requestStream, grpc::IServerStreamWriter<global::Grpc.Reflection.V1Alpha.ServerReflectionResponse> responseStream, grpc::ServerCallContext context)
      {
        throw new grpc::RpcException(new grpc::Status(grpc::StatusCode.Unimplemented, ""));
      }

    }

    /// <summary>Creates service definition that can be registered with a server</summary>
    /// <param name="serviceImpl">An object implementing the server-side handling logic.</param>
    public static grpc::ServerServiceDefinition BindService(ServerReflectionBase serviceImpl)
    {
      return grpc::ServerServiceDefinition.CreateBuilder()
          .AddMethod(__Method_ServerReflectionInfo, serviceImpl.ServerReflectionInfo).Build();
    }

    /// <summary>Register service method with a service binder with or without implementation. Useful when customizing the  service binding logic.
    /// Note: this method is part of an experimental API that can change or be removed without any prior notice.</summary>
    /// <param name="serviceBinder">Service methods will be bound by calling <c>AddMethod</c> on this object.</param>
    /// <param name="serviceImpl">An object implementing the server-side handling logic.</param>
    public static void BindService(grpc::ServiceBinderBase serviceBinder, ServerReflectionBase serviceImpl)
    {
      serviceBinder.AddMethod(__Method_ServerReflectionInfo, serviceImpl == null ? null : new grpc::DuplexStreamingServerMethod<global::Grpc.Reflection.V1Alpha.ServerReflectionRequest, global::Grpc.Reflection.V1Alpha.ServerReflectionResponse>(serviceImpl.ServerReflectionInfo));
    }

  }
}
#endregion
