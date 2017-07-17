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
