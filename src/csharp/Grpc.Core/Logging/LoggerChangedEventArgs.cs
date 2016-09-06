using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace Grpc.Core.Logging
{
    internal class LoggerChangedEventArgs : EventArgs
    {
        public readonly ILogger newLogger; 

        public LoggerChangedEventArgs(ILogger newLogger)
        {
            this.newLogger = newLogger;
        }
    }
}
