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
using System.Collections.Concurrent;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading;

namespace Grpc.Core.Internal
{
    internal delegate void GprLogDelegate(IntPtr fileStringPtr, int line, ulong threadId, IntPtr severityStringPtr, IntPtr msgPtr);

    /// <summary>
    /// Logs from gRPC C core library can get lost if your application is not a console app.
    /// This class allows redirection of logs to gRPC logger.
    /// </summary>
    internal static class NativeLogRedirector
    {
        static object staticLock = new object();
        static GprLogDelegate writeCallback;

        /// <summary>
        /// Redirects logs from native gRPC C core library to a general logger.
        /// </summary>
        public static void Redirect(NativeMethods native)
        {
            lock (staticLock)
            {
                if (writeCallback == null)
                {
                    writeCallback = new GprLogDelegate(HandleWrite);
                    native.grpcsharp_redirect_log(writeCallback);
                }
            }
        }

        [MonoPInvokeCallback(typeof(GprLogDelegate))]
        private static void HandleWrite(IntPtr fileStringPtr, int line, ulong threadId, IntPtr severityStringPtr, IntPtr msgPtr)
        {
            try
            {
                var logger = GrpcEnvironment.Logger;
                string severityString = Marshal.PtrToStringAnsi(severityStringPtr);
                string message = string.Format("{0} {1}:{2}: {3}",
                    threadId,
                    Marshal.PtrToStringAnsi(fileStringPtr), 
                    line, 
                    Marshal.PtrToStringAnsi(msgPtr));
                
                switch (severityString)
                {
                    case "D":
                        logger.Debug(message);
                        break;
                    case "I":
                        logger.Info(message);
                        break;
                    case "E":
                        logger.Error(message);
                        break;
                    default:
                        // severity not recognized, default to error.
                        logger.Error(message);
                        break;
                }
            }
            catch (Exception e)
            {
                Console.WriteLine("Caught exception in native callback " + e);
            }
        }
    }

    /// <summary>
    /// Use this attribute to mark methods that will be called back from P/Invoke calls.
    /// iOS (and probably other AOT platforms) needs to have delegates registered.
    /// Instead of depending on Xamarin.iOS for this, we can just create our own,
    /// the iOS runtime just checks for the type name.
    /// See: https://docs.microsoft.com/en-gb/xamarin/ios/internals/limitations#reverse-callbacks
    /// </summary>
    [AttributeUsage(AttributeTargets.Method)]
    internal sealed class MonoPInvokeCallbackAttribute : Attribute
    {
        public MonoPInvokeCallbackAttribute(Type type)
        {
            Type = type;
        }

        public Type Type { get; private set; }
    }
}
