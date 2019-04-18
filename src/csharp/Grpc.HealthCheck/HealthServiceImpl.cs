#region Copyright notice and license
// Copyright 2015 gRPC authors.
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
