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
    /// <summary>Logger that logs to System.Console.</summary>
    public class ConsoleLogger : ILogger
    {
        readonly Type forType;
        readonly string forTypeString;

        /// <summary>Creates a console logger not associated to any specific type.</summary>
        public ConsoleLogger() : this(null)
        {
        }

        /// <summary>Creates a console logger that logs messsage specific for given type.</summary>
        private ConsoleLogger(Type forType)
        {
            this.forType = forType;
            if (forType != null)
            {
                var namespaceStr = forType.Namespace ?? "";
                if (namespaceStr.Length > 0)
                {
                     namespaceStr += ".";
                }
                this.forTypeString = namespaceStr + forType.Name + " ";
            }
            else
            {
                this.forTypeString = "";
            }
        }
 
        /// <summary>
        /// Returns a logger associated with the specified type.
        /// </summary>
        public ILogger ForType<T>()
        {
            if (typeof(T) == forType)
            {
                return this;
            }
            return new ConsoleLogger(typeof(T));
        }

        /// <summary>Logs a message with severity Debug.</summary>
        public void Debug(string message, params object[] formatArgs)
        {
            Log("D", message, formatArgs);
        }

        /// <summary>Logs a message with severity Info.</summary>
        public void Info(string message, params object[] formatArgs)
        {
            Log("I", message, formatArgs);
        }

        /// <summary>Logs a message with severity Warning.</summary>
        public void Warning(string message, params object[] formatArgs)
        {
            Log("W", message, formatArgs);
        }

        /// <summary>Logs a message and an associated exception with severity Warning.</summary>
        public void Warning(Exception exception, string message, params object[] formatArgs)
        {
            Log("W", message + " " + exception, formatArgs);
        }

        /// <summary>Logs a message with severity Error.</summary>
        public void Error(string message, params object[] formatArgs)
        {
            Log("E", message, formatArgs);
        }

        /// <summary>Logs a message and an associated exception with severity Error.</summary>
        public void Error(Exception exception, string message, params object[] formatArgs)
        {
            Log("E", message + " " + exception, formatArgs);
        }

        private void Log(string severityString, string message, object[] formatArgs)
        {
            Console.Error.WriteLine("{0}{1} {2}{3}",
                severityString,
                DateTime.Now,
                forTypeString,
                string.Format(message, formatArgs));
        }
    }
}
