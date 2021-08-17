#region Copyright notice and license
// Copyright 2015-2016 gRPC authors.
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

namespace Math
{
    class MathClient
    {
        public static async Task Main(string[] args)
        {
            var channel = new Channel("127.0.0.1", 23456, ChannelCredentials.Insecure);
            Math.MathClient client = new Math.MathClient(channel);
            MathExamples.DivExample(client);

            await MathExamples.DivAsyncExample(client);

            await MathExamples.FibExample(client);

            await MathExamples.SumExample(client);

            await MathExamples.DivManyExample(client);

            await MathExamples.DependentRequestsExample(client);

            await channel.ShutdownAsync();
        }
    }
}
