using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace Grpc.Core.Logging
{
    internal class GlobalLoggerProxy<T>
    {
        private ILogger logger;

        internal GlobalLoggerProxy()
        {
            this.logger = GrpcEnvironment.Logger.ForType<T>();
            GrpcEnvironment.LoggerChangedEvent += logger_changed_event_handler;
        }

        internal ILogger GetLogger()
        {
            return logger;
        }

        private void logger_changed_event_handler(object sender, LoggerChangedEventArgs args)
        {
            this.logger = args.newLogger.ForType<T>();
        }
    }
}
