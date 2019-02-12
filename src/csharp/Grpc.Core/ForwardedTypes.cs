#region Copyright notice and license

// Copyright 2019 The gRPC Authors
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

#endregion

using System.Runtime.CompilerServices;
using Grpc.Core;
using Grpc.Core.Logging;
using Grpc.Core.Utils;

// API types that used to be in Grpc.Core package, but were moved to Grpc.Core.Api
// https://docs.microsoft.com/en-us/dotnet/framework/app-domains/type-forwarding-in-the-common-language-runtime

// TODO(jtattermusch): move types needed for implementing a client

[assembly:TypeForwardedToAttribute(typeof(GrpcPreconditions))]
[assembly:TypeForwardedToAttribute(typeof(AuthContext))]
[assembly:TypeForwardedToAttribute(typeof(ContextPropagationOptions))]
[assembly:TypeForwardedToAttribute(typeof(ContextPropagationToken))]
[assembly:TypeForwardedToAttribute(typeof(DeserializationContext))]
[assembly:TypeForwardedToAttribute(typeof(IAsyncStreamReader<>))]
[assembly:TypeForwardedToAttribute(typeof(IAsyncStreamWriter<>))]
[assembly:TypeForwardedToAttribute(typeof(IServerStreamWriter<>))]
[assembly:TypeForwardedToAttribute(typeof(Marshaller<>))]
[assembly:TypeForwardedToAttribute(typeof(Marshallers))]
[assembly:TypeForwardedToAttribute(typeof(Metadata))]
[assembly:TypeForwardedToAttribute(typeof(MethodType))]
[assembly:TypeForwardedToAttribute(typeof(IMethod))]
[assembly:TypeForwardedToAttribute(typeof(Method<,>))]
[assembly:TypeForwardedToAttribute(typeof(RpcException))]
[assembly:TypeForwardedToAttribute(typeof(SerializationContext))]
[assembly:TypeForwardedToAttribute(typeof(ServerCallContext))]
[assembly:TypeForwardedToAttribute(typeof(UnaryServerMethod<,>))]
[assembly:TypeForwardedToAttribute(typeof(ClientStreamingServerMethod<,>))]
[assembly:TypeForwardedToAttribute(typeof(ServerStreamingServerMethod<,>))]
[assembly:TypeForwardedToAttribute(typeof(DuplexStreamingServerMethod<,>))]
[assembly:TypeForwardedToAttribute(typeof(ServerServiceDefinition))]
[assembly:TypeForwardedToAttribute(typeof(ServiceBinderBase))]
[assembly:TypeForwardedToAttribute(typeof(Status))]
[assembly:TypeForwardedToAttribute(typeof(StatusCode))]
[assembly:TypeForwardedToAttribute(typeof(WriteOptions))]
[assembly:TypeForwardedToAttribute(typeof(WriteFlags))]
