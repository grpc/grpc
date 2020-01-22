// <auto-generated>
//     Generated by the protocol buffer compiler.  DO NOT EDIT!
//     source: grpc/health/v1/health.proto
// </auto-generated>
// Original file comments:
// Copyright 2015 The gRPC Authors
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
// The canonical version of this proto can be found at
// https://github.com/grpc/grpc-proto/blob/master/grpc/health/v1/health.proto
//
#pragma warning disable 0414, 1591
#region Designer generated code

using grpc = global::Grpc.Core;

namespace Grpc.Health.V1 {
  public static partial class Health
  {
    static readonly string __ServiceName = "grpc.health.v1.Health";

    #if !GOOGLE_PROTOBUF_DISABLE_BUFFER_SERIALIZATION
    static readonly grpc::Marshaller<global::Grpc.Health.V1.HealthCheckRequest> __Marshaller_grpc_health_v1_HealthCheckRequest = 
      grpc::Marshallers.Create(
        (arg, context) =>
        {
          var writer = new global::Google.Protobuf.CodedOutputWriter(context.GetBufferWriter());
          arg.WriteTo(ref writer);
          writer.Flush();
          context.Complete();
        },
        context => global::Grpc.Health.V1.HealthCheckRequest.Parser.ParseFrom(context.PayloadAsReadOnlySequence()));
    static readonly grpc::Marshaller<global::Grpc.Health.V1.HealthCheckResponse> __Marshaller_grpc_health_v1_HealthCheckResponse = 
      grpc::Marshallers.Create(
        (arg, context) =>
        {
          var writer = new global::Google.Protobuf.CodedOutputWriter(context.GetBufferWriter());
          arg.WriteTo(ref writer);
          writer.Flush();
          context.Complete();
        },
        context => global::Grpc.Health.V1.HealthCheckResponse.Parser.ParseFrom(context.PayloadAsReadOnlySequence()));
    #else
    static readonly grpc::Marshaller<global::Grpc.Health.V1.HealthCheckRequest> __Marshaller_grpc_health_v1_HealthCheckRequest = grpc::Marshallers.Create((arg) => global::Google.Protobuf.MessageExtensions.ToByteArray(arg), global::Grpc.Health.V1.HealthCheckRequest.Parser.ParseFrom);
    static readonly grpc::Marshaller<global::Grpc.Health.V1.HealthCheckResponse> __Marshaller_grpc_health_v1_HealthCheckResponse = grpc::Marshallers.Create((arg) => global::Google.Protobuf.MessageExtensions.ToByteArray(arg), global::Grpc.Health.V1.HealthCheckResponse.Parser.ParseFrom);
    #endif

    static readonly grpc::Method<global::Grpc.Health.V1.HealthCheckRequest, global::Grpc.Health.V1.HealthCheckResponse> __Method_Check = new grpc::Method<global::Grpc.Health.V1.HealthCheckRequest, global::Grpc.Health.V1.HealthCheckResponse>(
        grpc::MethodType.Unary,
        __ServiceName,
        "Check",
        __Marshaller_grpc_health_v1_HealthCheckRequest,
        __Marshaller_grpc_health_v1_HealthCheckResponse);

    static readonly grpc::Method<global::Grpc.Health.V1.HealthCheckRequest, global::Grpc.Health.V1.HealthCheckResponse> __Method_Watch = new grpc::Method<global::Grpc.Health.V1.HealthCheckRequest, global::Grpc.Health.V1.HealthCheckResponse>(
        grpc::MethodType.ServerStreaming,
        __ServiceName,
        "Watch",
        __Marshaller_grpc_health_v1_HealthCheckRequest,
        __Marshaller_grpc_health_v1_HealthCheckResponse);

    /// <summary>Service descriptor</summary>
    public static global::Google.Protobuf.Reflection.ServiceDescriptor Descriptor
    {
      get { return global::Grpc.Health.V1.HealthReflection.Descriptor.Services[0]; }
    }

    /// <summary>Base class for server-side implementations of Health</summary>
    [grpc::BindServiceMethod(typeof(Health), "BindService")]
    public abstract partial class HealthBase
    {
      /// <summary>
      /// If the requested service is unknown, the call will fail with status
      /// NOT_FOUND.
      /// </summary>
      /// <param name="request">The request received from the client.</param>
      /// <param name="context">The context of the server-side call handler being invoked.</param>
      /// <returns>The response to send back to the client (wrapped by a task).</returns>
      public virtual global::System.Threading.Tasks.Task<global::Grpc.Health.V1.HealthCheckResponse> Check(global::Grpc.Health.V1.HealthCheckRequest request, grpc::ServerCallContext context)
      {
        throw new grpc::RpcException(new grpc::Status(grpc::StatusCode.Unimplemented, ""));
      }

      /// <summary>
      /// Performs a watch for the serving status of the requested service.
      /// The server will immediately send back a message indicating the current
      /// serving status.  It will then subsequently send a new message whenever
      /// the service's serving status changes.
      ///
      /// If the requested service is unknown when the call is received, the
      /// server will send a message setting the serving status to
      /// SERVICE_UNKNOWN but will *not* terminate the call.  If at some
      /// future point, the serving status of the service becomes known, the
      /// server will send a new message with the service's serving status.
      ///
      /// If the call terminates with status UNIMPLEMENTED, then clients
      /// should assume this method is not supported and should not retry the
      /// call.  If the call terminates with any other status (including OK),
      /// clients should retry the call with appropriate exponential backoff.
      /// </summary>
      /// <param name="request">The request received from the client.</param>
      /// <param name="responseStream">Used for sending responses back to the client.</param>
      /// <param name="context">The context of the server-side call handler being invoked.</param>
      /// <returns>A task indicating completion of the handler.</returns>
      public virtual global::System.Threading.Tasks.Task Watch(global::Grpc.Health.V1.HealthCheckRequest request, grpc::IServerStreamWriter<global::Grpc.Health.V1.HealthCheckResponse> responseStream, grpc::ServerCallContext context)
      {
        throw new grpc::RpcException(new grpc::Status(grpc::StatusCode.Unimplemented, ""));
      }

    }

    /// <summary>Client for Health</summary>
    public partial class HealthClient : grpc::ClientBase<HealthClient>
    {
      /// <summary>Creates a new client for Health</summary>
      /// <param name="channel">The channel to use to make remote calls.</param>
      public HealthClient(grpc::ChannelBase channel) : base(channel)
      {
      }
      /// <summary>Creates a new client for Health that uses a custom <c>CallInvoker</c>.</summary>
      /// <param name="callInvoker">The callInvoker to use to make remote calls.</param>
      public HealthClient(grpc::CallInvoker callInvoker) : base(callInvoker)
      {
      }
      /// <summary>Protected parameterless constructor to allow creation of test doubles.</summary>
      protected HealthClient() : base()
      {
      }
      /// <summary>Protected constructor to allow creation of configured clients.</summary>
      /// <param name="configuration">The client configuration.</param>
      protected HealthClient(ClientBaseConfiguration configuration) : base(configuration)
      {
      }

      /// <summary>
      /// If the requested service is unknown, the call will fail with status
      /// NOT_FOUND.
      /// </summary>
      /// <param name="request">The request to send to the server.</param>
      /// <param name="headers">The initial metadata to send with the call. This parameter is optional.</param>
      /// <param name="deadline">An optional deadline for the call. The call will be cancelled if deadline is hit.</param>
      /// <param name="cancellationToken">An optional token for canceling the call.</param>
      /// <returns>The response received from the server.</returns>
      public virtual global::Grpc.Health.V1.HealthCheckResponse Check(global::Grpc.Health.V1.HealthCheckRequest request, grpc::Metadata headers = null, global::System.DateTime? deadline = null, global::System.Threading.CancellationToken cancellationToken = default(global::System.Threading.CancellationToken))
      {
        return Check(request, new grpc::CallOptions(headers, deadline, cancellationToken));
      }
      /// <summary>
      /// If the requested service is unknown, the call will fail with status
      /// NOT_FOUND.
      /// </summary>
      /// <param name="request">The request to send to the server.</param>
      /// <param name="options">The options for the call.</param>
      /// <returns>The response received from the server.</returns>
      public virtual global::Grpc.Health.V1.HealthCheckResponse Check(global::Grpc.Health.V1.HealthCheckRequest request, grpc::CallOptions options)
      {
        return CallInvoker.BlockingUnaryCall(__Method_Check, null, options, request);
      }
      /// <summary>
      /// If the requested service is unknown, the call will fail with status
      /// NOT_FOUND.
      /// </summary>
      /// <param name="request">The request to send to the server.</param>
      /// <param name="headers">The initial metadata to send with the call. This parameter is optional.</param>
      /// <param name="deadline">An optional deadline for the call. The call will be cancelled if deadline is hit.</param>
      /// <param name="cancellationToken">An optional token for canceling the call.</param>
      /// <returns>The call object.</returns>
      public virtual grpc::AsyncUnaryCall<global::Grpc.Health.V1.HealthCheckResponse> CheckAsync(global::Grpc.Health.V1.HealthCheckRequest request, grpc::Metadata headers = null, global::System.DateTime? deadline = null, global::System.Threading.CancellationToken cancellationToken = default(global::System.Threading.CancellationToken))
      {
        return CheckAsync(request, new grpc::CallOptions(headers, deadline, cancellationToken));
      }
      /// <summary>
      /// If the requested service is unknown, the call will fail with status
      /// NOT_FOUND.
      /// </summary>
      /// <param name="request">The request to send to the server.</param>
      /// <param name="options">The options for the call.</param>
      /// <returns>The call object.</returns>
      public virtual grpc::AsyncUnaryCall<global::Grpc.Health.V1.HealthCheckResponse> CheckAsync(global::Grpc.Health.V1.HealthCheckRequest request, grpc::CallOptions options)
      {
        return CallInvoker.AsyncUnaryCall(__Method_Check, null, options, request);
      }
      /// <summary>
      /// Performs a watch for the serving status of the requested service.
      /// The server will immediately send back a message indicating the current
      /// serving status.  It will then subsequently send a new message whenever
      /// the service's serving status changes.
      ///
      /// If the requested service is unknown when the call is received, the
      /// server will send a message setting the serving status to
      /// SERVICE_UNKNOWN but will *not* terminate the call.  If at some
      /// future point, the serving status of the service becomes known, the
      /// server will send a new message with the service's serving status.
      ///
      /// If the call terminates with status UNIMPLEMENTED, then clients
      /// should assume this method is not supported and should not retry the
      /// call.  If the call terminates with any other status (including OK),
      /// clients should retry the call with appropriate exponential backoff.
      /// </summary>
      /// <param name="request">The request to send to the server.</param>
      /// <param name="headers">The initial metadata to send with the call. This parameter is optional.</param>
      /// <param name="deadline">An optional deadline for the call. The call will be cancelled if deadline is hit.</param>
      /// <param name="cancellationToken">An optional token for canceling the call.</param>
      /// <returns>The call object.</returns>
      public virtual grpc::AsyncServerStreamingCall<global::Grpc.Health.V1.HealthCheckResponse> Watch(global::Grpc.Health.V1.HealthCheckRequest request, grpc::Metadata headers = null, global::System.DateTime? deadline = null, global::System.Threading.CancellationToken cancellationToken = default(global::System.Threading.CancellationToken))
      {
        return Watch(request, new grpc::CallOptions(headers, deadline, cancellationToken));
      }
      /// <summary>
      /// Performs a watch for the serving status of the requested service.
      /// The server will immediately send back a message indicating the current
      /// serving status.  It will then subsequently send a new message whenever
      /// the service's serving status changes.
      ///
      /// If the requested service is unknown when the call is received, the
      /// server will send a message setting the serving status to
      /// SERVICE_UNKNOWN but will *not* terminate the call.  If at some
      /// future point, the serving status of the service becomes known, the
      /// server will send a new message with the service's serving status.
      ///
      /// If the call terminates with status UNIMPLEMENTED, then clients
      /// should assume this method is not supported and should not retry the
      /// call.  If the call terminates with any other status (including OK),
      /// clients should retry the call with appropriate exponential backoff.
      /// </summary>
      /// <param name="request">The request to send to the server.</param>
      /// <param name="options">The options for the call.</param>
      /// <returns>The call object.</returns>
      public virtual grpc::AsyncServerStreamingCall<global::Grpc.Health.V1.HealthCheckResponse> Watch(global::Grpc.Health.V1.HealthCheckRequest request, grpc::CallOptions options)
      {
        return CallInvoker.AsyncServerStreamingCall(__Method_Watch, null, options, request);
      }
      /// <summary>Creates a new instance of client from given <c>ClientBaseConfiguration</c>.</summary>
      protected override HealthClient NewInstance(ClientBaseConfiguration configuration)
      {
        return new HealthClient(configuration);
      }
    }

    /// <summary>Creates service definition that can be registered with a server</summary>
    /// <param name="serviceImpl">An object implementing the server-side handling logic.</param>
    public static grpc::ServerServiceDefinition BindService(HealthBase serviceImpl)
    {
      return grpc::ServerServiceDefinition.CreateBuilder()
          .AddMethod(__Method_Check, serviceImpl.Check)
          .AddMethod(__Method_Watch, serviceImpl.Watch).Build();
    }

    /// <summary>Register service method with a service binder with or without implementation. Useful when customizing the  service binding logic.
    /// Note: this method is part of an experimental API that can change or be removed without any prior notice.</summary>
    /// <param name="serviceBinder">Service methods will be bound by calling <c>AddMethod</c> on this object.</param>
    /// <param name="serviceImpl">An object implementing the server-side handling logic.</param>
    public static void BindService(grpc::ServiceBinderBase serviceBinder, HealthBase serviceImpl)
    {
      serviceBinder.AddMethod(__Method_Check, serviceImpl == null ? null : new grpc::UnaryServerMethod<global::Grpc.Health.V1.HealthCheckRequest, global::Grpc.Health.V1.HealthCheckResponse>(serviceImpl.Check));
      serviceBinder.AddMethod(__Method_Watch, serviceImpl == null ? null : new grpc::ServerStreamingServerMethod<global::Grpc.Health.V1.HealthCheckRequest, global::Grpc.Health.V1.HealthCheckResponse>(serviceImpl.Watch));
    }

  }
}
#endregion
