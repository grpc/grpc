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
        // Verbosity environment variable used by C core.
        private const string CoreVerbosityEnvVarName = "GRPC_VERBOSITY";
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
        /// Creates and instance of <c>LogLevelFilter.</c>
        /// The <c>fromEnvironmentVariable</c> parameter allows looking up "GRPC_VERBOSITY" setting provided by C-core
        /// and uses the same log level for C# logs. Using this setting is recommended as it can prevent unintentionally hiding
        /// C core logs requested by "GRPC_VERBOSITY" environment variable (which could happen if C# logger's log level was set to a more restrictive value).
        /// </summary>
        /// <param name="logger">the logger to forward filtered logs to.</param>
        /// <param name="defaultLogLevel">the default log level, unless overriden by env variable.</param>
        /// <param name="fromEnvironmentVariable">if <c>true</c>, override log level with setting from environment variable.</param>
        public LogLevelFilterLogger(ILogger logger, LogLevel defaultLogLevel, bool fromEnvironmentVariable) : this(logger, GetLogLevelFromEnvironment(defaultLogLevel, fromEnvironmentVariable))
        {
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

        /// <summary>Get log level based on a default and lookup of <c>GRPC_VERBOSITY</c> environment variable.</summary>
        private static LogLevel GetLogLevelFromEnvironment(LogLevel defaultLogLevel, bool fromEnvironmentVariable)
        {
            if (!fromEnvironmentVariable)
            {
                return defaultLogLevel;
            }

            var verbosityString = System.Environment.GetEnvironmentVariable(CoreVerbosityEnvVarName);
            if (verbosityString == null)
            {
                return defaultLogLevel;
            }

            // NOTE: C core doesn't have "WARNING" log level
            switch (verbosityString.ToUpperInvariant())
            {
                case "DEBUG":
                    return LogLevel.Debug;
                case "INFO":
                    return LogLevel.Info;
                case "ERROR":
                    return LogLevel.Error;
                default:
                    return defaultLogLevel;
            }
        }
    }
}
