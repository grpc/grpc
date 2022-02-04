#region Copyright notice and license
// Copyright 2022 gRPC authors.
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

namespace Grpc.Core.Internal
{
    // mainly for use with Task<T> replies when receiving results

    // this is basically (bool HasValue, T Value), but without the need to ref System.ValueTuple;
    // alternatively you could think of it as Nullable<T> but for any T (not just T : struct)
    internal readonly struct Maybe<T>
    {
        public bool HasValue { get; }
        public T Value { get; }
        public Maybe(T value)
        {
            HasValue = true;
            Value = value;
        }
        public static Maybe<T> Empty => default;
    }
}
