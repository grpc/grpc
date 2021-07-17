#region Copyright notice and license

// Copyright 2019 The gRPC Authors
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

using System.Threading.Tasks;
using NUnit.Framework;

namespace Grpc.Core.Internal.Tests
{
    public class AsyncCallStateTest
    {
        [Test]
        public void Stateless()
        {
            bool disposed = false;
            Task<Metadata> responseHeaders = Task.FromResult(new Metadata());
            Metadata trailers = new Metadata();
            var state = new AsyncCallState(responseHeaders, () => new Status(StatusCode.DataLoss, "oops"),
                () => trailers, () => disposed = true);

            Assert.AreSame(responseHeaders, state.ResponseHeadersAsync());

            var status = state.GetStatus();
            Assert.AreEqual(StatusCode.DataLoss, status.StatusCode);
            Assert.AreEqual("oops", status.Detail);

            Assert.AreSame(trailers, state.GetTrailers());

            Assert.False(disposed);
            state.Dispose();
            Assert.True(disposed);
        }

        class State
        {
            public bool disposed = false;
            public Task<Metadata> responseHeaders = Task.FromResult(new Metadata());
            public Metadata trailers = new Metadata();
            public Status status = new Status(StatusCode.DataLoss, "oops");
            public void Dispose() { disposed = true; }
        }

        [Test]
        public void WithState()
        {
            var callbackState = new State();

            var state = new AsyncCallState(
                obj => ((State)obj).responseHeaders,
                obj => ((State)obj).status,
                obj => ((State)obj).trailers,
                obj => ((State)obj).Dispose(),
                callbackState);

            Assert.AreSame(callbackState.responseHeaders, state.ResponseHeadersAsync());

            var status = state.GetStatus();
            Assert.AreEqual(StatusCode.DataLoss, status.StatusCode);
            Assert.AreEqual("oops", status.Detail);

            Assert.AreSame(callbackState.trailers, state.GetTrailers());

            Assert.False(callbackState.disposed);
            state.Dispose();
            Assert.True(callbackState.disposed);
        }
    }
}
