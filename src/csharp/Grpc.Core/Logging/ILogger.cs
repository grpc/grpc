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
using System.Collections.Generic;

namespace Grpc.Core.Logging
{
    /// <summary>For logging messages.</summary>
    public interface ILogger
    {
        /// <summary>Returns a logger associated with the specified type.</summary>
        ILogger ForType<T>();

        /// <summary>Logs a message with severity Debug.</summary>
        void Debug(string message);

        /// <summary>Logs a formatted message with severity Debug.</summary>
        void Debug(string format, params object[] formatArgs);

        /// <summary>Logs a message with severity Info.</summary>
        void Info(string message);

        /// <summary>Logs a formatted message with severity Info.</summary>
        void Info(string format, params object[] formatArgs);

        /// <summary>Logs a message with severity Warning.</summary>
        void Warning(string message);

        /// <summary>Logs a formatted message with severity Warning.</summary>
        void Warning(string format, params object[] formatArgs);

        /// <summary>Logs a message and an associated exception with severity Warning.</summary>
        void Warning(Exception exception, string message);

        /// <summary>Logs a message with severity Error.</summary>
        void Error(string message);

        /// <summary>Logs a formatted message with severity Error.</summary>
        void Error(string format, params object[] formatArgs);

        /// <summary>Logs a message and an associated exception with severity Error.</summary>
        void Error(Exception exception, string message);
    }
}
