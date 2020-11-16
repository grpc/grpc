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
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Threading;
using Grpc.Core.Utils;

namespace Grpc.Core.Internal
{
    /// <summary>
    /// Utility methods for detecting platform and architecture.
    /// </summary>
    internal static class PlatformApis
    {
        const string UnityEngineAssemblyName = "UnityEngine";

        const string UnityEngineApplicationClassName = "UnityEngine.Application";

        const string UnityIPhonePlayer = "IPhonePlayer";
        const string XamarinAndroidObjectClassName = "Java.Lang.Object, Mono.Android";
        const string XamarinIOSObjectClassName = "Foundation.NSObject, Xamarin.iOS";

        static readonly bool isLinux;
        static readonly bool isMacOSX;
        static readonly bool isWindows;
        static readonly bool isMono;
        static readonly bool isNetCore;
        static readonly string unityApplicationPlatform;
        static readonly bool isXamarin;
        static readonly bool isXamarinIOS;
        static readonly bool isXamarinAndroid;

        static PlatformApis()
        {
#if NETSTANDARD
            isLinux = RuntimeInformation.IsOSPlatform(OSPlatform.Linux);
            isMacOSX = RuntimeInformation.IsOSPlatform(OSPlatform.OSX);
            isWindows = RuntimeInformation.IsOSPlatform(OSPlatform.Windows);
            isNetCore =
#if NETSTANDARD2_0
                Environment.Version.Major >= 5 ||
#endif
                RuntimeInformation.FrameworkDescription.StartsWith(".NET Core");
#else
            var platform = Environment.OSVersion.Platform;

            // PlatformID.MacOSX is never returned, commonly used trick is to identify Mac is by using uname.
            isMacOSX = (platform == PlatformID.Unix && GetUname() == "Darwin");
            isLinux = (platform == PlatformID.Unix && !isMacOSX);
            isWindows = (platform == PlatformID.Win32NT || platform == PlatformID.Win32S || platform == PlatformID.Win32Windows);
            isNetCore = false;
#endif
            isMono = Type.GetType("Mono.Runtime") != null;

            // Unity
            unityApplicationPlatform = TryGetUnityApplicationPlatform();

            // Xamarin
            isXamarinIOS = Type.GetType(XamarinIOSObjectClassName) != null;
            isXamarinAndroid = Type.GetType(XamarinAndroidObjectClassName) != null;
            isXamarin = isXamarinIOS || isXamarinAndroid;
        }

        public static bool IsLinux => isLinux;

        public static bool IsMacOSX => isMacOSX;

        public static bool IsWindows => isWindows;

        public static bool IsMono => isMono;

        /// <summary>
        /// true if running on Unity platform.
        /// </summary>
        public static bool IsUnity => unityApplicationPlatform != null;

        /// <summary>
        /// true if running on Unity iOS, false otherwise.
        /// </summary>
        public static bool IsUnityIOS => unityApplicationPlatform == UnityIPhonePlayer;

        /// <summary>
        /// true if running on a Xamarin platform (either Xamarin.Android or Xamarin.iOS),
        /// false otherwise.
        /// </summary>
        public static bool IsXamarin => isXamarin;

        /// <summary>
        /// true if running on Xamarin.iOS, false otherwise.
        /// </summary>
        public static bool IsXamarinIOS => isXamarinIOS;

        /// <summary>
        /// true if running on Xamarin.Android, false otherwise.
        /// </summary>
        public static bool IsXamarinAndroid => isXamarinAndroid;

        /// <summary>
        /// true if running on .NET Core (CoreCLR), false otherwise.
        /// </summary>
        public static bool IsNetCore => isNetCore;

        public static bool Is64Bit => IntPtr.Size == 8;

        /// <summary>
        /// Returns <c>UnityEngine.Application.platform</c> as a string.
        /// See https://docs.unity3d.com/ScriptReference/Application-platform.html for possible values.
        /// Value is obtained via reflection to avoid compile-time dependency on Unity.
        /// This method should only be called if <c>IsUnity</c> is <c>true</c>.
        /// </summary>
        public static string GetUnityApplicationPlatform()
        {
            GrpcPreconditions.CheckState(IsUnity, "Not running on Unity.");
            return unityApplicationPlatform;
        }

        /// <summary>
        /// Returns <c>UnityEngine.Application.platform</c> as a string or <c>null</c>
        /// if not running on Unity.
        /// Value is obtained via reflection to avoid compile-time dependency on Unity.
        /// </summary>
        static string TryGetUnityApplicationPlatform()
        {
            Assembly unityAssembly = null;
#if !NETSTANDARD1_5
            // On netstandard1.5, AppDomain is not available and we just short-circuit the logic there.
            // This is fine because only the net45 or netstandard2.0 version Grpc.Core assembly is going to used in Unity.
            // NOTE: Instead of trying to load the UnityEngine.Application class via <c>Type.GetType()</c>
            // we are using a more sneaky approach to avoid inadvertently loading the UnityEngine
            // assembly (that might be available even when we are not actually on Unity, resulting
            // in false positive). See https://github.com/grpc/grpc/issues/18801
            unityAssembly = AppDomain.CurrentDomain.GetAssemblies().FirstOrDefault(assembly => assembly.GetName().Name == UnityEngineAssemblyName);
#endif
            var applicationClass = unityAssembly?.GetType(UnityEngineApplicationClassName);
            var platformProperty = applicationClass?.GetTypeInfo().GetProperty("platform", BindingFlags.Static | BindingFlags.Public);
            try
            {
                // Consult value of Application.platform via reflection
                // https://docs.unity3d.com/ScriptReference/Application-platform.html
                return platformProperty?.GetValue(null)?.ToString();
            }
            catch (TargetInvocationException)
            {
                // The getter for Application.platform is defined as "extern", so if UnityEngine assembly is loaded outside of a Unity application,
                // the definition for the getter will be missing - note that this is a sneaky trick that helps us tell a real Unity application from a non-unity
                // application which just happens to have loaded the UnityEngine.dll assembly.
                // https://github.com/Unity-Technologies/UnityCsReference/blob/61f92bd79ae862c4465d35270f9d1d57befd1761/Runtime/Export/Application/Application.bindings.cs#L375
                // See https://github.com/grpc/grpc/issues/23334

                // If TargetInvocationException was thrown, it most likely means that the method definition for the extern method is missing,
                // and we are going to interpret this as "not running on Unity".
                return null;
            }
        }

        [DllImport("libc")]
        static extern int uname(IntPtr buf);

        static string GetUname()
        {
            var buffer = Marshal.AllocHGlobal(8192);
            try
            {
                if (uname(buffer) == 0)
                {
                    return Marshal.PtrToStringAnsi(buffer);
                }
                return string.Empty;
            }
            catch
            {
                return string.Empty;
            }
            finally
            {
                if (buffer != IntPtr.Zero)
                {
                    Marshal.FreeHGlobal(buffer);
                }
            }
        }
    }
}
