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
using System.Collections.Concurrent;
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
        private readonly Dictionary<string, List<IServerStreamWriter<HealthCheckResponse>>> watchers =
            new Dictionary<string, List<IServerStreamWriter<HealthCheckResponse>>>();

        /// <summary>
        /// Sets the health status for given service.
        /// </summary>
        /// <param name="service">The service. Cannot be null.</param>
        /// <param name="status">the health status</param>
        public void SetStatus(string service, HealthCheckResponse.Types.ServingStatus status)
        {
            lock (myLock)
            {
                HealthCheckResponse.Types.ServingStatus previousStatus = GetServiceStatus(service);
                statusMap[service] = status;

                if (status != previousStatus)
                {
                    NotifyStatus(service, status);
                }
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
                HealthCheckResponse.Types.ServingStatus previousStatus = GetServiceStatus(service);
                statusMap.Remove(service);

                if (previousStatus != HealthCheckResponse.Types.ServingStatus.ServiceUnknown)
                {
                    NotifyStatus(service, HealthCheckResponse.Types.ServingStatus.ServiceUnknown);
                }
            }
        }

        /// <summary>
        /// Clears statuses for all services.
        /// </summary>
        public void ClearAll()
        {
            lock (myLock)
            {
                List<KeyValuePair<string, HealthCheckResponse.Types.ServingStatus>> statuses = statusMap.ToList();

                statusMap.Clear();

                foreach (KeyValuePair<string, HealthCheckResponse.Types.ServingStatus> status in statuses)
                {
                    if (status.Value != HealthCheckResponse.Types.ServingStatus.Unknown)
                    {
                        NotifyStatus(status.Key, HealthCheckResponse.Types.ServingStatus.Unknown);
                    }
                }
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
            HealthCheckResponse response = GetHealthCheckResponse(request.Service, throwOnNotFound: true);

            return Task.FromResult(response);
        }

        /// <summary>
        /// Performs a watch for the serving status of the requested service.
        /// The server will immediately send back a message indicating the current
        /// serving status.  It will then subsequently send a new message whenever
        /// the service's serving status changes.
        ///
        /// If the requested service is unknown when the call is received, the
        /// server will send a message setting the serving status to
        /// SERVICE_UNKNOWN but will *not* terminate the call.  If at some
        /// future point, the serving status of the service becomes known, the
        /// server will send a new message with the service's serving status.
        ///
        /// If the call terminates with status UNIMPLEMENTED, then clients
        /// should assume this method is not supported and should not retry the
        /// call.  If the call terminates with any other status (including OK),
        /// clients should retry the call with appropriate exponential backoff.
        /// </summary>
        /// <param name="request">The request received from the client.</param>
        /// <param name="responseStream">Used for sending responses back to the client.</param>
        /// <param name="context">The context of the server-side call handler being invoked.</param>
        /// <returns>A task indicating completion of the handler.</returns>
        public override async Task Watch(HealthCheckRequest request, IServerStreamWriter<HealthCheckResponse> responseStream, ServerCallContext context)
        {
            string service = request.Service;
            TaskCompletionSource<object> watchTcs = new TaskCompletionSource<object>();

            HealthCheckResponse response = GetHealthCheckResponse(service, throwOnNotFound: false);
            await responseStream.WriteAsync(response);

            lock (myLock)
            {
                if (!watchers.TryGetValue(service, out List<IServerStreamWriter<HealthCheckResponse>> serverStreamWriters))
                {
                    serverStreamWriters = new List<IServerStreamWriter<HealthCheckResponse>>();
                    watchers.Add(service, serverStreamWriters);
                }

                serverStreamWriters.Add(responseStream);
            }

            // Handle the Watch call being canceled
            context.CancellationToken.Register(() => {
                lock (myLock)
                {
                    if (watchers.TryGetValue(service, out List<IServerStreamWriter<HealthCheckResponse>> serverStreamWriters))
                    {
                        // Remove the response stream from the watchers
                        if (serverStreamWriters.Remove(responseStream))
                        {
                            // Remove empty collection if service has no more response streams
                            if (serverStreamWriters.Count == 0)
                            {
                                watchers.Remove(service);
                            }
                        }
                    }
                }

                // Allow watch method to exit.
                watchTcs.TrySetResult(null);
            });

            // Wait for call to be cancelled before exiting.
            await watchTcs.Task;
        }

        private HealthCheckResponse GetHealthCheckResponse(string service, bool throwOnNotFound)
        {
            HealthCheckResponse response = null;
            lock (myLock)
            {
                HealthCheckResponse.Types.ServingStatus status;
                if (!statusMap.TryGetValue(service, out status))
                {
                    if (throwOnNotFound)
                    {
                        // TODO(jtattermusch): returning specific status from server handler is not supported yet.
                        throw new RpcException(new Status(StatusCode.NotFound, ""));
                    }
                    else
                    {
                        status = HealthCheckResponse.Types.ServingStatus.ServiceUnknown;
                    }
                }
                response = new HealthCheckResponse { Status = status };
            }

            return response;
        }

        private HealthCheckResponse.Types.ServingStatus GetServiceStatus(string service)
        {
            if (statusMap.TryGetValue(service, out HealthCheckResponse.Types.ServingStatus s))
            {
                return s;
            }
            else
            {
                return HealthCheckResponse.Types.ServingStatus.ServiceUnknown;
            }
        }

        private void NotifyStatus(string service, HealthCheckResponse.Types.ServingStatus status)
        {
            if (watchers.TryGetValue(service, out List<IServerStreamWriter<HealthCheckResponse>> serverStreamWriters))
            {
                HealthCheckResponse response = new HealthCheckResponse { Status = status };

                foreach (IServerStreamWriter<HealthCheckResponse> serverStreamWriter in serverStreamWriters)
                {
                    // TODO(JamesNK): This will fail if a pending write is already in progress.
                    _ = serverStreamWriter.WriteAsync(response);
                }
            }
        }
    }
}
