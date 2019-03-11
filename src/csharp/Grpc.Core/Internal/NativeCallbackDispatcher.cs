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
using System.Collections.Concurrent;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading;
using System.Collections.Generic;
using Grpc.Core.Logging;

namespace Grpc.Core.Internal
{
    internal delegate int UniversalNativeCallback(IntPtr arg0, IntPtr arg1, IntPtr arg2, IntPtr arg3, IntPtr arg4, IntPtr arg5);

    internal delegate int NativeCallbackDispatcherCallback(IntPtr tag, IntPtr arg0, IntPtr arg1, IntPtr arg2, IntPtr arg3, IntPtr arg4, IntPtr arg5);

    internal class NativeCallbackDispatcher
    {
        static readonly ILogger Logger = GrpcEnvironment.Logger.ForType<NativeCallbackDispatcher>();
        static readonly object staticLock = new object();
        static readonly AtomicCounter atomicCounter = new AtomicCounter();
        static readonly ConcurrentDictionary<IntPtr, UniversalNativeCallback> registry = new ConcurrentDictionary<IntPtr, UniversalNativeCallback>();

        static NativeCallbackDispatcherCallback dispatcherCallback;

        public static void Init(NativeMethods native)
        {
            lock (staticLock)
            {
                if (dispatcherCallback == null)
                {
                    dispatcherCallback = new NativeCallbackDispatcherCallback(HandleDispatcherCallback);
                    native.grpcsharp_native_callback_dispatcher_init(dispatcherCallback);
                }
            }
        }

        public static NativeCallbackRegistration RegisterCallback(UniversalNativeCallback callback)
        {
            while (true)
            {
                // TODO: retries might not work well on 32-bit
                var tag = NextTag();
                if (registry.TryAdd(tag, callback))
                {
                    return new NativeCallbackRegistration(tag);
                }
            }
        }

        public static void UnregisterCallback(IntPtr tag)
        {
            registry.TryRemove(tag, out UniversalNativeCallback callback);
        }

        private static bool TryGetCallback(IntPtr tag, out UniversalNativeCallback callback)
        {
            return registry.TryGetValue(tag, out callback);
        }

        private static IntPtr NextTag()
        {
            return (IntPtr) atomicCounter.Increment();
        }

        [MonoPInvokeCallback(typeof(NativeCallbackDispatcherCallback))]
        private static int HandleDispatcherCallback(IntPtr tag, IntPtr arg0, IntPtr arg1, IntPtr arg2, IntPtr arg3, IntPtr arg4, IntPtr arg5)
        {
            try
            {
                UniversalNativeCallback callback;
                if (!TryGetCallback(tag, out callback))
                {
                    Logger.Error("No native callback handler registered for tag {0}.", tag);
                    return 0;
                }
                return callback(arg0, arg1, arg2, arg3, arg4, arg5);
            }
            catch (Exception e)
            {
                // eat the exception, we must not throw when inside callback from native code.
                Logger.Error(e, "Caught exception inside callback from native callback.");
                return 0;
            }
        }
    }

    internal class NativeCallbackRegistration : IDisposable
    {
        readonly IntPtr tag;

        public NativeCallbackRegistration(IntPtr tag)
        {
            this.tag = tag;
        }

        public IntPtr Tag => tag;

        public void Dispose()
        {
            NativeCallbackDispatcher.UnregisterCallback(tag);
        }
    }
}
