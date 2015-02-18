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
using Google.GRPC.Core;

namespace Google.GRPC.Core.Internal
{
    /// <summary>
    /// grpc_event from grpc/grpc.h
    /// </summary>
    internal class EventSafeHandle : SafeHandleZeroIsInvalid
    {
        [DllImport("grpc_csharp_ext.dll")]
        static extern void grpcsharp_event_finish(IntPtr ev);

        [DllImport("grpc_csharp_ext.dll")]
        static extern GRPCCompletionType grpcsharp_event_type(EventSafeHandle ev);

        [DllImport("grpc_csharp_ext.dll")]
        static extern CallSafeHandle grpcsharp_event_call(EventSafeHandle ev);

        [DllImport("grpc_csharp_ext.dll")]
        static extern GRPCOpError grpcsharp_event_write_accepted(EventSafeHandle ev);

        [DllImport("grpc_csharp_ext.dll")]
        static extern GRPCOpError grpcsharp_event_finish_accepted(EventSafeHandle ev);

        [DllImport("grpc_csharp_ext.dll")]
        static extern StatusCode grpcsharp_event_finished_status(EventSafeHandle ev);

        [DllImport("grpc_csharp_ext.dll")]
        static extern IntPtr grpcsharp_event_finished_details(EventSafeHandle ev);  // returns const char*

        [DllImport("grpc_csharp_ext.dll")]
        static extern IntPtr grpcsharp_event_read_length(EventSafeHandle ev);

        [DllImport("grpc_csharp_ext.dll")]
        static extern void grpcsharp_event_read_copy_to_buffer(EventSafeHandle ev, byte[] buffer, UIntPtr bufferLen);

        [DllImport("grpc_csharp_ext.dll")]
        static extern IntPtr grpcsharp_event_server_rpc_new_method(EventSafeHandle ev); // returns const char*

        public GRPCCompletionType GetCompletionType()
        {
            return grpcsharp_event_type(this);
        }

        public GRPCOpError GetWriteAccepted()
        {
            return grpcsharp_event_write_accepted(this);
        }

        public GRPCOpError GetFinishAccepted()
        {
            return grpcsharp_event_finish_accepted(this);
        }

        public Status GetFinished()
        {
            // TODO: can the native method return string directly?
            string details = Marshal.PtrToStringAnsi(grpcsharp_event_finished_details(this));
            return new Status(grpcsharp_event_finished_status(this), details);
        }

        public byte[] GetReadData()
        {
            IntPtr len = grpcsharp_event_read_length(this);
            if (len == new IntPtr(-1))
            {
                return null;
            }
            byte[] data = new byte[(int) len];
            grpcsharp_event_read_copy_to_buffer(this, data, new UIntPtr((ulong)data.Length));
            return data;
        }

        public CallSafeHandle GetCall() {
            return grpcsharp_event_call(this);
        }

        public string GetServerRpcNewMethod() {
            // TODO: can the native method return string directly?
            return Marshal.PtrToStringAnsi(grpcsharp_event_server_rpc_new_method(this));
        }

        //TODO: client_metadata_read event type

        protected override bool ReleaseHandle()
        {
            grpcsharp_event_finish(handle);
            return true;
        }
    }

    // TODO: this is basically c&p of EventSafeHandle. Unify!
    /// <summary>
    /// Not owned version of 
    /// grpc_event from grpc/grpc.h
    /// </summary>
    internal class EventSafeHandleNotOwned : SafeHandleZeroIsInvalid
    {
        [DllImport("grpc_csharp_ext.dll")]
        static extern void grpcsharp_event_finish(IntPtr ev);

        [DllImport("grpc_csharp_ext.dll")]
        static extern GRPCCompletionType grpcsharp_event_type(EventSafeHandleNotOwned ev);

        [DllImport("grpc_csharp_ext.dll")]
        static extern CallSafeHandle grpcsharp_event_call(EventSafeHandleNotOwned ev);

        [DllImport("grpc_csharp_ext.dll")]
        static extern GRPCOpError grpcsharp_event_write_accepted(EventSafeHandleNotOwned ev);

        [DllImport("grpc_csharp_ext.dll")]
        static extern GRPCOpError grpcsharp_event_finish_accepted(EventSafeHandleNotOwned ev);

        [DllImport("grpc_csharp_ext.dll")]
        static extern StatusCode grpcsharp_event_finished_status(EventSafeHandleNotOwned ev);

        [DllImport("grpc_csharp_ext.dll")]
        static extern IntPtr grpcsharp_event_finished_details(EventSafeHandleNotOwned ev);  // returns const char*

        [DllImport("grpc_csharp_ext.dll")]
        static extern IntPtr grpcsharp_event_read_length(EventSafeHandleNotOwned ev);

        [DllImport("grpc_csharp_ext.dll")]
        static extern void grpcsharp_event_read_copy_to_buffer(EventSafeHandleNotOwned ev, byte[] buffer, UIntPtr bufferLen);

        [DllImport("grpc_csharp_ext.dll")]
        static extern IntPtr grpcsharp_event_server_rpc_new_method(EventSafeHandleNotOwned ev); // returns const char*

        public EventSafeHandleNotOwned() : base(false)
        {
        }

        public EventSafeHandleNotOwned(IntPtr handle) : base(false)
        {
            SetHandle(handle);
        }

        public GRPCCompletionType GetCompletionType()
        {
            return grpcsharp_event_type(this);
        }

        public GRPCOpError GetWriteAccepted()
        {
            return grpcsharp_event_write_accepted(this);
        }

        public GRPCOpError GetFinishAccepted()
        {
            return grpcsharp_event_finish_accepted(this);
        }

        public Status GetFinished()
        {
            // TODO: can the native method return string directly?
            string details = Marshal.PtrToStringAnsi(grpcsharp_event_finished_details(this));
            return new Status(grpcsharp_event_finished_status(this), details);
        }

        public byte[] GetReadData()
        {
            IntPtr len = grpcsharp_event_read_length(this);
            if (len == new IntPtr(-1))
            {
                return null;
            }
            byte[] data = new byte[(int) len];
            grpcsharp_event_read_copy_to_buffer(this, data, new UIntPtr((ulong)data.Length));
            return data;
        }

        public CallSafeHandle GetCall() {
            return grpcsharp_event_call(this);
        }

        public string GetServerRpcNewMethod() {
            // TODO: can the native method return string directly?
            return Marshal.PtrToStringAnsi(grpcsharp_event_server_rpc_new_method(this));
        }

        //TODO: client_metadata_read event type

        protected override bool ReleaseHandle()
        {
            grpcsharp_event_finish(handle);
            return true;
        }
    }
}