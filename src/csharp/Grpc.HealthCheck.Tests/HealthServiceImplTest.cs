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

        private static HealthCheckResponse.Types.ServingStatus GetStatusHelper(HealthServiceImpl impl, string service)
        {
            return impl.Check(new HealthCheckRequest { Service = service }, null).Result.Status;
        }
    }
}
