The `etc/roots.pem` file is generated using:

1. The root certificates from Mozilla:
https://hg.mozilla.org/mozilla-central/raw-file/tip/security/nss/lib/ckfw/builtins/certdata.txt

2. A tool to convert them to PEM format:
https://github.com/agl/extract-nss-root-certs.
