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
#if GRPC_SUPPORT_WATCH
using System.Threading.Channels;
#endif
using System.Threading.Tasks;

using Grpc.Core;
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
        // The maximum number of statuses to buffer on the server.
        internal const int MaxStatusBufferSize = 5;

        private readonly object statusLock = new object();
        private readonly Dictionary<string, HealthCheckResponse.Types.ServingStatus> statusMap =
            new Dictionary<string, HealthCheckResponse.Types.ServingStatus>();

#if GRPC_SUPPORT_WATCH
        private readonly object watchersLock = new object();
        private readonly Dictionary<string, List<ChannelWriter<HealthCheckResponse>>> watchers =
            new Dictionary<string, List<ChannelWriter<HealthCheckResponse>>>();
#endif

        /// <summary>
        /// Sets the health status for given service.
        /// </summary>
        /// <param name="service">The service. Cannot be null.</param>
        /// <param name="status">the health status</param>
        public void SetStatus(string service, HealthCheckResponse.Types.ServingStatus status)
        {
            HealthCheckResponse.Types.ServingStatus previousStatus;
            lock (statusLock)
            {
                previousStatus = GetServiceStatus(service);
                statusMap[service] = status;
            }

#if GRPC_SUPPORT_WATCH
            if (status != previousStatus)
            {
                NotifyStatus(service, status);
            }
#endif
        }

        /// <summary>
        /// Clears health status for given service.
        /// </summary>
        /// <param name="service">The service. Cannot be null.</param>
        public void ClearStatus(string service)
        {
            HealthCheckResponse.Types.ServingStatus previousStatus;
            lock (statusLock)
            {
                previousStatus = GetServiceStatus(service);
                statusMap.Remove(service);
            }

#if GRPC_SUPPORT_WATCH
            if (previousStatus != HealthCheckResponse.Types.ServingStatus.ServiceUnknown)
            {
                NotifyStatus(service, HealthCheckResponse.Types.ServingStatus.ServiceUnknown);
            }
#endif
        }

        /// <summary>
        /// Clears statuses for all services.
        /// </summary>
        public void ClearAll()
        {
            List<KeyValuePair<string, HealthCheckResponse.Types.ServingStatus>> statuses;
            lock (statusLock)
            {
                statuses = statusMap.ToList();
                statusMap.Clear();
            }

#if GRPC_SUPPORT_WATCH
            foreach (KeyValuePair<string, HealthCheckResponse.Types.ServingStatus> status in statuses)
            {
                if (status.Value != HealthCheckResponse.Types.ServingStatus.ServiceUnknown)
                {
                    NotifyStatus(status.Key, HealthCheckResponse.Types.ServingStatus.ServiceUnknown);
                }
            }
#endif
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

#if GRPC_SUPPORT_WATCH
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

            // Channel is used to to marshall multiple callers updating status into a single queue.
            // This is required because IServerStreamWriter is not thread safe.
            //
            // A queue of unwritten statuses could build up if flow control causes responseStream.WriteAsync to await.
            // When this number is exceeded the server will discard older statuses. The discarded intermediate statues
            // will never be sent to the client.
            Channel<HealthCheckResponse> channel = Channel.CreateBounded<HealthCheckResponse>(new BoundedChannelOptions(capacity: MaxStatusBufferSize) {
                SingleReader = true,
                SingleWriter = false,
                FullMode = BoundedChannelFullMode.DropOldest
            });

            lock (watchersLock)
            {
                if (!watchers.TryGetValue(service, out List<ChannelWriter<HealthCheckResponse>> channelWriters))
                {
                    channelWriters = new List<ChannelWriter<HealthCheckResponse>>();
                    watchers.Add(service, channelWriters);
                }

                channelWriters.Add(channel.Writer);
            }

            // Watch calls run until ended by the client canceling them.
            context.CancellationToken.Register(() => {
                lock (watchersLock)
                {
                    if (watchers.TryGetValue(service, out List<ChannelWriter<HealthCheckResponse>> channelWriters))
                    {
                        // Remove the writer from the watchers
                        if (channelWriters.Remove(channel.Writer))
                        {
                            // Remove empty collection if service has no more response streams
                            if (channelWriters.Count == 0)
                            {
                                watchers.Remove(service);
                            }
                        }
                    }
                }

                // Signal the writer is complete and the watch method can exit.
                channel.Writer.Complete();
            });

            // Send current status immediately
            HealthCheckResponse response = GetHealthCheckResponse(service, throwOnNotFound: false);
            await responseStream.WriteAsync(response);

            // Read messages. WaitToReadAsync will wait until new messages are available.
            // Loop will exit when the call is canceled and the writer is marked as complete.
            while (await channel.Reader.WaitToReadAsync())
            {
                if (channel.Reader.TryRead(out HealthCheckResponse item))
                {
                    await responseStream.WriteAsync(item);
                }
            }
        }

        private void NotifyStatus(string service, HealthCheckResponse.Types.ServingStatus status)
        {
            lock (watchersLock)
            {
                if (watchers.TryGetValue(service, out List<ChannelWriter<HealthCheckResponse>> channelWriters))
                {
                    HealthCheckResponse response = new HealthCheckResponse { Status = status };

                    foreach (ChannelWriter<HealthCheckResponse> writer in channelWriters)
                    {
                        if (!writer.TryWrite(response))
                        {
                            throw new InvalidOperationException("Unable to queue health check notification.");
                        }
                    }
                }
            }
        }
#endif

        private HealthCheckResponse GetHealthCheckResponse(string service, bool throwOnNotFound)
        {
            HealthCheckResponse response = null;
            lock (statusLock)
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
                // A service with no set status has a status of ServiceUnknown
                return HealthCheckResponse.Types.ServingStatus.ServiceUnknown;
            }
        }
    }
}
