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
using Google.Protobuf;
using Grpc.Core;
using Grpc.Core.Utils;
using Grpc.IntegrationTesting;

namespace Grpc.Testing
{
    /// <summary>
    /// Implementation of WorkerService server
    /// </summary>
    public class WorkerServiceImpl : WorkerService.WorkerServiceBase
    {
        readonly Action stopRequestHandler;

        public WorkerServiceImpl(Action stopRequestHandler)
        {
            this.stopRequestHandler = GrpcPreconditions.CheckNotNull(stopRequestHandler);
        }
        
        public override async Task RunServer(IAsyncStreamReader<ServerArgs> requestStream, IServerStreamWriter<ServerStatus> responseStream, ServerCallContext context)
        {
            GrpcPreconditions.CheckState(await requestStream.MoveNext());
            var serverConfig = requestStream.Current.Setup;
            var runner = ServerRunners.CreateStarted(serverConfig);

            await responseStream.WriteAsync(new ServerStatus
            {
                Stats = runner.GetStats(false),
                Port = runner.BoundPort,
                Cores = Environment.ProcessorCount,
            });
                
            while (await requestStream.MoveNext())
            {
                var reset = requestStream.Current.Mark.Reset;
                await responseStream.WriteAsync(new ServerStatus
                {
                    Stats = runner.GetStats(reset)
                });
            }
            await runner.StopAsync();
        }

        public override async Task RunClient(IAsyncStreamReader<ClientArgs> requestStream, IServerStreamWriter<ClientStatus> responseStream, ServerCallContext context)
        {
            GrpcPreconditions.CheckState(await requestStream.MoveNext());
            var clientConfig = requestStream.Current.Setup;
            var runner = ClientRunners.CreateStarted(clientConfig);

            await responseStream.WriteAsync(new ClientStatus
            {
                Stats = runner.GetStats(false)
            });

            while (await requestStream.MoveNext())
            {
                var reset = requestStream.Current.Mark.Reset;
                await responseStream.WriteAsync(new ClientStatus
                {
                    Stats = runner.GetStats(reset)
                });
            }
            await runner.StopAsync();
        }

        public override Task<CoreResponse> CoreCount(CoreRequest request, ServerCallContext context)
        {
            return Task.FromResult(new CoreResponse { Cores = Environment.ProcessorCount });
        }

        public override Task<Void> QuitWorker(Void request, ServerCallContext context)
        {
            stopRequestHandler();
            return Task.FromResult(new Void());
        }
    }
}
