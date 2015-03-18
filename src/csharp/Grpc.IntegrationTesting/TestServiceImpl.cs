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
using Grpc.Core.Utils;

namespace grpc.testing
{
    /// <summary>
    /// Implementation of TestService server
    /// </summary>
    public class TestServiceImpl : TestServiceGrpc.ITestService
    {
        public void EmptyCall(Empty request, IObserver<Empty> responseObserver)
        {
            responseObserver.OnNext(Empty.DefaultInstance);
            responseObserver.OnCompleted();
        }

        public void UnaryCall(SimpleRequest request, IObserver<SimpleResponse> responseObserver)
        {
            var response = SimpleResponse.CreateBuilder()
                .SetPayload(CreateZerosPayload(request.ResponseSize)).Build();
            // TODO: check we support ReponseType
            responseObserver.OnNext(response);
            responseObserver.OnCompleted();
        }

        public void StreamingOutputCall(StreamingOutputCallRequest request, IObserver<StreamingOutputCallResponse> responseObserver)
        {
            foreach (var responseParam in request.ResponseParametersList)
            {
                var response = StreamingOutputCallResponse.CreateBuilder()
                    .SetPayload(CreateZerosPayload(responseParam.Size)).Build();
                responseObserver.OnNext(response);
            }
            responseObserver.OnCompleted();
        }

        public IObserver<StreamingInputCallRequest> StreamingInputCall(IObserver<StreamingInputCallResponse> responseObserver)
        {
            var recorder = new RecordingObserver<StreamingInputCallRequest>();
            Task.Run(() =>
            {
                int sum = 0;
                foreach (var req in recorder.ToList().Result)
                {
                    sum += req.Payload.Body.Length;
                }
                var response = StreamingInputCallResponse.CreateBuilder()
                    .SetAggregatedPayloadSize(sum).Build();
                responseObserver.OnNext(response);
                responseObserver.OnCompleted();
            });
            return recorder;
        }

        public IObserver<StreamingOutputCallRequest> FullDuplexCall(IObserver<StreamingOutputCallResponse> responseObserver)
        {
            return new FullDuplexObserver(responseObserver);
        }

        public IObserver<StreamingOutputCallRequest> HalfDuplexCall(IObserver<StreamingOutputCallResponse> responseObserver)
        {
            throw new NotImplementedException();
        }

        private class FullDuplexObserver : IObserver<StreamingOutputCallRequest>
        {
            readonly IObserver<StreamingOutputCallResponse> responseObserver;

            public FullDuplexObserver(IObserver<StreamingOutputCallResponse> responseObserver)
            {
                this.responseObserver = responseObserver;
            }

            public void OnCompleted()
            {
                responseObserver.OnCompleted();
            }

            public void OnError(Exception error)
            {
                throw new NotImplementedException();
            }

            public void OnNext(StreamingOutputCallRequest value)
            {
                foreach (var responseParam in value.ResponseParametersList)
                {
                    var response = StreamingOutputCallResponse.CreateBuilder()
                        .SetPayload(CreateZerosPayload(responseParam.Size)).Build();
                    responseObserver.OnNext(response);
                }
            }
        }

        private static Payload CreateZerosPayload(int size)
        {
            return Payload.CreateBuilder().SetBody(ByteString.CopyFrom(new byte[size])).Build();
        }
    }
}
