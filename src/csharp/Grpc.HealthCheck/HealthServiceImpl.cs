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
using System.Linq;
using System.Text;
using System.Threading.Tasks;

using Grpc.Core;
using Grpc.Core.Utils;
using Grpc.Health.V1;

namespace Grpc.HealthCheck
{
    /// <summary>
    /// Implementation of a simple Health service. Useful for health checking.
    /// 
    /// Registering service with a server:
    /// <code>
    /// var serviceImpl = new HealthServiceImpl();
    /// server = new Server();
    /// server.AddServiceDefinition(Grpc.Health.V1.Health.BindService(serviceImpl));
    /// </code>
    /// </summary>
    public class HealthServiceImpl : Grpc.Health.V1.Health.HealthBase
    {
        private readonly object myLock = new object();
        private readonly Dictionary<string, HealthCheckResponse.Types.ServingStatus> statusMap = 
            new Dictionary<string, HealthCheckResponse.Types.ServingStatus>();

        /// <summary>
        /// Sets the health status for given service.
        /// </summary>
        /// <param name="service">The service. Cannot be null.</param>
        /// <param name="status">the health status</param>
        public void SetStatus(string service, HealthCheckResponse.Types.ServingStatus status)
        {
            lock (myLock)
            {
                statusMap[service] = status;
            }
        }

        /// <summary>
        /// Clears health status for given service.
        /// </summary>
        /// <param name="service">The service. Cannot be null.</param>
        public void ClearStatus(string service)
        {
            lock (myLock)
            {
                statusMap.Remove(service);
            }
        }
        
        /// <summary>
        /// Clears statuses for all services.
        /// </summary>
        public void ClearAll()
        {
            lock (myLock)
            {
                statusMap.Clear();
            }
        }

        /// <summary>
        /// Performs a health status check.
        /// </summary>
        /// <param name="request">The check request.</param>
        /// <param name="context">The call context.</param>
        /// <returns>The asynchronous response.</returns>
        public override Task<HealthCheckResponse> Check(HealthCheckRequest request, ServerCallContext context)
        {
            lock (myLock)
            {
                var service = request.Service;

                HealthCheckResponse.Types.ServingStatus status;
                if (!statusMap.TryGetValue(service, out status))
                {
                    // TODO(jtattermusch): returning specific status from server handler is not supported yet.
                    throw new RpcException(new Status(StatusCode.NotFound, ""));
                }
                return Task.FromResult(new HealthCheckResponse { Status = status });
            }
        }
    }
}
