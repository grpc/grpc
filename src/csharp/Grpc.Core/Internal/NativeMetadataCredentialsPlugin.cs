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
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;

using Grpc.Core.Logging;
using Grpc.Core.Utils;

namespace Grpc.Core.Internal
{
    internal delegate void NativeMetadataInterceptor(IntPtr statePtr, IntPtr serviceUrlPtr, IntPtr callbackPtr, IntPtr userDataPtr, bool isDestroy);

    internal class NativeMetadataCredentialsPlugin
    {
        const string GetMetadataExceptionMsg = "Exception occured in metadata credentials plugin.";
        static readonly ILogger Logger = GrpcEnvironment.Logger.ForType<NativeMetadataCredentialsPlugin>();

        [DllImport("grpc_csharp_ext.dll")]
        static extern CredentialsSafeHandle grpcsharp_metadata_credentials_create_from_plugin(NativeMetadataInterceptor interceptor);
        
        [DllImport("grpc_csharp_ext.dll", CharSet = CharSet.Ansi)]
        static extern void grpcsharp_metadata_credentials_notify_from_plugin(IntPtr callbackPtr, IntPtr userData, MetadataArraySafeHandle metadataArray, StatusCode statusCode, string errorDetails);

        AsyncAuthInterceptor interceptor;
        GCHandle gcHandle;
        NativeMetadataInterceptor nativeInterceptor;
        CredentialsSafeHandle credentials;

        public NativeMetadataCredentialsPlugin(AsyncAuthInterceptor interceptor)
        {
            this.interceptor = Preconditions.CheckNotNull(interceptor, "interceptor");
            this.nativeInterceptor = NativeMetadataInterceptorHandler;

            // Make sure the callback doesn't get garbage collected until it is destroyed.
            this.gcHandle = GCHandle.Alloc(this.nativeInterceptor, GCHandleType.Normal);
            this.credentials = grpcsharp_metadata_credentials_create_from_plugin(nativeInterceptor);
        }

        public CredentialsSafeHandle Credentials
        {
            get { return credentials; }
        }

        private void NativeMetadataInterceptorHandler(IntPtr statePtr, IntPtr serviceUrlPtr, IntPtr callbackPtr, IntPtr userDataPtr, bool isDestroy)
        {
            if (isDestroy)
            {
                gcHandle.Free();
                return;
            }

            try
            {
                string serviceUrl = Marshal.PtrToStringAnsi(serviceUrlPtr);
                StartGetMetadata(serviceUrl, callbackPtr, userDataPtr);
            }
            catch (Exception e)
            {
                grpcsharp_metadata_credentials_notify_from_plugin(callbackPtr, userDataPtr, MetadataArraySafeHandle.Create(Metadata.Empty), StatusCode.Unknown, GetMetadataExceptionMsg);
                Logger.Error(e, GetMetadataExceptionMsg);
            }
        }

        private async void StartGetMetadata(string serviceUrl, IntPtr callbackPtr, IntPtr userDataPtr)
        {
            try
            {
                var metadata = new Metadata();
                await interceptor(serviceUrl, metadata);

                using (var metadataArray = MetadataArraySafeHandle.Create(metadata))
                {
                    grpcsharp_metadata_credentials_notify_from_plugin(callbackPtr, userDataPtr, metadataArray, StatusCode.OK, null);
                }
            }
            catch (Exception e)
            {
                grpcsharp_metadata_credentials_notify_from_plugin(callbackPtr, userDataPtr, MetadataArraySafeHandle.Create(Metadata.Empty), StatusCode.Unknown, GetMetadataExceptionMsg);
                Logger.Error(e, GetMetadataExceptionMsg);
            }
        }
    }
}
