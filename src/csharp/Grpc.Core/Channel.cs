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
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;

using Grpc.Core.Internal;
using Grpc.Core.Logging;
using Grpc.Core.Utils;

namespace Grpc.Core
{
    /// <summary>
    /// Represents a gRPC channel. Channels are an abstraction of long-lived connections to remote servers.
    /// More client objects can reuse the same channel. Creating a channel is an expensive operation compared to invoking
    /// a remote call so in general you should reuse a single channel for as many calls as possible.
    /// </summary>
    public class Channel : ChannelBase
    {
        static readonly ILogger Logger = GrpcEnvironment.Logger.ForType<Channel>();

        readonly object myLock = new object();
        readonly AtomicCounter activeCallCounter = new AtomicCounter();
        readonly CancellationTokenSource shutdownTokenSource = new CancellationTokenSource();

        readonly GrpcEnvironment environment;
        readonly CompletionQueueSafeHandle completionQueue;
        readonly ChannelSafeHandle handle;
        readonly Dictionary<string, ChannelOption> options;

        bool shutdownRequested;

        /// <summary>
        /// Creates a channel that connects to a specific host.
        /// Port will default to 80 for an unsecure channel and to 443 for a secure channel.
        /// </summary>
        /// <param name="target">Target of the channel.</param>
        /// <param name="credentials">Credentials to secure the channel.</param>
        public Channel(string target, ChannelCredentials credentials) :
            this(target, credentials, null)
        {
        }

        /// <summary>
        /// Creates a channel that connects to a specific host.
        /// Port will default to 80 for an unsecure channel or to 443 for a secure channel.
        /// </summary>
        /// <param name="target">Target of the channel.</param>
        /// <param name="credentials">Credentials to secure the channel.</param>
        /// <param name="options">Channel options.</param>
        public Channel(string target, ChannelCredentials credentials, IEnumerable<ChannelOption> options) : base(target)
        {
            this.options = CreateOptionsDictionary(options);
            EnsureUserAgentChannelOption(this.options);
            this.environment = GrpcEnvironment.AddRef();

            this.completionQueue = this.environment.PickCompletionQueue();
            using (var nativeChannelArgs = ChannelOptions.CreateChannelArgs(this.options.Values))
            {
                var nativeCredentials = credentials.ToNativeCredentials();
                if (nativeCredentials != null)
                {
                    this.handle = ChannelSafeHandle.CreateSecure(nativeCredentials, target, nativeChannelArgs);
                }
                else
                {
                    this.handle = ChannelSafeHandle.CreateInsecure(target, nativeChannelArgs);
                }
            }
            GrpcEnvironment.RegisterChannel(this);
        }

        /// <summary>
        /// Creates a channel that connects to a specific host and port.
        /// </summary>
        /// <param name="host">The name or IP address of the host.</param>
        /// <param name="port">The port.</param>
        /// <param name="credentials">Credentials to secure the channel.</param>
        public Channel(string host, int port, ChannelCredentials credentials) :
            this(host, port, credentials, null)
        {
        }

        /// <summary>
        /// Creates a channel that connects to a specific host and port.
        /// </summary>
        /// <param name="host">The name or IP address of the host.</param>
        /// <param name="port">The port.</param>
        /// <param name="credentials">Credentials to secure the channel.</param>
        /// <param name="options">Channel options.</param>
        public Channel(string host, int port, ChannelCredentials credentials, IEnumerable<ChannelOption> options) :
            this(string.Format("{0}:{1}", host, port), credentials, options)
        {
        }

        /// <summary>
        /// Gets current connectivity state of this channel.
        /// After channel has been shutdown, <c>ChannelState.Shutdown</c> will be returned.
        /// </summary>
        public ChannelState State
        {
            get
            {
                return GetConnectivityState(false);
            }
        }

        // cached handler for watch connectivity state
        static readonly BatchCompletionDelegate WatchConnectivityStateHandler = (success, ctx, state) =>
        {
            var tcs = (TaskCompletionSource<bool>) state;
            tcs.SetResult(success);
        };

        /// <summary>
        /// Returned tasks completes once channel state has become different from 
        /// given lastObservedState. 
        /// If deadline is reached or an error occurs, returned task is cancelled.
        /// </summary>
        public async Task WaitForStateChangedAsync(ChannelState lastObservedState, DateTime? deadline = null)
        {
            var result = await TryWaitForStateChangedAsync(lastObservedState, deadline).ConfigureAwait(false);
            if (!result)
            {
                throw new TaskCanceledException("Reached deadline.");
            }
        }

        /// <summary>
        /// Returned tasks completes once channel state has become different from
        /// given lastObservedState (<c>true</c> is returned) or if the wait has timed out (<c>false</c> is returned).
        /// </summary>
        public Task<bool> TryWaitForStateChangedAsync(ChannelState lastObservedState, DateTime? deadline = null)
        {
            GrpcPreconditions.CheckArgument(lastObservedState != ChannelState.Shutdown,
                "Shutdown is a terminal state. No further state changes can occur.");
            var tcs = new TaskCompletionSource<bool>();
            var deadlineTimespec = deadline.HasValue ? Timespec.FromDateTime(deadline.Value) : Timespec.InfFuture;
            lock (myLock)
            {
                if (handle.IsClosed)
                {
                    // If channel has been already shutdown and handle was disposed, we would end up with
                    // an abandoned completion added to the completion registry. Instead, we make sure we fail early.
                    throw new ObjectDisposedException(nameof(handle), "Channel handle has already been disposed.");
                }
                else
                {
                    // pass "tcs" as "state" for WatchConnectivityStateHandler.
                    handle.WatchConnectivityState(lastObservedState, deadlineTimespec, completionQueue, WatchConnectivityStateHandler, tcs);
                }
            }
            return tcs.Task;
        }

        /// <summary>Resolved address of the remote endpoint in URI format.</summary>
        public string ResolvedTarget
        {
            get
            {
                return handle.GetTarget();
            }
        }

        /// <summary>
        /// Returns a token that gets cancelled once <c>ShutdownAsync</c> is invoked.
        /// </summary>
        public CancellationToken ShutdownToken
        {
            get
            {
                return this.shutdownTokenSource.Token;
            }
        }

        /// <summary>
        /// Allows explicitly requesting channel to connect without starting an RPC.
        /// Returned task completes once state Ready was seen. If the deadline is reached,
        /// or channel enters the Shutdown state, the task is cancelled.
        /// There is no need to call this explicitly unless your use case requires that.
        /// Starting an RPC on a new channel will request connection implicitly.
        /// </summary>
        /// <param name="deadline">The deadline. <c>null</c> indicates no deadline.</param>
        public async Task ConnectAsync(DateTime? deadline = null)
        {
            var currentState = GetConnectivityState(true);
            while (currentState != ChannelState.Ready)
            {
                if (currentState == ChannelState.Shutdown)
                {
                    throw new OperationCanceledException("Channel has reached Shutdown state.");
                }
                await WaitForStateChangedAsync(currentState, deadline).ConfigureAwait(false);
                currentState = GetConnectivityState(false);
            }
        }

        /// <summary>Provides implementation of a non-virtual public member.</summary>
        protected override async Task ShutdownAsyncCore()
        {
            lock (myLock)
            {
                GrpcPreconditions.CheckState(!shutdownRequested);
                shutdownRequested = true;
            }
            GrpcEnvironment.UnregisterChannel(this);

            shutdownTokenSource.Cancel();

            var activeCallCount = activeCallCounter.Count;
            if (activeCallCount > 0)
            {
                Logger.Warning("Channel shutdown was called but there are still {0} active calls for that channel.", activeCallCount);
            }

            lock (myLock)
            {
                handle.Dispose();
            }

            await GrpcEnvironment.ReleaseAsync().ConfigureAwait(false);
        }

        /// <summary>
        /// Create a new <see cref="CallInvoker"/> for the channel.
        /// </summary>
        /// <returns>A new <see cref="CallInvoker"/>.</returns>
        public override CallInvoker CreateCallInvoker()
        {
            return new DefaultCallInvoker(this);
        }

        internal ChannelSafeHandle Handle
        {
            get
            {
                return this.handle;
            }
        }

        internal GrpcEnvironment Environment
        {
            get
            {
                return this.environment;
            }
        }

        internal CompletionQueueSafeHandle CompletionQueue
        {
            get
            {
                return this.completionQueue;
            }
        }

        internal void AddCallReference(object call)
        {
            activeCallCounter.Increment();

            bool success = false;
            handle.DangerousAddRef(ref success);
            GrpcPreconditions.CheckState(success);
        }

        internal void RemoveCallReference(object call)
        {
            handle.DangerousRelease();

            activeCallCounter.Decrement();
        }

        // for testing only
        internal long GetCallReferenceCount()
        {
            return activeCallCounter.Count;
        }

        private ChannelState GetConnectivityState(bool tryToConnect)
        {
            try
            {
                lock (myLock)
                {
                    return handle.CheckConnectivityState(tryToConnect);
                }
            }
            catch (ObjectDisposedException)
            {
                return ChannelState.Shutdown;
            }
        }

        private static void EnsureUserAgentChannelOption(Dictionary<string, ChannelOption> options)
        {
            var key = ChannelOptions.PrimaryUserAgentString;
            var userAgentString = "";

            ChannelOption option;
            if (options.TryGetValue(key, out option))
            {
                // user-provided userAgentString needs to be at the beginning
                userAgentString = option.StringValue + " ";
            };

            userAgentString += UserAgentStringProvider.DefaultInstance.GrpcCsharpUserAgentString;

            options[ChannelOptions.PrimaryUserAgentString] = new ChannelOption(key, userAgentString);
        }

        private static Dictionary<string, ChannelOption> CreateOptionsDictionary(IEnumerable<ChannelOption> options)
        {
            var dict = new Dictionary<string, ChannelOption>();
            if (options == null)
            {
                return dict;
            }
            foreach (var option in options)
            {
                dict.Add(option.Name, option);
            }
            return dict;
        }
    }
}
