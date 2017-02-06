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
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Threading;

using Grpc.Core.Logging;
using Grpc.Core.Utils;

namespace Grpc.Core.Internal
{
    /// <summary>
    /// Represents a dynamically loaded unmanaged library in a (partially) platform independent manner.
    /// First, the native library is loaded using dlopen (on Unix systems) or using LoadLibrary (on Windows).
    /// dlsym or GetProcAddress are then used to obtain symbol addresses. <c>Marshal.GetDelegateForFunctionPointer</c>
    /// transforms the addresses into delegates to native methods.
    /// See http://stackoverflow.com/questions/13461989/p-invoke-to-dynamically-loaded-library-on-mono.
    /// </summary>
    internal class UnmanagedLibrary
    {
        static readonly ILogger Logger = GrpcEnvironment.Logger.ForType<UnmanagedLibrary>();

        // flags for dlopen
        const int RTLD_LAZY = 1;
        const int RTLD_GLOBAL = 8;

        readonly string libraryPath;
        readonly IntPtr handle;

        public UnmanagedLibrary(string[] libraryPathAlternatives)
        {
            this.libraryPath = FirstValidLibraryPath(libraryPathAlternatives);

            Logger.Debug("Attempting to load native library \"{0}\"", this.libraryPath);

            this.handle = PlatformSpecificLoadLibrary(this.libraryPath);

            if (this.handle == IntPtr.Zero)
            {
                throw new IOException(string.Format("Error loading native library \"{0}\"", this.libraryPath));
            }
        }

        /// <summary>
        /// Loads symbol in a platform specific way.
        /// </summary>
        /// <param name="symbolName"></param>
        /// <returns></returns>
        public IntPtr LoadSymbol(string symbolName)
        {
            if (PlatformApis.IsWindows)
            {
                // See http://stackoverflow.com/questions/10473310 for background on this.
                if (PlatformApis.Is64Bit)
                {
                    return Windows.GetProcAddress(this.handle, symbolName);
                }
                else
                {
                    // Yes, we could potentially predict the size... but it's a lot simpler to just try
                    // all the candidates. Most functions have a suffix of @0, @4 or @8 so we won't be trying
                    // many options - and if it takes a little bit longer to fail if we've really got the wrong
                    // library, that's not a big problem. This is only called once per function in the native library.
                    symbolName = "_" + symbolName + "@";
                    for (int stackSize = 0; stackSize < 128; stackSize += 4)
                    {
                        IntPtr candidate = Windows.GetProcAddress(this.handle, symbolName + stackSize);
                        if (candidate != IntPtr.Zero)
                        {
                            return candidate;
                        }
                    }
                    // Fail.
                    return IntPtr.Zero;
                }
            }
            if (PlatformApis.IsLinux)
            {
                if (PlatformApis.IsMono)
                {
                    return Mono.dlsym(this.handle, symbolName);
                }
                if (PlatformApis.IsNetCore)
                {
                    return CoreCLR.dlsym(this.handle, symbolName);
                }
                return Linux.dlsym(this.handle, symbolName);
            }
            if (PlatformApis.IsMacOSX)
            {
                return MacOSX.dlsym(this.handle, symbolName);
            }
            throw new InvalidOperationException("Unsupported platform.");
        }

        public T GetNativeMethodDelegate<T>(string methodName)
            where T : class
        {
            var ptr = LoadSymbol(methodName);
            if (ptr == IntPtr.Zero)
            {
                throw new MissingMethodException(string.Format("The native method \"{0}\" does not exist", methodName));
            }
#if NETSTANDARD1_5
            return Marshal.GetDelegateForFunctionPointer<T>(ptr);  // non-generic version is obsolete
#else
            return Marshal.GetDelegateForFunctionPointer(ptr, typeof(T)) as T;  // generic version not available in .NET45
#endif
        }

        /// <summary>
        /// Loads library in a platform specific way.
        /// </summary>
        private static IntPtr PlatformSpecificLoadLibrary(string libraryPath)
        {
            if (PlatformApis.IsWindows)
            {
                return Windows.LoadLibrary(libraryPath);
            }
            if (PlatformApis.IsLinux)
            {
                if (PlatformApis.IsMono)
                {
                    return Mono.dlopen(libraryPath, RTLD_GLOBAL + RTLD_LAZY);
                }
                if (PlatformApis.IsNetCore)
                {
                    return CoreCLR.dlopen(libraryPath, RTLD_GLOBAL + RTLD_LAZY);
                }
                return Linux.dlopen(libraryPath, RTLD_GLOBAL + RTLD_LAZY);
            }
            if (PlatformApis.IsMacOSX)
            {
                return MacOSX.dlopen(libraryPath, RTLD_GLOBAL + RTLD_LAZY);
            }
            throw new InvalidOperationException("Unsupported platform.");
        }

        private static string FirstValidLibraryPath(string[] libraryPathAlternatives)
        {
            GrpcPreconditions.CheckArgument(libraryPathAlternatives.Length > 0, "libraryPathAlternatives cannot be empty.");
            foreach (var path in libraryPathAlternatives)
            {
                if (File.Exists(path))
                {
                    return path;
                }
            }
            throw new FileNotFoundException(
                String.Format("Error loading native library. Not found in any of the possible locations: {0}", 
                    string.Join(",", libraryPathAlternatives)));
        }

        private static class Windows
        {
            [DllImport("kernel32.dll")]
            internal static extern IntPtr LoadLibrary(string filename);

            [DllImport("kernel32.dll")]
            internal static extern IntPtr GetProcAddress(IntPtr hModule, string procName);
        }

        private static class Linux
        {
            [DllImport("libdl.so")]
            internal static extern IntPtr dlopen(string filename, int flags);

            [DllImport("libdl.so")]
            internal static extern IntPtr dlsym(IntPtr handle, string symbol);
        }

        private static class MacOSX
        {
            [DllImport("libSystem.dylib")]
            internal static extern IntPtr dlopen(string filename, int flags);

            [DllImport("libSystem.dylib")]
            internal static extern IntPtr dlsym(IntPtr handle, string symbol);
        }

        /// <summary>
        /// On Linux systems, using using dlopen and dlsym results in
        /// DllNotFoundException("libdl.so not found") if libc6-dev
        /// is not installed. As a workaround, we load symbols for
        /// dlopen and dlsym from the current process as on Linux
        /// Mono sure is linked against these symbols.
        /// </summary>
        private static class Mono
        {
            [DllImport("__Internal")]
            internal static extern IntPtr dlopen(string filename, int flags);

            [DllImport("__Internal")]
            internal static extern IntPtr dlsym(IntPtr handle, string symbol);
        }

        /// <summary>
        /// Similarly as for Mono on Linux, we load symbols for
        /// dlopen and dlsym from the "libcoreclr.so",
        /// to avoid the dependency on libc-dev Linux.
        /// </summary>
        private static class CoreCLR
        {
            [DllImport("libcoreclr.so")]
            internal static extern IntPtr dlopen(string filename, int flags);

            [DllImport("libcoreclr.so")]
            internal static extern IntPtr dlsym(IntPtr handle, string symbol);
        }
    }
}
