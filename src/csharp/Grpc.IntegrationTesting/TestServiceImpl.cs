#region Copyright notice and license

// Copyright 2015-2016 gRPC authors.
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
using System.Threading;
using System.Threading.Tasks;
using Google.Protobuf;
using Grpc.Core;
using Grpc.Core.Utils;

namespace Grpc.Testing
{
    /// <summary>
    /// Implementation of TestService server
    /// </summary>
    public class TestServiceImpl : TestService.TestServiceBase
    {
        public override Task<Empty> EmptyCall(Empty request, ServerCallContext context)
        {
            return Task.FromResult(new Empty());
        }

        public override async Task<SimpleResponse> UnaryCall(SimpleRequest request, ServerCallContext context)
        {
            await EnsureEchoMetadataAsync(context);
            EnsureEchoStatus(request.ResponseStatus, context);

            var response = new SimpleResponse { Payload = CreateZerosPayload(request.ResponseSize) };
            return response;
        }

        public override async Task StreamingOutputCall(StreamingOutputCallRequest request, IServerStreamWriter<StreamingOutputCallResponse> responseStream, ServerCallContext context)
        {
            await EnsureEchoMetadataAsync(context);
            EnsureEchoStatus(request.ResponseStatus, context);

            foreach (var responseParam in request.ResponseParameters)
            {
                var response = new StreamingOutputCallResponse { Payload = CreateZerosPayload(responseParam.Size) };
                await responseStream.WriteAsync(response);
            }
        }

        public override async Task<StreamingInputCallResponse> StreamingInputCall(IAsyncStreamReader<StreamingInputCallRequest> requestStream, ServerCallContext context)
        {
            await EnsureEchoMetadataAsync(context);

            int sum = 0;
            await requestStream.ForEachAsync(async request =>
            {
                sum += request.Payload.Body.Length;
            });
            return new StreamingInputCallResponse { AggregatedPayloadSize = sum };
        }

        public override async Task FullDuplexCall(IAsyncStreamReader<StreamingOutputCallRequest> requestStream, IServerStreamWriter<StreamingOutputCallResponse> responseStream, ServerCallContext context)
        {
            await EnsureEchoMetadataAsync(context);

            await requestStream.ForEachAsync(async request =>
            {
                EnsureEchoStatus(request.ResponseStatus, context);
                foreach (var responseParam in request.ResponseParameters)
                {
                    var response = new StreamingOutputCallResponse { Payload = CreateZerosPayload(responseParam.Size) };
                    await responseStream.WriteAsync(response);
                }
            });
        }

        public override async Task HalfDuplexCall(IAsyncStreamReader<StreamingOutputCallRequest> requestStream, IServerStreamWriter<StreamingOutputCallResponse> responseStream, ServerCallContext context)
        {
            throw new NotImplementedException();
        }

        private static Payload CreateZerosPayload(int size)
        {
            return new Payload { Body = ByteString.CopyFrom(new byte[size]) };
        }

        private static async Task EnsureEchoMetadataAsync(ServerCallContext context)
        {
            var echoInitialList = context.RequestHeaders.Where((entry) => entry.Key == "x-grpc-test-echo-initial").ToList();
            if (echoInitialList.Any()) {
                var entry = echoInitialList.Single();
                await context.WriteResponseHeadersAsync(new Metadata { entry });
            }

            var echoTrailingList = context.RequestHeaders.Where((entry) => entry.Key == "x-grpc-test-echo-trailing-bin").ToList();
            if (echoTrailingList.Any()) {
                context.ResponseTrailers.Add(echoTrailingList.Single());
            }
        }

        private static void EnsureEchoStatus(EchoStatus responseStatus, ServerCallContext context)
        {
            if (responseStatus != null)
            {
                var statusCode = (StatusCode)responseStatus.Code;
                context.Status = new Status(statusCode, responseStatus.Message);
            }
        }
    }
}
