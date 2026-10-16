# xDS Bootstrap File Format in gRPC

This document specifies the xDS bootstrap file format supported by gRPC.

## Background

gRPC expects the xDS bootstrap configuration to be specified as a JSON string.
The xDS bootstrap file location may be specified using the environment variable
`GRPC_XDS_BOOTSTRAP`.  Alternatively, the bootstrap file contents may be
specified using the environment variable `GRPC_XDS_BOOTSTRAP_CONFIG`.  If both
are specified, the former takes precedence.

The xDS client inside of gRPC parses the bootstrap configuration specified by
one of the above means when it is created to configure itself.

The following sections describe the bootstrap file format, including links to
gRFCs where support for appropriate fields was added.

## File Format

```
{
  // The xDS server to talk to.  The value is an ordered array of server
  // configurations, to support failing over to a secondary xDS server if the
  // primary is down.
  //
  // Prior to gRFC A71, all but the first entry was ignored.
  "xds_servers": [
    {

      // A target URI string suitable for creating a gRPC channel.
      "server_uri": <string containing the target URI of xds server>,

      // List of channel creds; client will stop at the first type it
      // supports.  This field is required and must contain at least one
      // channel creds type that the client supports.
      //
      // See section titled "Supported Channel Credentials".
      "channel_creds": [
        {
          "type": <string containing channel cred type>,

          // The "config" field is optional; it may be missing if the
          // credential type does not require config parameters.
          "config": <JSON object containing config for the type>
        }
      ],

      // A list of features supported by the server.  New values will
      // be added over time.  For forward compatibility reasons, the
      // client will ignore any entry in the list that it does not
      // understand, regardless of type.
      // 
      // See section titled "Supported Server Features".
      "server_features": [ ... ]
    }
  ],

  // Identifies a specific gRPC instance.
  "node": {

    // Opaque identifier for the gRPC instance.
    "id": <string>,

    // Identifier for the local service cluster where the gRPC instance is
    // running.
    "cluster": <string>,

    // Specifies where the gRPC instance is running.
    "locality": {
      "region": <string>,
      "zone": <string>,
      "sub_zone": <string>,
    },

    // Opaque metadata extending the node identifier.
    "metadata": <JSON Object>,
  }

  // Map of supported certificate providers, keyed by the provider instance
  // name.
  // See section titled "Supported certificate providers".
  "certificate_providers": {

    // Certificate provider instance name, specified by the
    // control plane, to fetch certificates from.
    "<instance_name>": {

      // Name of the plugin implementation.
      "plugin_name": <string containing plugin type>,

      // A JSON object containing the configuration for the plugin, whose schema
      // is defined by the plugin.  The "config" field is optional; it may be
      // missing if the credential type does not require config parameters.
      "config": <JSON object containing config for the type>
    }
  }

  // A template for the name of the Listener resource to subscribe to for a gRPC
  // server. If the token `%s` is present in the string, all instances of the
  // token will be replaced with the server's listening "IP:port" (e.g.,
  // "0.0.0.0:8080", "[::]:8080").
  "server_listener_resource_name_template": "example/resource/%s",

  // A template for the name of the Listener resource to subscribe to for a gRPC
  // client channel.  Used only when the channel is created with an "xds:" URI
  // with no authority.
  //
  // If starts with "xdstp:", will be interpreted as a new-style name, in which
  // case the authority of the URI will be used to select the relevant
  // configuration in the "authorities" map.
  //
  // The token "%s", if present in this string, will be replaced with the
  // service authority (i.e., the path part of the target URI used to create the
  // gRPC channel).  If the template starts with "xdstp:", the replaced string
  // will be percent-encoded.  In that case, the replacement string must include
  // only characters allowed in a URI path as per RFC-3986 section 3.3 (which
  // includes '/'), and all other characters must be percent-encoded.
  //
  // Defaults to "%s".
  "client_default_listener_resource_name_template": <string>,

  // A map of authority name to corresponding configuration.
  //
  // This is used in the following cases:
  // - A gRPC client channel is created using an "xds:" URI that includes
  //   an authority.
  // - A gRPC client channel is created using an "xds:" URI with no
  //   authority, but the "client_default_listener_resource_name_template"
  //   field turns it into an "xdstp:" URI.
  // - A gRPC server is created and the
  //   "server_listener_resource_name_template" field is an "xdstp:" URI.
  //
  // In any of those cases, it is an error if the specified authority is
  // not present in this map.
  "authorities": {
    // Entries are keyed by authority name.
    // Note: If a new-style resource name has no authority, we will use
    // the empty string here as the key.
    "<authority_name>": {

      // A template for the name of the Listener resource to subscribe
      // to for a gRPC client channel.  Used only when the channel is
      // created using an "xds:" URI with this authority name.
      //
      // The token "%s", if present in this string, will be replaced
      // with percent-encoded service authority (i.e., the path part of the
      // target URI used to create the gRPC channel).  The replacement string
      // must include only characters allowed in a URI path as per RFC-3986
      // section 3.3 (which includes '/'), and all other characters must be
      // percent-encoded.
      //
      // Must start with "xdstp://<authority_name>/".  If it does not,
      // that is considered a bootstrap file parsing error.
      //
      // If not present in the bootstrap file, defaults to
      // "xdstp://<authority_name>/envoy.config.listener.v3.Listener/%s".
      "client_listener_resource_name_template": <string>,

      // Ordered list of xDS servers to contact for this authority.
      // Format is exactly the same as the top level "xds_servers" field.
      //
      // If the same server is listed in multiple authorities, the
      // entries will be de-duped (i.e., resources for both authorities
      // will be fetched on the same ADS stream).
      //
      // If not specified, the top-level server list is used.
      "xds_servers": [ ... ]
    }
  }
}
```

### Supported Channel Credentials

gRPC supports the following channel credentials as part of the `channel_creds`
field of `xds_servers`.

#### Insecure credentials

- **Type Name**: `insecure`
- **Config**: Accepts no configuration

#### Google Default credentials

- **Type Name**: `google_default`
- **Config**: Accepts no configuration

#### mTLS credentials

- **Type Name**: `tls`
- **Config**: As described in [gRFC A65](a65):
```
{
  // Path to CA certificate file.
  // If unset, system-wide root certs are used.
  "ca_certificate_file": <string>,

  // Paths to identity certificate file and private key file.
  // If either of these fields are set, both must be set.
  // If set, mTLS will be used; if unset, normal TLS will be used.
  "certificate_file": <string>,
  "private_key_file": <string>,

  // How often to re-read the certificate files.
  // Value is the JSON format described for a google.protobuf.Duration
  // message in https://protobuf.dev/programming-guides/proto3/#json.
  // If unset, defaults to "600s".
  "refresh_interval": <string>
}
```

### Supported Certificate Provider Instances

gRPC supports the following Certificate Provider instances as part of the
`certificate_providers` field:

#### PEM file watcher

- **Plugin Name**: `file_watcher`
- **Config**: As described in [gRFC A29](a29):
```
{
    "certificate_file": "<path to the certificate file in PEM format>",
    "private_key_file": "<path to private key file in PEM format>",
    "ca_certificate_file": "<path to CA certificate file in PEM format>",
    "refresh_interval": "<JSON form of google.protobuf.Duration>"
}
```

### Supported Server Features

gRPC supports the following server features in the `server_features` field
inside `xds_servers`:
- `xds_v3`: Added in gRFC A30. Supported in older versions of gRPC. See
[here](grpc_xds_features.md) for when gRPC added support for xDS transport
protocol v3, and when support for xDS transport protocol v2 was dropped. 
- `ignore_resource_deletion`: Added in [gRFC A53](a53)


### When were fields added?

| Bootstrap Field | Relevant gRFCs
------------------|---------------
`xds_servers` | [A27](a27), [A71](a71)
`google_default` channel credentials | [A27](a27)
`insecure` channel credentials | [A27](a27)
`node` |  [A27](a27)
`certificate_providers` | [A29](a29)
`file_watcher`certificate provider | [A29](a29)
`xds_servers.server_features` | [A30](a30)
`server_listener_resource_name_template` | [A36](a36), [A47](a47)
`client_default_listener_resource_name_template` | [A47](a47)
`authorities` | [A47](a47)
`tls` channel credentials | [A65](a65)


[a27]: https://github.com/grpc/proposal/blob/master/A27-xds-global-load-balancing.md
[a29]: https://github.com/grpc/proposal/blob/master/A29-xds-tls-security.md#file_watcher-certificate-provider
[a30]: https://github.com/grpc/proposal/blob/master/A30-xds-v3.md
[a36]: https://github.com/grpc/proposal/blob/master/A36-xds-for-servers.md
[a47]: https://github.com/grpc/proposal/blob/master/A47-xds-federation.md
[a53]: https://github.com/grpc/proposal/blob/master/A53-xds-ignore-resource-deletion.md
[a65]: https://github.com/grpc/proposal/blob/master/A65-xds-mtls-creds-in-bootstrap.md#proposal
[a71]: https://github.com/grpc/proposal/blob/master/A71-xds-fallback.md