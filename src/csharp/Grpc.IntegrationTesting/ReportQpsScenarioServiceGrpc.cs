// <auto-generated>
//     Generated by the protocol buffer compiler.  DO NOT EDIT!
//     source: src/proto/grpc/testing/report_qps_scenario_service.proto
// </auto-generated>
// Original file comments:
// Copyright 2015 gRPC authors.
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
// An integration test service that covers all the method signature permutations
// of unary/streaming requests/responses.
#pragma warning disable 0414, 1591
#region Designer generated code

using grpc = global::Grpc.Core;

namespace Grpc.Testing {
  public static partial class ReportQpsScenarioService
  {
    static readonly string __ServiceName = "grpc.testing.ReportQpsScenarioService";

    static readonly grpc::Marshaller<global::Grpc.Testing.ScenarioResult> __Marshaller_grpc_testing_ScenarioResult = grpc::Marshallers.Create((arg) => global::Google.Protobuf.MessageExtensions.ToByteArray(arg), global::Grpc.Testing.ScenarioResult.Parser.ParseFrom);
    static readonly grpc::Marshaller<global::Grpc.Testing.Void> __Marshaller_grpc_testing_Void = grpc::Marshallers.Create((arg) => global::Google.Protobuf.MessageExtensions.ToByteArray(arg), global::Grpc.Testing.Void.Parser.ParseFrom);

    static readonly grpc::Method<global::Grpc.Testing.ScenarioResult, global::Grpc.Testing.Void> __Method_ReportScenario = new grpc::Method<global::Grpc.Testing.ScenarioResult, global::Grpc.Testing.Void>(
        grpc::MethodType.Unary,
        __ServiceName,
        "ReportScenario",
        __Marshaller_grpc_testing_ScenarioResult,
        __Marshaller_grpc_testing_Void);

    /// <summary>Service descriptor</summary>
    public static global::Google.Protobuf.Reflection.ServiceDescriptor Descriptor
    {
      get { return global::Grpc.Testing.ReportQpsScenarioServiceReflection.Descriptor.Services[0]; }
    }

    /// <summary>Base class for server-side implementations of ReportQpsScenarioService</summary>
    public abstract partial class ReportQpsScenarioServiceBase
    {
      /// <summary>
      /// Report results of a QPS test benchmark scenario.
      /// </summary>
      /// <param name="request">The request received from the client.</param>
      /// <param name="context">The context of the server-side call handler being invoked.</param>
      /// <returns>The response to send back to the client (wrapped by a task).</returns>
      public virtual global::System.Threading.Tasks.Task<global::Grpc.Testing.Void> ReportScenario(global::Grpc.Testing.ScenarioResult request, grpc::ServerCallContext context)
      {
        throw new grpc::RpcException(new grpc::Status(grpc::StatusCode.Unimplemented, ""));
      }

    }

    /// <summary>Client for ReportQpsScenarioService</summary>
    public partial class ReportQpsScenarioServiceClient : grpc::ClientBase<ReportQpsScenarioServiceClient>
    {
      /// <summary>Creates a new client for ReportQpsScenarioService</summary>
      /// <param name="channel">The channel to use to make remote calls.</param>
      public ReportQpsScenarioServiceClient(grpc::Channel channel) : base(channel)
      {
      }
      /// <summary>Creates a new client for ReportQpsScenarioService that uses a custom <c>CallInvoker</c>.</summary>
      /// <param name="callInvoker">The callInvoker to use to make remote calls.</param>
      public ReportQpsScenarioServiceClient(grpc::CallInvoker callInvoker) : base(callInvoker)
      {
      }
      /// <summary>Protected parameterless constructor to allow creation of test doubles.</summary>
      protected ReportQpsScenarioServiceClient() : base()
      {
      }
      /// <summary>Protected constructor to allow creation of configured clients.</summary>
      /// <param name="configuration">The client configuration.</param>
      protected ReportQpsScenarioServiceClient(ClientBaseConfiguration configuration) : base(configuration)
      {
      }

      /// <summary>
      /// Report results of a QPS test benchmark scenario.
      /// </summary>
      /// <param name="request">The request to send to the server.</param>
      /// <param name="headers">The initial metadata to send with the call. This parameter is optional.</param>
      /// <param name="deadline">An optional deadline for the call. The call will be cancelled if deadline is hit.</param>
      /// <param name="cancellationToken">An optional token for canceling the call.</param>
      /// <returns>The response received from the server.</returns>
      public virtual global::Grpc.Testing.Void ReportScenario(global::Grpc.Testing.ScenarioResult request, grpc::Metadata headers = null, global::System.DateTime? deadline = null, global::System.Threading.CancellationToken cancellationToken = default(global::System.Threading.CancellationToken))
      {
        return ReportScenario(request, new grpc::CallOptions(headers, deadline, cancellationToken));
      }
      /// <summary>
      /// Report results of a QPS test benchmark scenario.
      /// </summary>
      /// <param name="request">The request to send to the server.</param>
      /// <param name="options">The options for the call.</param>
      /// <returns>The response received from the server.</returns>
      public virtual global::Grpc.Testing.Void ReportScenario(global::Grpc.Testing.ScenarioResult request, grpc::CallOptions options)
      {
        return CallInvoker.BlockingUnaryCall(__Method_ReportScenario, null, options, request);
      }
      /// <summary>
      /// Report results of a QPS test benchmark scenario.
      /// </summary>
      /// <param name="request">The request to send to the server.</param>
      /// <param name="headers">The initial metadata to send with the call. This parameter is optional.</param>
      /// <param name="deadline">An optional deadline for the call. The call will be cancelled if deadline is hit.</param>
      /// <param name="cancellationToken">An optional token for canceling the call.</param>
      /// <returns>The call object.</returns>
      public virtual grpc::AsyncUnaryCall<global::Grpc.Testing.Void> ReportScenarioAsync(global::Grpc.Testing.ScenarioResult request, grpc::Metadata headers = null, global::System.DateTime? deadline = null, global::System.Threading.CancellationToken cancellationToken = default(global::System.Threading.CancellationToken))
      {
        return ReportScenarioAsync(request, new grpc::CallOptions(headers, deadline, cancellationToken));
      }
      /// <summary>
      /// Report results of a QPS test benchmark scenario.
      /// </summary>
      /// <param name="request">The request to send to the server.</param>
      /// <param name="options">The options for the call.</param>
      /// <returns>The call object.</returns>
      public virtual grpc::AsyncUnaryCall<global::Grpc.Testing.Void> ReportScenarioAsync(global::Grpc.Testing.ScenarioResult request, grpc::CallOptions options)
      {
        return CallInvoker.AsyncUnaryCall(__Method_ReportScenario, null, options, request);
      }
      /// <summary>Creates a new instance of client from given <c>ClientBaseConfiguration</c>.</summary>
      protected override ReportQpsScenarioServiceClient NewInstance(ClientBaseConfiguration configuration)
      {
        return new ReportQpsScenarioServiceClient(configuration);
      }
    }

    /// <summary>Creates service definition that can be registered with a server</summary>
    /// <param name="serviceImpl">An object implementing the server-side handling logic.</param>
    public static grpc::ServerServiceDefinition BindService(ReportQpsScenarioServiceBase serviceImpl)
    {
      return grpc::ServerServiceDefinition.CreateBuilder()
          .AddMethod(__Method_ReportScenario, serviceImpl.ReportScenario).Build();
    }

  }
}
#endregion
