#region Copyright notice and license

// Copyright 2018 gRPC authors.
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

using System.Linq;
using Microsoft.Build.Framework;
using Microsoft.Build.Utilities;

namespace Grpc.Tools.Tests
{
    static class Utils
    {
        // Build an item with a name from args[0] and metadata key-value pairs
        // from the rest of args, interleaved.
        // This does not do any checking, and expects an odd number of args.
        public static ITaskItem MakeItem(params string[] args)
        {
            var item = new TaskItem(args[0]);
            for (int i = 1; i < args.Length; i += 2)
            {
                item.SetMetadata(args[i], args[i + 1]);
            }
            return item;
        }

        // Return an array of items from given itemspecs.
        public static ITaskItem[] MakeSimpleItems(params string[] specs)
        {
            return specs.Select(s => new TaskItem(s)).ToArray();
        }
    };
}
