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
using System.Linq;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;

using Grpc.Core.Internal;
using Grpc.Core.Logging;
using Grpc.Core.Utils;

namespace Grpc.Core
{
    /// <summary>
    /// gRPC Channel
    /// </summary>
    public class Channel : IDisposable
    {
        static readonly ILogger Logger = GrpcEnvironment.Logger.ForType<Channel>();

        readonly GrpcEnvironment environment;
        readonly ChannelSafeHandle handle;
        readonly List<ChannelOption> options;
        bool disposed;

        /// <summary>
        /// Creates a channel that connects to a specific host.
        /// Port will default to 80 for an unsecure channel and to 443 for a secure channel.
        /// </summary>
        /// <param name="host">The name or IP address of the host.</param>
        /// <param name="credentials">Credentials to secure the channel.</param>
        /// <param name="options">Channel options.</param>
        public Channel(string host, Credentials credentials, IEnumerable<ChannelOption> options = null)
        {
            Preconditions.CheckNotNull(host);
            this.environment = GrpcEnvironment.GetInstance();
            this.options = options != null ? new List<ChannelOption>(options) : new List<ChannelOption>();

            EnsureUserAgentChannelOption(this.options);
            using (CredentialsSafeHandle nativeCredentials = credentials.ToNativeCredentials())
            using (ChannelArgsSafeHandle nativeChannelArgs = ChannelOptions.CreateChannelArgs(this.options))
            {
                if (nativeCredentials != null)
                {
                    this.handle = ChannelSafeHandle.CreateSecure(nativeCredentials, host, nativeChannelArgs);
                }
                else
                {
                    this.handle = ChannelSafeHandle.CreateInsecure(host, nativeChannelArgs);
                }
            }
        }

        /// <summary>
        /// Creates a channel that connects to a specific host and port.
        /// </summary>
        /// <param name="host">The name or IP address of the host.</param>
        /// <param name="port">The port.</param>
        /// <param name="credentials">Credentials to secure the channel.</param>
        /// <param name="options">Channel options.</param>
        public Channel(string host, int port, Credentials credentials, IEnumerable<ChannelOption> options = null) :
            this(string.Format("{0}:{1}", host, port), credentials, options)
        {
        }

        /// <summary>
        /// Gets current connectivity state of this channel.
        /// </summary>
        public ChannelState State
        {
            get
            {
                return handle.CheckConnectivityState(false);        
            }
        }

        /// <summary>
        /// Returned tasks completes once channel state has become different from 
        /// given lastObservedState. 
        /// If deadline is reached or and error occurs, returned task is cancelled.
        /// </summary>
        public Task WaitForStateChangedAsync(ChannelState lastObservedState, DateTime? deadline = null)
        {
            Preconditions.CheckArgument(lastObservedState != ChannelState.FatalFailure,
                "FatalFailure is a terminal state. No further state changes can occur.");
            var tcs = new TaskCompletionSource<object>();
            var deadlineTimespec = deadline.HasValue ? Timespec.FromDateTime(deadline.Value) : Timespec.InfFuture;
            var handler = new BatchCompletionDelegate((success, ctx) =>
            {
                if (success)
                {
                    tcs.SetResult(null);
                }
                else
                {
                    tcs.SetCanceled();
                }
            });
            handle.WatchConnectivityState(lastObservedState, deadlineTimespec, environment.CompletionQueue, environment.CompletionRegistry, handler);
            return tcs.Task;
        }

        /// <summary> Address of the remote endpoint in URI format.</summary>
        public string Target
        {
            get
            {
                return handle.GetTarget();
            }
        }

        /// <summary>
        /// Allows explicitly requesting channel to connect without starting an RPC.
        /// Returned task completes once state Ready was seen. If the deadline is reached,
        /// or channel enters the FatalFailure state, the task is cancelled.
        /// There is no need to call this explicitly unless your use case requires that.
        /// Starting an RPC on a new channel will request connection implicitly.
        /// </summary>
        public async Task ConnectAsync(DateTime? deadline = null)
        {
            var currentState = handle.CheckConnectivityState(true);
            while (currentState != ChannelState.Ready)
            {
                if (currentState == ChannelState.FatalFailure)
                {
                    throw new OperationCanceledException("Channel has reached FatalFailure state.");
                }
                await WaitForStateChangedAsync(currentState, deadline);
                currentState = handle.CheckConnectivityState(false);
            }
        }

        /// <summary>
        /// Destroys the underlying channel.
        /// </summary>
        public void Dispose()
        {
            Dispose(true);
            GC.SuppressFinalize(this);
        }

        internal ChannelSafeHandle Handle
        {
            get
            {
                return this.handle;
            }
        }

        internal CompletionQueueSafeHandle CompletionQueue
        {
            get
            {
                return this.environment.CompletionQueue;
            }
        }

        internal CompletionRegistry CompletionRegistry
        {
            get
            {
                return this.environment.CompletionRegistry;
            }
        }

        internal GrpcEnvironment Environment
        {
            get
            {
                return this.environment;
            }
        }

        protected virtual void Dispose(bool disposing)
        {
            if (disposing && handle != null && !disposed)
            {
                disposed = true;
                handle.Dispose();
            }
        }

        private static void EnsureUserAgentChannelOption(List<ChannelOption> options)
        {
            if (!options.Any((option) => option.Name == ChannelOptions.PrimaryUserAgentString))
            {
                options.Add(new ChannelOption(ChannelOptions.PrimaryUserAgentString, GetUserAgentString()));
            }
        }

        private static string GetUserAgentString()
        {
            // TODO(jtattermusch): it would be useful to also provide .NET/mono version.
            return string.Format("grpc-csharp/{0}", VersionInfo.CurrentVersion);
        }
    }
}
