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

using System;

namespace Grpc.Core.Utils
{
    /// <summary>
    /// Utility methods to simplify checking preconditions in the code.
    /// </summary>
    public static class GrpcPreconditions
    {
        /// <summary>
        /// Throws <see cref="ArgumentException"/> if condition is false.
        /// </summary>
        /// <param name="condition">The condition.</param>
        public static void CheckArgument(bool condition)
        {
            if (!condition)
            {
                throw new ArgumentException();
            }
        }

        /// <summary>
        /// Throws <see cref="ArgumentException"/> with given message if condition is false.
        /// </summary>
        /// <param name="condition">The condition.</param>
        /// <param name="errorMessage">The error message.</param>
        public static void CheckArgument(bool condition, string errorMessage)
        {
            if (!condition)
            {
                throw new ArgumentException(errorMessage);
            }
        }

        /// <summary>
        /// Throws <see cref="ArgumentNullException"/> if reference is null.
        /// </summary>
        /// <param name="reference">The reference.</param>
        public static T CheckNotNull<T>(T reference)
        {
            if (reference == null)
            {
                throw new ArgumentNullException();
            }
            return reference;
        }

        /// <summary>
        /// Throws <see cref="ArgumentNullException"/> if reference is null.
        /// </summary>
        /// <param name="reference">The reference.</param>
        /// <param name="paramName">The parameter name.</param>
        public static T CheckNotNull<T>(T reference, string paramName)
        {
            if (reference == null)
            {
                throw new ArgumentNullException(paramName);
            }
            return reference;
        }

        /// <summary>
        /// Throws <see cref="InvalidOperationException"/> if condition is false.
        /// </summary>
        /// <param name="condition">The condition.</param>
        public static void CheckState(bool condition)
        {
            if (!condition)
            {
                throw new InvalidOperationException();
            }
        }

        /// <summary>
        /// Throws <see cref="InvalidOperationException"/> with given message if condition is false.
        /// </summary>
        /// <param name="condition">The condition.</param>
        /// <param name="errorMessage">The error message.</param>
        public static void CheckState(bool condition, string errorMessage)
        {
            if (!condition)
            {
                throw new InvalidOperationException(errorMessage);
            }
        }
    }
}
