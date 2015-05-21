#region Copyright notice and license

// Copyright 2015, Google Inc.
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
using System.Threading;
using System.Threading.Tasks;
using Google.ProtocolBuffers;
using Grpc.Core;
using Grpc.Core.Utils;

namespace grpc.testing
{
    /// <summary>
    /// Implementation of TestService server
    /// </summary>
    public class TestServiceImpl : TestService.ITestService
    {
        public Task<Empty> EmptyCall(ServerCallContext context, Empty request)
        {
            return Task.FromResult(Empty.DefaultInstance);
        }

        public Task<SimpleResponse> UnaryCall(ServerCallContext context, SimpleRequest request)
        {
            var response = SimpleResponse.CreateBuilder()
                .SetPayload(CreateZerosPayload(request.ResponseSize)).Build();
            return Task.FromResult(response);
        }

        public async Task StreamingOutputCall(ServerCallContext context, StreamingOutputCallRequest request, IServerStreamWriter<StreamingOutputCallResponse> responseStream)
        {
            foreach (var responseParam in request.ResponseParametersList)
            {
                var response = StreamingOutputCallResponse.CreateBuilder()
                    .SetPayload(CreateZerosPayload(responseParam.Size)).Build();
                await responseStream.WriteAsync(response);
            }
        }

        public async Task<StreamingInputCallResponse> StreamingInputCall(ServerCallContext context, IAsyncStreamReader<StreamingInputCallRequest> requestStream)
        {
            int sum = 0;
            await requestStream.ForEach(async request =>
            {
                sum += request.Payload.Body.Length;
            });
            return StreamingInputCallResponse.CreateBuilder().SetAggregatedPayloadSize(sum).Build();
        }

        public async Task FullDuplexCall(ServerCallContext context, IAsyncStreamReader<StreamingOutputCallRequest> requestStream, IServerStreamWriter<StreamingOutputCallResponse> responseStream)
        {
            await requestStream.ForEach(async request =>
            {
                foreach (var responseParam in request.ResponseParametersList)
                {
                    var response = StreamingOutputCallResponse.CreateBuilder()
                        .SetPayload(CreateZerosPayload(responseParam.Size)).Build();
                    await responseStream.WriteAsync(response);
                }
            });
        }

        public async Task HalfDuplexCall(ServerCallContext context, IAsyncStreamReader<StreamingOutputCallRequest> requestStream, IServerStreamWriter<StreamingOutputCallResponse> responseStream)
        {
            throw new NotImplementedException();
        }

        private static Payload CreateZerosPayload(int size)
        {
            return Payload.CreateBuilder().SetBody(ByteString.CopyFrom(new byte[size])).Build();
        }
    }
}
