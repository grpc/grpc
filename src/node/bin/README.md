# Command Line Tools

# Service Packager

The command line tool `bin/service_packager`, when called with the following command line:

```bash
service_packager proto_file -o output_path -n name -v version [-i input_path...]
```

Populates `output_path` with a node package consisting of a `package.json` populated with `name` and `version`, an `index.js`, a `LICENSE` file copied from gRPC, and a `service.json`, which is compiled from `proto_file` and the given `input_path`s. `require('output_path')` returns an object that is equivalent to

```js
{ client: require('grpc').load('service.json'),
  auth: require('google-auth-library') }
```
