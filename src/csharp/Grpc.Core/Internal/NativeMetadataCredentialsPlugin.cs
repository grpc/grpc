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
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;

using Grpc.Core.Logging;
using Grpc.Core.Utils;

namespace Grpc.Core.Internal
{
    internal class NativeMetadataCredentialsPlugin
    {
        const string GetMetadataExceptionStatusMsg = "Exception occurred in metadata credentials plugin.";
        const string GetMetadataExceptionLogMsg = GetMetadataExceptionStatusMsg + " This is likely not a problem with gRPC itself. Please verify that the code supplying the metadata (usually an authentication token) works correctly.";
        static readonly ILogger Logger = GrpcEnvironment.Logger.ForType<NativeMetadataCredentialsPlugin>();
        static readonly NativeMethods Native = NativeMethods.Get();

        AsyncAuthInterceptor interceptor;
        CallCredentialsSafeHandle credentials;
        NativeCallbackRegistration callbackRegistration;

        public NativeMetadataCredentialsPlugin(AsyncAuthInterceptor interceptor)
        {
            this.interceptor = GrpcPreconditions.CheckNotNull(interceptor, "interceptor");
            this.callbackRegistration = NativeCallbackDispatcher.RegisterCallback(HandleUniversalCallback);
            this.credentials = Native.grpcsharp_metadata_credentials_create_from_plugin(this.callbackRegistration.Tag);
        }

        public CallCredentialsSafeHandle Credentials
        {
            get { return credentials; }
        }

        private int HandleUniversalCallback(IntPtr arg0, IntPtr arg1, IntPtr arg2, IntPtr arg3, IntPtr arg4, IntPtr arg5)
        {
            NativeMetadataInterceptorHandler(arg0, arg1, arg2, arg3, arg4 != IntPtr.Zero);
            return 0;
        }

        private void NativeMetadataInterceptorHandler(IntPtr serviceUrlPtr, IntPtr methodNamePtr, IntPtr callbackPtr, IntPtr userDataPtr, bool isDestroy)
        {
            if (isDestroy)
            {
                this.callbackRegistration.Dispose();
                return;
            }

            try
            {
                // NOTE: The serviceUrlPtr and methodNamePtr values come from the grpc_auth_metadata_context
                // and are only guaranteed to be valid until synchronous return of this handler.
                // We are effectively making a copy of these strings by creating C# string from the native char pointers,
                // and passing the resulting C# strings to the context is therefore safe.
                var context = new AuthInterceptorContext(Marshal.PtrToStringAnsi(serviceUrlPtr), Marshal.PtrToStringAnsi(methodNamePtr));
                // Make a guarantee that credentials_notify_from_plugin is invoked async to be compliant with c-core API.
                ThreadPool.QueueUserWorkItem(async (stateInfo) => await GetMetadataAsync(context, callbackPtr, userDataPtr));
            }
            catch (Exception e)
            {
                // eat the exception, we must not throw when inside callback from native code.
                Logger.Error(e, "Exception occurred while invoking native metadata interceptor handler.");
            }
        }

        private async Task GetMetadataAsync(AuthInterceptorContext context, IntPtr callbackPtr, IntPtr userDataPtr)
        {
            try
            {
                var metadata = new Metadata();
                await interceptor(context, metadata).ConfigureAwait(false);

                using (var metadataArray = MetadataArraySafeHandle.Create(metadata))
                {
                    Native.grpcsharp_metadata_credentials_notify_from_plugin(callbackPtr, userDataPtr, metadataArray, StatusCode.OK, null);
                }
            }
            catch (Exception e)
            {
                string detail = GetMetadataExceptionStatusMsg + " " + e.ToString();
                Native.grpcsharp_metadata_credentials_notify_from_plugin(callbackPtr, userDataPtr, MetadataArraySafeHandle.Create(Metadata.Empty), StatusCode.Unknown, detail);
                Logger.Error(e, GetMetadataExceptionLogMsg);
            }
        }
    }
}
