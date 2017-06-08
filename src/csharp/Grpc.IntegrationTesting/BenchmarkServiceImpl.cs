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
using System.Threading;
using System.Threading.Tasks;
using Google.Protobuf;
using Grpc.Core;
using Grpc.Core.Utils;

namespace Grpc.Testing
{
    /// <summary>
    /// Implementation of BenchmarkService server
    /// </summary>
    public class BenchmarkServiceImpl : BenchmarkService.BenchmarkServiceBase
    {
        public BenchmarkServiceImpl()
        {
        }

        public override Task<SimpleResponse> UnaryCall(SimpleRequest request, ServerCallContext context)
        {
            var response = new SimpleResponse { Payload = CreateZerosPayload(request.ResponseSize) };
            return Task.FromResult(response);
        }

        public override async Task StreamingCall(IAsyncStreamReader<SimpleRequest> requestStream, IServerStreamWriter<SimpleResponse> responseStream, ServerCallContext context)
        {
            await requestStream.ForEachAsync(async request =>
            {
                var response = new SimpleResponse { Payload = CreateZerosPayload(request.ResponseSize) };
                await responseStream.WriteAsync(response);
            });
        }

        private static Payload CreateZerosPayload(int size)
        {
            return new Payload { Body = ByteString.CopyFrom(new byte[size]) };
        }
    }
}
