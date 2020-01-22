// <auto-generated>
//     Generated by the protocol buffer compiler.  DO NOT EDIT!
//     source: src/proto/grpc/testing/empty_service.proto
// </auto-generated>
// Original file comments:
// Copyright 2018 gRPC authors.
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
#pragma warning disable 0414, 1591
#region Designer generated code

using grpc = global::Grpc.Core;

namespace Grpc.Testing {
  /// <summary>
  /// A service that has zero methods.
  /// See https://github.com/grpc/grpc/issues/15574
  /// </summary>
  public static partial class EmptyService
  {
    static readonly string __ServiceName = "grpc.testing.EmptyService";

    /// <summary>Service descriptor</summary>
    public static global::Google.Protobuf.Reflection.ServiceDescriptor Descriptor
    {
      get { return global::Grpc.Testing.EmptyServiceReflection.Descriptor.Services[0]; }
    }

    /// <summary>Base class for server-side implementations of EmptyService</summary>
    [grpc::BindServiceMethod(typeof(EmptyService), "BindService")]
    public abstract partial class EmptyServiceBase
    {
    }

    /// <summary>Client for EmptyService</summary>
    public partial class EmptyServiceClient : grpc::ClientBase<EmptyServiceClient>
    {
      /// <summary>Creates a new client for EmptyService</summary>
      /// <param name="channel">The channel to use to make remote calls.</param>
      public EmptyServiceClient(grpc::ChannelBase channel) : base(channel)
      {
      }
      /// <summary>Creates a new client for EmptyService that uses a custom <c>CallInvoker</c>.</summary>
      /// <param name="callInvoker">The callInvoker to use to make remote calls.</param>
      public EmptyServiceClient(grpc::CallInvoker callInvoker) : base(callInvoker)
      {
      }
      /// <summary>Protected parameterless constructor to allow creation of test doubles.</summary>
      protected EmptyServiceClient() : base()
      {
      }
      /// <summary>Protected constructor to allow creation of configured clients.</summary>
      /// <param name="configuration">The client configuration.</param>
      protected EmptyServiceClient(ClientBaseConfiguration configuration) : base(configuration)
      {
      }

      /// <summary>Creates a new instance of client from given <c>ClientBaseConfiguration</c>.</summary>
      protected override EmptyServiceClient NewInstance(ClientBaseConfiguration configuration)
      {
        return new EmptyServiceClient(configuration);
      }
    }

    /// <summary>Creates service definition that can be registered with a server</summary>
    /// <param name="serviceImpl">An object implementing the server-side handling logic.</param>
    public static grpc::ServerServiceDefinition BindService(EmptyServiceBase serviceImpl)
    {
      return grpc::ServerServiceDefinition.CreateBuilder().Build();
    }

    /// <summary>Register service method with a service binder with or without implementation. Useful when customizing the  service binding logic.
    /// Note: this method is part of an experimental API that can change or be removed without any prior notice.</summary>
    /// <param name="serviceBinder">Service methods will be bound by calling <c>AddMethod</c> on this object.</param>
    /// <param name="serviceImpl">An object implementing the server-side handling logic.</param>
    public static void BindService(grpc::ServiceBinderBase serviceBinder, EmptyServiceBase serviceImpl)
    {
    }

  }
}
#endregion
