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

using System;

namespace Grpc.Core.Internal
{
    /// <summary>
    /// An object that can be pooled in <c>IObjectPool</c>.
    /// </summary>
    /// <typeparam name="T"></typeparam>
    internal interface IPooledObject<T> : IDisposable
    {
        /// <summary>
        /// Set the action that will be invoked to return a leased object to the pool.
        /// </summary>
        void SetReturnToPoolAction(Action<T> returnAction);
    }
}
