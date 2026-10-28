# gRPC PHP Extension Mirror (PIE)

This repository is a **read-only mirror** of the gRPC C-Core and PHP extension wrapper source code, optimized for installation using **PIE (PHP Installer for Extensions)**.

Development of the gRPC PHP extension takes place inside the main monorepo at [grpc/grpc](https://github.com/grpc/grpc). Please submit all issues, feature requests, and pull requests to the main repository.

## Installation via PIE

You can install this extension using PIE:

```bash
pie install grpc/grpc-php-ext
```

Ensure your system has the required build tools (like `make`, `gcc`, `autoconf`, `phpize`) installed prior to running the installation command.
