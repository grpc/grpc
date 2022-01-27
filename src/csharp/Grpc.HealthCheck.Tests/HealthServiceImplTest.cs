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
using System.Diagnostics;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

using Grpc.Core;
using Grpc.Health.V1;
using NUnit.Framework;

namespace Grpc.HealthCheck.Tests
{
    /// <summary>
    /// Tests for HealthCheckServiceImpl
    /// </summary>
    public class HealthServiceImplTest
    {
        [Test]
        public void SetStatus()
        {
            var impl = new HealthServiceImpl();
            impl.SetStatus("", HealthCheckResponse.Types.ServingStatus.Serving);
            Assert.AreEqual(HealthCheckResponse.Types.ServingStatus.Serving, GetStatusHelper(impl, ""));

            impl.SetStatus("", HealthCheckResponse.Types.ServingStatus.NotServing);
            Assert.AreEqual(HealthCheckResponse.Types.ServingStatus.NotServing, GetStatusHelper(impl, ""));

            impl.SetStatus("", HealthCheckResponse.Types.ServingStatus.Unknown);
            Assert.AreEqual(HealthCheckResponse.Types.ServingStatus.Unknown, GetStatusHelper(impl, ""));

            impl.SetStatus("grpc.test.TestService", HealthCheckResponse.Types.ServingStatus.Serving);
            Assert.AreEqual(HealthCheckResponse.Types.ServingStatus.Serving, GetStatusHelper(impl, "grpc.test.TestService"));
        }

        [Test]
        public void ClearStatus()
        {
            var impl = new HealthServiceImpl();
            impl.SetStatus("", HealthCheckResponse.Types.ServingStatus.Serving);
            impl.SetStatus("grpc.test.TestService", HealthCheckResponse.Types.ServingStatus.Unknown);

            impl.ClearStatus("");

            var ex = Assert.Throws<RpcException>(() => GetStatusHelper(impl, ""));
            Assert.AreEqual(StatusCode.NotFound, ex.Status.StatusCode);
            Assert.AreEqual(HealthCheckResponse.Types.ServingStatus.Unknown, GetStatusHelper(impl, "grpc.test.TestService"));
        }

        [Test]
        public void ClearAll()
        {
            var impl = new HealthServiceImpl();
            impl.SetStatus("", HealthCheckResponse.Types.ServingStatus.Serving);
            impl.SetStatus("grpc.test.TestService", HealthCheckResponse.Types.ServingStatus.Unknown);

            impl.ClearAll();
            Assert.Throws(typeof(RpcException), () => GetStatusHelper(impl, ""));
            Assert.Throws(typeof(RpcException), () => GetStatusHelper(impl, "grpc.test.TestService"));
        }

        [Test]
        public void NullsRejected()
        {
            var impl = new HealthServiceImpl();
            Assert.Throws(typeof(ArgumentNullException), () => impl.SetStatus(null, HealthCheckResponse.Types.ServingStatus.Serving));

            Assert.Throws(typeof(ArgumentNullException), () => impl.ClearStatus(null));
        }

#if GRPC_SUPPORT_WATCH
        [Test]
        public async Task Watch()
        {
            var cts = new CancellationTokenSource();
            var context = new TestServerCallContext(cts.Token);
            var writer = new TestResponseStreamWriter();

            var impl = new HealthServiceImpl();
            var callTask = impl.Watch(new HealthCheckRequest { Service = "" }, writer, context);

            // Calling Watch on a service that doesn't have a value set will initially return ServiceUnknown
            var nextWriteTask = writer.WrittenMessagesReader.ReadAsync();
            Assert.AreEqual(HealthCheckResponse.Types.ServingStatus.ServiceUnknown, (await nextWriteTask).Status);

            nextWriteTask = writer.WrittenMessagesReader.ReadAsync();
            impl.SetStatus("", HealthCheckResponse.Types.ServingStatus.Serving);
            Assert.AreEqual(HealthCheckResponse.Types.ServingStatus.Serving, (await nextWriteTask).Status);

            nextWriteTask = writer.WrittenMessagesReader.ReadAsync();
            impl.SetStatus("", HealthCheckResponse.Types.ServingStatus.NotServing);
            Assert.AreEqual(HealthCheckResponse.Types.ServingStatus.NotServing, (await nextWriteTask).Status);

            nextWriteTask = writer.WrittenMessagesReader.ReadAsync();
            impl.SetStatus("", HealthCheckResponse.Types.ServingStatus.Unknown);
            Assert.AreEqual(HealthCheckResponse.Types.ServingStatus.Unknown, (await nextWriteTask).Status);

            // Setting status for a different service name will not update Watch results
            nextWriteTask = writer.WrittenMessagesReader.ReadAsync();
            impl.SetStatus("grpc.test.TestService", HealthCheckResponse.Types.ServingStatus.Serving);
            Assert.IsFalse(nextWriteTask.IsCompleted);

            impl.ClearStatus("");
            Assert.AreEqual(HealthCheckResponse.Types.ServingStatus.ServiceUnknown, (await nextWriteTask).Status);

            Assert.IsFalse(callTask.IsCompleted);
            cts.Cancel();
            await callTask;
        }

        [Test]
        public async Task Watch_MultipleWatchesForSameService()
        {
            var cts = new CancellationTokenSource();
            var context = new TestServerCallContext(cts.Token);
            var writer1 = new TestResponseStreamWriter();
            var writer2 = new TestResponseStreamWriter();

            var impl = new HealthServiceImpl();
            var callTask1 = impl.Watch(new HealthCheckRequest { Service = "" }, writer1, context);
            var callTask2 = impl.Watch(new HealthCheckRequest { Service = "" }, writer2, context);

            // Calling Watch on a service that doesn't have a value set will initially return ServiceUnknown
            var nextWriteTask1 = writer1.WrittenMessagesReader.ReadAsync();
            var nextWriteTask2 = writer2.WrittenMessagesReader.ReadAsync();
            Assert.AreEqual(HealthCheckResponse.Types.ServingStatus.ServiceUnknown, (await nextWriteTask1).Status);
            Assert.AreEqual(HealthCheckResponse.Types.ServingStatus.ServiceUnknown, (await nextWriteTask2).Status);

            nextWriteTask1 = writer1.WrittenMessagesReader.ReadAsync();
            nextWriteTask2 = writer2.WrittenMessagesReader.ReadAsync();
            impl.SetStatus("", HealthCheckResponse.Types.ServingStatus.Serving);
            Assert.AreEqual(HealthCheckResponse.Types.ServingStatus.Serving, (await nextWriteTask1).Status);
            Assert.AreEqual(HealthCheckResponse.Types.ServingStatus.Serving, (await nextWriteTask2).Status);

            nextWriteTask1 = writer1.WrittenMessagesReader.ReadAsync();
            nextWriteTask2 = writer2.WrittenMessagesReader.ReadAsync();
            impl.ClearStatus("");
            Assert.AreEqual(HealthCheckResponse.Types.ServingStatus.ServiceUnknown, (await nextWriteTask1).Status);
            Assert.AreEqual(HealthCheckResponse.Types.ServingStatus.ServiceUnknown, (await nextWriteTask2).Status);

            cts.Cancel();
            await callTask1;
            await callTask2;
        }

        [Test]
        public async Task Watch_MultipleWatchesForDifferentServices()
        {
            var cts = new CancellationTokenSource();
            var context = new TestServerCallContext(cts.Token);
            var writer1 = new TestResponseStreamWriter();
            var writer2 = new TestResponseStreamWriter();

            var impl = new HealthServiceImpl();
            var callTask1 = impl.Watch(new HealthCheckRequest { Service = "One" }, writer1, context);
            var callTask2 = impl.Watch(new HealthCheckRequest { Service = "Two" }, writer2, context);

            // Calling Watch on a service that doesn't have a value set will initially return ServiceUnknown
            var nextWriteTask1 = writer1.WrittenMessagesReader.ReadAsync();
            var nextWriteTask2 = writer2.WrittenMessagesReader.ReadAsync();
            Assert.AreEqual(HealthCheckResponse.Types.ServingStatus.ServiceUnknown, (await nextWriteTask1).Status);
            Assert.AreEqual(HealthCheckResponse.Types.ServingStatus.ServiceUnknown, (await nextWriteTask2).Status);

            nextWriteTask1 = writer1.WrittenMessagesReader.ReadAsync();
            nextWriteTask2 = writer2.WrittenMessagesReader.ReadAsync();
            impl.SetStatus("One", HealthCheckResponse.Types.ServingStatus.Serving);
            impl.SetStatus("Two", HealthCheckResponse.Types.ServingStatus.NotServing);
            Assert.AreEqual(HealthCheckResponse.Types.ServingStatus.Serving, (await nextWriteTask1).Status);
            Assert.AreEqual(HealthCheckResponse.Types.ServingStatus.NotServing, (await nextWriteTask2).Status);

            nextWriteTask1 = writer1.WrittenMessagesReader.ReadAsync();
            nextWriteTask2 = writer2.WrittenMessagesReader.ReadAsync();
            impl.ClearAll();
            Assert.AreEqual(HealthCheckResponse.Types.ServingStatus.ServiceUnknown, (await nextWriteTask1).Status);
            Assert.AreEqual(HealthCheckResponse.Types.ServingStatus.ServiceUnknown, (await nextWriteTask2).Status);

            cts.Cancel();
            await callTask1;
            await callTask2;
        }

        [Test]
        public async Task Watch_ExceedMaximumCapacitySize_DiscardOldValues()
        {
            var cts = new CancellationTokenSource();
            var context = new TestServerCallContext(cts.Token);
            var writer = new TestResponseStreamWriter(started: false);

            var impl = new HealthServiceImpl();
            var callTask = impl.Watch(new HealthCheckRequest { Service = "" }, writer, context);

            // Write new statuses. Only last statuses will be returned when we read them from watch writer
            for (var i = 0; i < HealthServiceImpl.MaxStatusBufferSize * 2; i++)
            {
                // These statuses aren't "valid" but it is useful for testing to have an incrementing number
                impl.SetStatus("", (HealthCheckResponse.Types.ServingStatus)i + 10);
            }

            // Start reading responses now that statuses have been queued up
            // This is to keep the test non-flakey
            writer.Start();

            // Read messages in a background task
            var statuses = new List<HealthCheckResponse.Types.ServingStatus>();
            var readStatusesTask = Task.Run(async () => {
                while (await writer.WrittenMessagesReader.WaitToReadAsync())
                {
                    if (writer.WrittenMessagesReader.TryRead(out var response))
                    {
                        statuses.Add(response.Status);
                    }
                }
            });

            // Tell server we're done watching and it can write what it has left and then exit
            cts.Cancel();
            await callTask;

            // Ensure we've read all the queued statuses
            writer.Complete();
            await readStatusesTask;

            // Collection will contain initial written message (ServiceUnknown) plus 5 queued messages
            Assert.AreEqual(HealthServiceImpl.MaxStatusBufferSize + 1, statuses.Count);

            // Initial written message
            Assert.AreEqual(HealthCheckResponse.Types.ServingStatus.ServiceUnknown, statuses[0]);

            // Last 5 queued messages
            Assert.AreEqual((HealthCheckResponse.Types.ServingStatus)15, statuses[statuses.Count - 5]);
            Assert.AreEqual((HealthCheckResponse.Types.ServingStatus)16, statuses[statuses.Count - 4]);
            Assert.AreEqual((HealthCheckResponse.Types.ServingStatus)17, statuses[statuses.Count - 3]);
            Assert.AreEqual((HealthCheckResponse.Types.ServingStatus)18, statuses[statuses.Count - 2]);
            Assert.AreEqual((HealthCheckResponse.Types.ServingStatus)19, statuses[statuses.Count - 1]);
        }
#endif

        private static HealthCheckResponse.Types.ServingStatus GetStatusHelper(HealthServiceImpl impl, string service)
        {
            return impl.Check(new HealthCheckRequest { Service = service }, null).Result.Status;
        }
    }
}
