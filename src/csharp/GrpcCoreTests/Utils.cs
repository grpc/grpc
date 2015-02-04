using System;
using System.Net;
using System.Net.Sockets;

namespace Google.GRPC.Core.Tests
{
    /// <summary>
    /// Testing utils.
    /// </summary>
    public class Utils
    {
        static Random random = new Random();
        // TODO: cleanup this code a bit
        public static int PickUnusedPort()
        {
            int port;
            do
            {
                port = random.Next(2000, 50000);

            } while(!IsPortAvailable(port));
            return port;
        }
        // TODO: cleanup this code a bit
        public static bool IsPortAvailable(int port)
        {
            bool available = true;

            TcpListener server = null;
            try
            {
                IPAddress ipAddress = Dns.GetHostEntry("localhost").AddressList[0];
                server = new TcpListener(ipAddress, port);
                server.Start();
            }
            catch (Exception ex)
            {
                available = false;
            }
            finally
            {
                if (server != null)
                {
                    server.Stop();
                }
            }
            return available;
        }
    }
}

