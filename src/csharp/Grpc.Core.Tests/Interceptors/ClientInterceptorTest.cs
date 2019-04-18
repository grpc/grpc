#region Copyright notice and license

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

#endregion

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using Grpc.Core;
using Grpc.Core.Interceptors;
using Grpc.Core.Internal;
using Grpc.Core.Utils;
using Grpc.Core.Tests;
using NUnit.Framework;

namespace Grpc.Core.Interceptors.Tests
{
    public class ClientInterceptorTest
    {
        const string Host = "127.0.0.1";

        [Test]
        public void AddRequestHeaderInClientInterceptor()
        {
            const string HeaderKey = "x-client-interceptor";
            const string HeaderValue = "hello-world";
            var helper = new MockServiceHelper(Host);
            helper.UnaryHandler = new UnaryServerMethod<string, string>((request, context) =>
            {
                var interceptorHeader = context.RequestHeaders.Last(m => (m.Key == HeaderKey)).Value;
                Assert.AreEqual(interceptorHeader, HeaderValue);
                return Task.FromResult("PASS");
            });
            var server = helper.GetServer();
            server.Start();
            var callInvoker = helper.GetChannel().Intercept(metadata =>
            {
                metadata = metadata ?? new Metadata();
                metadata.Add(new Metadata.Entry(HeaderKey, HeaderValue));
                return metadata;
            });
            Assert.AreEqual("PASS", callInvoker.BlockingUnaryCall(new Method<string, string>(MethodType.Unary, MockServiceHelper.ServiceName, "Unary", Marshallers.StringMarshaller, Marshallers.StringMarshaller), Host, new CallOptions(), ""));
        }

        [Test]
        public void CheckInterceptorOrderInClientInterceptors()
        {
            var helper = new MockServiceHelper(Host);
            helper.UnaryHandler = new UnaryServerMethod<string, string>((request, context) =>
            {
                return Task.FromResult("PASS");
            });
            var server = helper.GetServer();
            server.Start();
            var stringBuilder = new StringBuilder();
            var callInvoker = helper.GetChannel().Intercept(metadata => {
                stringBuilder.Append("interceptor1");
                return metadata;
            }).Intercept(new CallbackInterceptor(() => stringBuilder.Append("array1")),
                new CallbackInterceptor(() => stringBuilder.Append("array2")),
                new CallbackInterceptor(() => stringBuilder.Append("array3")))
            .Intercept(metadata =>
            {
                stringBuilder.Append("interceptor2");
                return metadata;
            }).Intercept(metadata =>
            {
                stringBuilder.Append("interceptor3");
                return metadata;
            });
            Assert.AreEqual("PASS", callInvoker.BlockingUnaryCall(new Method<string, string>(MethodType.Unary, MockServiceHelper.ServiceName, "Unary", Marshallers.StringMarshaller, Marshallers.StringMarshaller), Host, new CallOptions(), ""));
            Assert.AreEqual("interceptor3interceptor2array1array2array3interceptor1", stringBuilder.ToString());
        }

        [Test]
        public void CheckNullInterceptorRegistrationFails()
        {
            var helper = new MockServiceHelper(Host);
            helper.UnaryHandler = new UnaryServerMethod<string, string>((request, context) =>
            {
                return Task.FromResult("PASS");
            });
            Assert.Throws<ArgumentNullException>(() => helper.GetChannel().Intercept(default(Interceptor)));
            Assert.Throws<ArgumentNullException>(() => helper.GetChannel().Intercept(new[]{default(Interceptor)}));
            Assert.Throws<ArgumentNullException>(() => helper.GetChannel().Intercept(new[]{new CallbackInterceptor(()=>{}), null}));
            Assert.Throws<ArgumentNullException>(() => helper.GetChannel().Intercept(default(Interceptor[])));
        }

        [Test]
        public async Task CountNumberOfRequestsInClientInterceptors()
        {
            var helper = new MockServiceHelper(Host);
            helper.ClientStreamingHandler = new ClientStreamingServerMethod<string, string>(async (requestStream, context) =>
            {
                var stringBuilder = new StringBuilder();
                await requestStream.ForEachAsync(request =>
                {
                    stringBuilder.Append(request);
                    return TaskUtils.CompletedTask;
                });
                await Task.Delay(100);
                return stringBuilder.ToString();
            });

            var callInvoker = helper.GetChannel().Intercept(new ClientStreamingCountingInterceptor());

            var server = helper.GetServer();
            server.Start();
            var call = callInvoker.AsyncClientStreamingCall(new Method<string, string>(MethodType.ClientStreaming, MockServiceHelper.ServiceName, "ClientStreaming", Marshallers.StringMarshaller, Marshallers.StringMarshaller), Host, new CallOptions());
            await call.RequestStream.WriteAllAsync(new string[] { "A", "B", "C" });
            Assert.AreEqual("3", await call.ResponseAsync);

            Assert.AreEqual(StatusCode.OK, call.GetStatus().StatusCode);
            Assert.IsNotNull(call.GetTrailers());
        }

        private class CallbackInterceptor : Interceptor
        {
            readonly Action callback;

            public CallbackInterceptor(Action callback)
            {
                this.callback = GrpcPreconditions.CheckNotNull(callback, nameof(callback));
            }

            public override TResponse BlockingUnaryCall<TRequest, TResponse>(TRequest request, ClientInterceptorContext<TRequest, TResponse> context, BlockingUnaryCallContinuation<TRequest, TResponse> continuation)
            {
                callback();
                return continuation(request, context);
            }

            public override AsyncUnaryCall<TResponse> AsyncUnaryCall<TRequest, TResponse>(TRequest request, ClientInterceptorContext<TRequest, TResponse> context, AsyncUnaryCallContinuation<TRequest, TResponse> continuation)
            {
                callback();
                return continuation(request, context);
            }

            public override AsyncServerStreamingCall<TResponse> AsyncServerStreamingCall<TRequest, TResponse>(TRequest request, ClientInterceptorContext<TRequest, TResponse> context, AsyncServerStreamingCallContinuation<TRequest, TResponse> continuation)
            {
                callback();
                return continuation(request, context);
            }

            public override AsyncClientStreamingCall<TRequest, TResponse> AsyncClientStreamingCall<TRequest, TResponse>(ClientInterceptorContext<TRequest, TResponse> context, AsyncClientStreamingCallContinuation<TRequest, TResponse> continuation)
            {
                callback();
                return continuation(context);
            }

            public override AsyncDuplexStreamingCall<TRequest, TResponse> AsyncDuplexStreamingCall<TRequest, TResponse>(ClientInterceptorContext<TRequest, TResponse> context, AsyncDuplexStreamingCallContinuation<TRequest, TResponse> continuation)
            {
                callback();
                return continuation(context);
            }
        }

        private class ClientStreamingCountingInterceptor : Interceptor
        {
            public override AsyncClientStreamingCall<TRequest, TResponse> AsyncClientStreamingCall<TRequest, TResponse>(ClientInterceptorContext<TRequest, TResponse> context, AsyncClientStreamingCallContinuation<TRequest, TResponse> continuation)
            {
                var response = continuation(context);
                int counter = 0;
                var requestStream = new WrappedClientStreamWriter<TRequest>(response.RequestStream,
                    message => { counter++; return message; }, null);
                var responseAsync = response.ResponseAsync.ContinueWith(
                    unaryResponse => (TResponse)(object)counter.ToString()  // Cast to object first is needed to satisfy the type-checker    
                );
                return new AsyncClientStreamingCall<TRequest, TResponse>(requestStream, responseAsync, response.ResponseHeadersAsync, response.GetStatus, response.GetTrailers, response.Dispose);
            }
        }

        private class WrappedClientStreamWriter<T> : IClientStreamWriter<T>
        {
            readonly IClientStreamWriter<T> writer;
            readonly Func<T, T> onMessage;
            readonly Action onResponseStreamEnd;
            public WrappedClientStreamWriter(IClientStreamWriter<T> writer, Func<T, T> onMessage, Action onResponseStreamEnd)
            {
                this.writer = writer;
                this.onMessage = onMessage;
                this.onResponseStreamEnd = onResponseStreamEnd;
            }
            public Task CompleteAsync()
            {
                if (onResponseStreamEnd != null)
                {
                    return writer.CompleteAsync().ContinueWith(x => onResponseStreamEnd());
                }
                return writer.CompleteAsync();
            }
            public Task WriteAsync(T message)
            {
                if (onMessage != null)
                {
                    message = onMessage(message);
                }
                return writer.WriteAsync(message);
            }
            public WriteOptions WriteOptions
            {
                get
                {
                    return writer.WriteOptions;
                }
                set
                {
                    writer.WriteOptions = value;
                }
            }
        }
    }
}
