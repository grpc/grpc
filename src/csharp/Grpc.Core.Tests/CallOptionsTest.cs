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
using System.Collections.Generic;
using System.Threading;
using Grpc.Core;
using Grpc.Core.Internal;
using Grpc.Core.Utils;
using NUnit.Framework;

namespace Grpc.Core.Tests
{
    public class CallOptionsTest
    {
        [Test]
        public void WithMethods()
        {
            var options = new CallOptions();
            
            var metadata = new Metadata();
            Assert.AreSame(metadata, options.WithHeaders(metadata).Headers);

            var deadline = DateTime.UtcNow;
            Assert.AreEqual(deadline, options.WithDeadline(deadline).Deadline.Value);

            var cancellationToken = new CancellationTokenSource().Token;
            Assert.AreEqual(cancellationToken, options.WithCancellationToken(cancellationToken).CancellationToken);

            var writeOptions = new WriteOptions();
            Assert.AreSame(writeOptions, options.WithWriteOptions(writeOptions).WriteOptions);

            var propagationToken = new ContextPropagationToken(CallSafeHandle.NullInstance, DateTime.UtcNow, 
                CancellationToken.None, ContextPropagationOptions.Default);
            Assert.AreSame(propagationToken, options.WithPropagationToken(propagationToken).PropagationToken);

            var credentials = new FakeCallCredentials();
            Assert.AreSame(credentials, options.WithCredentials(credentials).Credentials);

            var flags = CallFlags.WaitForReady | CallFlags.CacheableRequest;
            Assert.AreEqual(flags, options.WithFlags(flags).Flags);

            // Check that the original instance is unchanged.
            Assert.IsNull(options.Headers);
            Assert.IsNull(options.Deadline);
            Assert.AreEqual(CancellationToken.None, options.CancellationToken);
            Assert.IsNull(options.WriteOptions);
            Assert.IsNull(options.PropagationToken);
            Assert.IsNull(options.Credentials);
            Assert.AreEqual(default(CallFlags), options.Flags);
        }

        [Test]
        public void Normalize()
        {
            Assert.AreSame(Metadata.Empty, new CallOptions().Normalize().Headers);
            Assert.AreEqual(DateTime.MaxValue, new CallOptions().Normalize().Deadline.Value);

            var deadline = DateTime.UtcNow;
            var propagationToken1 = new ContextPropagationToken(CallSafeHandle.NullInstance, deadline, CancellationToken.None,
                new ContextPropagationOptions(propagateDeadline: true, propagateCancellation: false));
            Assert.AreEqual(deadline, new CallOptions(propagationToken: propagationToken1).Normalize().Deadline.Value);
            Assert.Throws(typeof(ArgumentException), () => new CallOptions(deadline: deadline, propagationToken: propagationToken1).Normalize());

            var token = new CancellationTokenSource().Token;
            var propagationToken2 = new ContextPropagationToken(CallSafeHandle.NullInstance, deadline, token,
                new ContextPropagationOptions(propagateDeadline: false, propagateCancellation: true));
            Assert.AreEqual(token, new CallOptions(propagationToken: propagationToken2).Normalize().CancellationToken);
            Assert.Throws(typeof(ArgumentException), () => new CallOptions(cancellationToken: token, propagationToken: propagationToken2).Normalize());
        }

        [Test]
        public void WaitForReady()
        {
            var callOptions = new CallOptions();
            Assert.IsFalse(callOptions.IsWaitForReady);

            Assert.AreEqual(CallFlags.WaitForReady, callOptions.WithWaitForReady().Flags);
            Assert.IsTrue(callOptions.WithWaitForReady().IsWaitForReady);
            Assert.IsFalse(callOptions.WithWaitForReady(true).WithWaitForReady(false).IsWaitForReady);
        }
    }
}
