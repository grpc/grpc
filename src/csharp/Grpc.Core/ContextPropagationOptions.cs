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

using System;

namespace Grpc.Core
{
    /// <summary>
    /// Options for <see cref="ContextPropagationToken"/>.
    /// </summary>
    public class ContextPropagationOptions
    {
        /// <summary>
        /// The context propagation options that will be used by default.
        /// </summary>
        public static readonly ContextPropagationOptions Default = new ContextPropagationOptions();

        bool propagateDeadline;
        bool propagateCancellation;

        /// <summary>
        /// Creates new context propagation options.
        /// </summary>
        /// <param name="propagateDeadline">If set to <c>true</c> parent call's deadline will be propagated to the child call.</param>
        /// <param name="propagateCancellation">If set to <c>true</c> parent call's cancellation token will be propagated to the child call.</param>
        public ContextPropagationOptions(bool propagateDeadline = true, bool propagateCancellation = true)
        {
            this.propagateDeadline = propagateDeadline;
            this.propagateCancellation = propagateCancellation;
        }
            
        /// <summary><c>true</c> if parent call's deadline should be propagated to the child call.</summary>
        public bool IsPropagateDeadline
        {
            get { return this.propagateDeadline; }
        }

        /// <summary><c>true</c> if parent call's cancellation token should be propagated to the child call.</summary>
        public bool IsPropagateCancellation
        {
            get { return this.propagateCancellation; }
        }
    }
}
