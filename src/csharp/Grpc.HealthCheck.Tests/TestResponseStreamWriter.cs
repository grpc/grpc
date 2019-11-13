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

using System.Threading.Tasks;

using Grpc.Core;
using Grpc.Health.V1;

namespace Grpc.HealthCheck.Tests
{
    private class TestResponseStreamWriter : IServerStreamWriter<HealthCheckResponse>
    {
        private TaskCompletionSource<HealthCheckResponse> _tcs;

        public WriteOptions WriteOptions { get; set; }

        public Task<HealthCheckResponse> WaitNextAsync()
        {
            _tcs = new TaskCompletionSource<HealthCheckResponse>();
            return _tcs.Task;
        }

        public Task WriteAsync(HealthCheckResponse message)
        {
            if (_tcs != null)
            {
                _tcs.TrySetResult(message);
            }

            return Task.FromResult<object>(null);
        }
    }
}
