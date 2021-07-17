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
        static readonly bool isNet5OrHigher;
        static readonly bool isNetCore;
        static readonly string frameworkDescription;
        static readonly string clrVersion;
        static readonly string unityApplicationPlatform;
        static readonly bool isXamarin;
        static readonly bool isXamarinIOS;
        static readonly bool isXamarinAndroid;

        static PlatformApis()
        {
            // Detect OS
            var osKind = CommonPlatformDetection.GetOSKind();
            isLinux = osKind == CommonPlatformDetection.OSKind.Linux;
            isMacOSX = osKind == CommonPlatformDetection.OSKind.MacOSX;
            isWindows = osKind == CommonPlatformDetection.OSKind.Windows;

#if NETSTANDARD1_5
            // assume that on .NET 5+, the netstandard2.0 or newer TFM is always going to be selected
            // so for netstandard1.5 we assume we are never on .NET5+
            isNet5OrHigher = false;
            isNetCore = isNet5OrHigher || RuntimeInformation.FrameworkDescription.StartsWith(".NET Core");
#elif NETSTANDARD
            isNet5OrHigher = Environment.Version.Major >= 5;
            isNetCore = isNet5OrHigher || RuntimeInformation.FrameworkDescription.StartsWith(".NET Core");
#else
            isNet5OrHigher = false;
            isNetCore = false;
#endif
            frameworkDescription = TryGetFrameworkDescription();
            clrVersion = TryGetClrVersion();

            // Detect mono runtime
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
        /// true if running on .NET 5+, false otherwise.
        /// </summary>
        public static bool IsNet5OrHigher => isNet5OrHigher;

        /// <summary>
        /// Contains <c>RuntimeInformation.FrameworkDescription</c> if the property is available on current TFM.
        /// <c>null</c> otherwise.
        /// </summary>
        public static string FrameworkDescription => frameworkDescription;

        /// <summary>
        /// Contains the version of common language runtime obtained from <c>Environment.Version</c>
        /// if the property is available on current TFM. <c>null</c> otherwise.
        /// </summary>
        public static string ClrVersion => clrVersion;

        /// <summary>
        /// true if running on .NET Core (CoreCLR) or NET 5+, false otherwise.
        /// </summary>
        public static bool IsNetCore => isNetCore;

        public static bool Is64Bit => IntPtr.Size == 8;

        public static CommonPlatformDetection.CpuArchitecture ProcessArchitecture => CommonPlatformDetection.GetProcessArchitecture();

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

        /// <summary>
        /// Returns description of the framework this process is running on.
        /// Value is based on <c>RuntimeInformation.FrameworkDescription</c>.
        /// </summary>
        static string TryGetFrameworkDescription()
        {
#if NETSTANDARD
            return RuntimeInformation.FrameworkDescription;
#else
            // on full .NET framework we are targeting net45, and the property is only available starting from .NET Framework 4.7.1+
            // try obtaining the value by reflection since we might be running on a newer framework even though we're targeting
            // an older one.
            var runtimeInformationClass = Type.GetType("System.Runtime.InteropServices.RuntimeInformation");
            var frameworkDescriptionProperty = runtimeInformationClass?.GetTypeInfo().GetProperty("FrameworkDescription", BindingFlags.Static | BindingFlags.Public);
            return frameworkDescriptionProperty?.GetValue(null)?.ToString();
#endif
        }

        /// <summary>
        /// Returns version of the common language runtime this process is running on.
        /// Value is based on <c>Environment.Version</c>.
        /// </summary>
        static string TryGetClrVersion()
        {
#if NETSTANDARD1_5
            return null;
#else
            return Environment.Version.ToString();
#endif
        }

        /// <summary>
        /// Returns the TFM of the Grpc.Core assembly.
        /// </summary>
        public static string GetGrpcCoreTargetFrameworkMoniker()
        {
#if NETSTANDARD1_5
            return "netstandard1.5";
#elif NETSTANDARD2_0
            return "netstandard2.0";
#elif NET45
            return "net45";
#else
            // The TFM is determined at compile time.
            // The is intentionally no "default" return clause here so that
            // if the set of TFMs we build for changes and this method is not updated accordingly,
            // it will result in compilation error.
#endif
        }
    }
}
