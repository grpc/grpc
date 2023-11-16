These are test keys *NOT* to be used in production.

The `certificate_hierarchy_1` and `certificate_hierarchy_2` contain
two disjoint but similarly organized certificate hierarchies. Each
contains:

* The respective root CA cert in `certs/ca.cert.pem`

* The intermediate CA cert in
  `intermediate/certs/intermediate.cert.pem`, signed by the root CA

* A client cert and a server cert--both signed by the intermediate
  CA--in `intermediate/certs/client.cert.pem` and
  `intermediate/certs/localhost-1.cert.pem`; the corresponding keys
  are in `intermediate/private`
