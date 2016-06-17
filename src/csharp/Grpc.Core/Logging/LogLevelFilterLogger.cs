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
using System.Globalization;
using System.IO;
using Grpc.Core.Utils;

namespace Grpc.Core.Logging
{
    /// <summary>Logger that filters out messages below certain log level.</summary>
    public class LogLevelFilterLogger : ILogger
    {
        readonly ILogger innerLogger;
        readonly LogLevel logLevel;

        /// <summary>
        /// Creates and instance of <c>LogLevelFilter.</c>
        /// </summary>
        public LogLevelFilterLogger(ILogger logger, LogLevel logLevel)
        {
            this.innerLogger = GrpcPreconditions.CheckNotNull(logger);
            this.logLevel = logLevel;
        }

        /// <summary>
        /// Returns a logger associated with the specified type.
        /// </summary>
        public virtual ILogger ForType<T>()
        {
            var newInnerLogger = innerLogger.ForType<T>();
            if (object.ReferenceEquals(this.innerLogger, newInnerLogger))
            {
                return this;
            }
            return new LogLevelFilterLogger(newInnerLogger, logLevel);
        }

        /// <summary>Logs a message with severity Debug.</summary>
        public void Debug(string message)
        {
            if (logLevel <= LogLevel.Debug)
            {
                innerLogger.Debug(message);
            }
        }

        /// <summary>Logs a formatted message with severity Debug.</summary>
        public void Debug(string format, params object[] formatArgs)
        {
            if (logLevel <= LogLevel.Debug)
            {
                innerLogger.Debug(format, formatArgs);
            }
        }

        /// <summary>Logs a message with severity Info.</summary>
        public void Info(string message)
        {
            if (logLevel <= LogLevel.Info)
            {
                innerLogger.Info(message);
            }
        }

        /// <summary>Logs a formatted message with severity Info.</summary>
        public void Info(string format, params object[] formatArgs)
        {
            if (logLevel <= LogLevel.Info)
            {
                innerLogger.Info(format, formatArgs);
            }
        }

        /// <summary>Logs a message with severity Warning.</summary>
        public void Warning(string message)
        {
            if (logLevel <= LogLevel.Warning)
            {
                innerLogger.Warning(message);
            }
        }

        /// <summary>Logs a formatted message with severity Warning.</summary>
        public void Warning(string format, params object[] formatArgs)
        {
            if (logLevel <= LogLevel.Warning)
            {
                innerLogger.Warning(format, formatArgs);
            }
        }

        /// <summary>Logs a message and an associated exception with severity Warning.</summary>
        public void Warning(Exception exception, string message)
        {
            if (logLevel <= LogLevel.Warning)
            {
                innerLogger.Warning(exception, message);
            }
        }

        /// <summary>Logs a message with severity Error.</summary>
        public void Error(string message)
        {
            if (logLevel <= LogLevel.Error)
            {
                innerLogger.Error(message);
            }
        }

        /// <summary>Logs a formatted message with severity Error.</summary>
        public void Error(string format, params object[] formatArgs)
        {
            if (logLevel <= LogLevel.Error)
            {
                innerLogger.Error(format, formatArgs);
            }
        }

        /// <summary>Logs a message and an associated exception with severity Error.</summary>
        public void Error(Exception exception, string message)
        {
            if (logLevel <= LogLevel.Error)
            {
                innerLogger.Error(exception, message);
            }
        }
    }
}
