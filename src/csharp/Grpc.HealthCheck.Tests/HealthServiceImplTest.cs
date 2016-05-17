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
            impl.SetStatus("", HealthCheckResponse.Types.ServingStatus.SERVING);
            Assert.AreEqual(HealthCheckResponse.Types.ServingStatus.SERVING, GetStatusHelper(impl, ""));

            impl.SetStatus("", HealthCheckResponse.Types.ServingStatus.NOT_SERVING);
            Assert.AreEqual(HealthCheckResponse.Types.ServingStatus.NOT_SERVING, GetStatusHelper(impl, ""));

            impl.SetStatus("", HealthCheckResponse.Types.ServingStatus.UNKNOWN);
            Assert.AreEqual(HealthCheckResponse.Types.ServingStatus.UNKNOWN, GetStatusHelper(impl, ""));

            impl.SetStatus("grpc.test.TestService", HealthCheckResponse.Types.ServingStatus.SERVING);
            Assert.AreEqual(HealthCheckResponse.Types.ServingStatus.SERVING, GetStatusHelper(impl, "grpc.test.TestService"));
        }

        [Test]
        public void ClearStatus()
        {
            var impl = new HealthServiceImpl();
            impl.SetStatus("", HealthCheckResponse.Types.ServingStatus.SERVING);
            impl.SetStatus("grpc.test.TestService", HealthCheckResponse.Types.ServingStatus.UNKNOWN);

            impl.ClearStatus("");

            Assert.Throws(Is.TypeOf(typeof(RpcException)).And.Property("Status").Property("StatusCode").EqualTo(StatusCode.NotFound), () => GetStatusHelper(impl, ""));
            Assert.AreEqual(HealthCheckResponse.Types.ServingStatus.UNKNOWN, GetStatusHelper(impl, "grpc.test.TestService"));
        }

        [Test]
        public void ClearAll()
        {
            var impl = new HealthServiceImpl();
            impl.SetStatus("", HealthCheckResponse.Types.ServingStatus.SERVING);
            impl.SetStatus("grpc.test.TestService", HealthCheckResponse.Types.ServingStatus.UNKNOWN);

            impl.ClearAll();
            Assert.Throws(typeof(RpcException), () => GetStatusHelper(impl, ""));
            Assert.Throws(typeof(RpcException), () => GetStatusHelper(impl, "grpc.test.TestService"));
        }

        [Test]
        public void NullsRejected()
        {
            var impl = new HealthServiceImpl();
            Assert.Throws(typeof(ArgumentNullException), () => impl.SetStatus(null, HealthCheckResponse.Types.ServingStatus.SERVING));

            Assert.Throws(typeof(ArgumentNullException), () => impl.ClearStatus(null));
        }

        private static HealthCheckResponse.Types.ServingStatus GetStatusHelper(HealthServiceImpl impl, string service)
        {
            return impl.Check(new HealthCheckRequest { Service = service }, null).Result.Status;
        }
    }
}
