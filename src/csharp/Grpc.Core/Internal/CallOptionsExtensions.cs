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
using Grpc.Core.Utils;

namespace Grpc.Core.Internal
{
    internal static class CallOptionsExtensions
    {
        /// <summary>
        /// Returns a new instance of <see cref="CallOptions"/> with
        /// all previously unset values set to their defaults and deadline and cancellation
        /// token propagated when appropriate.
        /// </summary>
        internal static CallOptions Normalize(this CallOptions options)
        {
            var newOptions = options;
            // silently ignore the context propagation token if it wasn't produced by "us"
            var propagationTokenImpl = options.PropagationToken.AsImplOrNull();
            if (propagationTokenImpl != null)
            {
                if (propagationTokenImpl.Options.IsPropagateDeadline)
                {
                    GrpcPreconditions.CheckArgument(!newOptions.Deadline.HasValue,
                        "Cannot propagate deadline from parent call. The deadline has already been set explicitly.");
                    newOptions = newOptions.WithDeadline(propagationTokenImpl.ParentDeadline);
                }
                if (propagationTokenImpl.Options.IsPropagateCancellation)
                {
                    GrpcPreconditions.CheckArgument(!newOptions.CancellationToken.CanBeCanceled,
                        "Cannot propagate cancellation token from parent call. The cancellation token has already been set to a non-default value.");
                    newOptions = newOptions.WithCancellationToken(propagationTokenImpl.ParentCancellationToken);
                }
            }

            newOptions = newOptions.WithHeaders(newOptions.Headers ?? Metadata.Empty);
            newOptions = newOptions.WithDeadline(newOptions.Deadline ?? DateTime.MaxValue);
            return newOptions;
        }
    }
}
