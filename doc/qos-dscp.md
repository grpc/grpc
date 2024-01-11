# Quality of Service (QoS) using Differentiated services

Differentiated services or DiffServ is a mechanism for classifying network traffic and providing quality of service on IP networks.
DiffServ uses dedicated fields in the IP header for packet classification purposes.
By marking outgoing packets using a Differentiated Services Code Point (DSCP) the network can prioritize accordingly.

The DSCP value on outgoing packets is controlled by the following channel argument:

* **GRPC_ARG_DSCP**
  * This channel argument accepts integer values 0 to 63. See [dscp-registry](https://www.iana.org/assignments/dscp-registry/dscp-registry.xhtml) for details.
  * Default value is to use system default, i.e. not set.
  * Only apply to POSIX systems.
