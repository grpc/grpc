#region Copyright notice and license

// Copyright 2015, Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

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
