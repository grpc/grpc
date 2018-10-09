namespace Grpc.Core
{
    /// <summary>
    /// Verification context for VerifyPeerCallback.
    /// Note: experimental API that can change or be removed without any prior notice.
    /// </summary>
    public class VerifyPeerContext
    {
        /// <summary>
        /// Initializes a new instance of the <see cref="T:Grpc.Core.VerifyPeerContext"/> class.
        /// </summary>
        /// <param name="targetHost">string containing the host name of the peer.</param>
        /// <param name="targetPem">string containing PEM encoded certificate of the peer.</param>
        internal VerifyPeerContext(string targetHost, string targetPem)
        {
            this.TargetHost = targetHost;
            this.TargetPem = targetPem;
        }

        /// <summary>
        /// String containing the host name of the peer.
        /// </summary>
        public string TargetHost { get; }

        /// <summary>
        /// string containing PEM encoded certificate of the peer.
        /// </summary>
        public string TargetPem { get; }
    }
}
