#region Copyright notice and license

// Copyright 2017 gRPC authors.
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

using System;

namespace Grpc.Core
{
    public interface IInterceptor
    {
        TResponse BlockingUnaryCall<TRequest, TResponse>(Method<TRequest, TResponse> method, string host, CallOptions options, TRequest request, Func<Method<TRequest, TResponse>, string, CallOptions, TRequest, TResponse> next);
        AsyncUnaryCall<TResponse> AsyncUnaryCall<TRequest, TResponse>(Method<TRequest, TResponse> method, string host, CallOptions options, TRequest request, Func<Method<TRequest, TResponse>, string, CallOptions, TRequest, AsyncUnaryCall<TResponse>> next);
        AsyncServerStreamingCall<TResponse> AsyncServerStreamingCall<TRequest, TResponse>(Method<TRequest, TResponse> method, string host, CallOptions options, TRequest request, Func<Method<TRequest, TResponse>, string, CallOptions, TRequest, AsyncServerStreamingCall<TResponse>> next);
        AsyncClientStreamingCall<TRequest, TResponse> AsyncClientStreamingCall<TRequest, TResponse>(Method<TRequest, TResponse> method, string host, CallOptions options, Func<Method<TRequest, TResponse>, string, CallOptions, AsyncClientStreamingCall<TRequest, TResponse>> next);
        AsyncDuplexStreamingCall<TRequest, TResponse> AsyncDuplexStreamingCall<TRequest, TResponse>(Method<TRequest, TResponse> method, string host, CallOptions options, Func<Method<TRequest, TResponse>, string, CallOptions, AsyncDuplexStreamingCall<TRequest, TResponse>> next);
    }
}
