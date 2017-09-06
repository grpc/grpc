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
    internal delegate void NativeMetadataInterceptor(IntPtr statePtr, IntPtr serviceUrlPtr, IntPtr methodNamePtr, IntPtr callbackPtr, IntPtr userDataPtr, bool isDestroy);

    internal class NativeMetadataCredentialsPlugin
    {
        const string GetMetadataExceptionMsg = "Exception occured in metadata credentials plugin.";
        static readonly ILogger Logger = GrpcEnvironment.Logger.ForType<NativeMetadataCredentialsPlugin>();
        static readonly NativeMethods Native = NativeMethods.Get();

        AsyncAuthInterceptor interceptor;
        GCHandle gcHandle;
        NativeMetadataInterceptor nativeInterceptor;
        CallCredentialsSafeHandle credentials;

        public NativeMetadataCredentialsPlugin(AsyncAuthInterceptor interceptor)
        {
            this.interceptor = GrpcPreconditions.CheckNotNull(interceptor, "interceptor");
            this.nativeInterceptor = NativeMetadataInterceptorHandler;

            // Make sure the callback doesn't get garbage collected until it is destroyed.
            this.gcHandle = GCHandle.Alloc(this.nativeInterceptor, GCHandleType.Normal);
            this.credentials = Native.grpcsharp_metadata_credentials_create_from_plugin(nativeInterceptor);
        }

        public CallCredentialsSafeHandle Credentials
        {
            get { return credentials; }
        }

        private void NativeMetadataInterceptorHandler(IntPtr statePtr, IntPtr serviceUrlPtr, IntPtr methodNamePtr, IntPtr callbackPtr, IntPtr userDataPtr, bool isDestroy)
        {
            if (isDestroy)
            {
                gcHandle.Free();
                return;
            }

            try
            {
                var context = new AuthInterceptorContext(Marshal.PtrToStringAnsi(serviceUrlPtr),
                                                         Marshal.PtrToStringAnsi(methodNamePtr));
                // Don't await, we are in a native callback and need to return.
                #pragma warning disable 4014
                GetMetadataAsync(context, callbackPtr, userDataPtr);
                #pragma warning restore 4014
            }
            catch (Exception e)
            {
                Native.grpcsharp_metadata_credentials_notify_from_plugin(callbackPtr, userDataPtr, MetadataArraySafeHandle.Create(Metadata.Empty), StatusCode.Unknown, GetMetadataExceptionMsg);
                Logger.Error(e, GetMetadataExceptionMsg);
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
                Native.grpcsharp_metadata_credentials_notify_from_plugin(callbackPtr, userDataPtr, MetadataArraySafeHandle.Create(Metadata.Empty), StatusCode.Unknown, GetMetadataExceptionMsg);
                Logger.Error(e, GetMetadataExceptionMsg);
            }
        }
    }
}
