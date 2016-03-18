#region Copyright notice and license

// Copyright 2015-2016, Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

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
