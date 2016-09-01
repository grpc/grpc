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
}
