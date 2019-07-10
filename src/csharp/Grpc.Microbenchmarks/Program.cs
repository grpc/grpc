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
using PooledAwait;

namespace Grpc.Microbenchmarks
{
    class Program
    {
        public static Task Main()
        {
            return Impl();
            async PooledTask Impl()
            {
                var impl = new PingBenchmark();
                await impl.Setup();
                for (int i = 0; i < 10000; i++)
                {
                    await impl.PingAsync();
                }
                await impl.Cleanup();
            }
        }
    }
}
