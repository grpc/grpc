# borrowed from https://jamielinux.com/docs/openssl-certificate-authority/

# generate a root ca cert. use that to sign an intermediate ca cert,
# and use that to sign a few server certs and a client cert


WORK_DIR=/tmp/_test_certs_
ROOT_CA_DIR=$WORK_DIR/root_ca

mkdir -p $WORK_DIR && cd $WORK_DIR

mkdir $ROOT_CA_DIR
cd $ROOT_CA_DIR

mkdir certs crl newcerts private
chmod 700 private
touch index.txt
echo 1000 > serial

cat > openssl.cnf <<EOF
[ ca ]
default_ca = CA_default

[ CA_default ]
# Directory and file locations.
dir               = root_ca
certs             = $ROOT_CA_DIR/certs
crl_dir           = $ROOT_CA_DIR/crl
new_certs_dir     = $ROOT_CA_DIR/newcerts
database          = $ROOT_CA_DIR/index.txt
serial            = $ROOT_CA_DIR/serial
RANDFILE          = $ROOT_CA_DIR/private/.rand

# The root key and root certificate.
private_key       = $ROOT_CA_DIR/private/ca.key.pem
certificate       = $ROOT_CA_DIR/certs/ca.cert.pem

# For certificate revocation lists.
# crlnumber         = $ROOT_CA_DIR/crlnumber
# crl               = $ROOT_CA_DIR/crl/ca.crl.pem
# crl_extensions    = crl_ext
# default_crl_days  = 30

# SHA-1 is deprecated, so use SHA-2 instead.
default_md        = sha256

name_opt          = ca_default
cert_opt          = ca_default
default_days      = 375
preserve          = no
policy            = policy_strict

[ policy_strict ]
countryName             = match
stateOrProvinceName     = match
organizationName        = match
organizationalUnitName  = optional
commonName              = supplied
emailAddress            = optional

[ policy_loose ]
countryName             = optional
stateOrProvinceName     = optional
localityName            = optional
organizationName        = optional
organizationalUnitName  = optional
commonName              = supplied
emailAddress            = optional

[ req ]
default_bits        = 2048
distinguished_name  = req_distinguished_name
string_mask         = utf8only

# SHA-1 is deprecated, so use SHA-2 instead.
default_md          = sha256

# Extension to add when the -x509 option is used.
x509_extensions     = v3_ca

[ req_distinguished_name ]
# See <https://en.wikipedia.org/wiki/Certificate_signing_request>.
countryName                     = Country Name (2 letter code)
stateOrProvinceName             = State or Province Name
localityName                    = Locality Name
0.organizationName              = Organization Name
organizationalUnitName          = Organizational Unit Name
commonName                      = Common Name
emailAddress                    = Email Address

# Optionally, specify some defaults.
countryName_default             = US
stateOrProvinceName_default     = dummy
localityName_default            = dummy
0.organizationName_default      = dummy
organizationalUnitName_default  = dummy
emailAddress_default            =

[ v3_ca ]
subjectKeyIdentifier = hash
authorityKeyIdentifier = keyid:always,issuer
basicConstraints = critical, CA:true
keyUsage = critical, digitalSignature, cRLSign, keyCertSign

[ v3_intermediate_ca ]
subjectKeyIdentifier = hash
authorityKeyIdentifier = keyid:always,issuer
basicConstraints = critical, CA:true, pathlen:0
keyUsage = critical, digitalSignature, cRLSign, keyCertSign

[ usr_cert ]
basicConstraints = CA:FALSE
nsCertType = client, email
nsComment = "OpenSSL Generated Client Certificate"
subjectKeyIdentifier = hash
authorityKeyIdentifier = keyid,issuer
keyUsage = critical, nonRepudiation, digitalSignature, keyEncipherment
extendedKeyUsage = clientAuth, emailProtection

[ server_cert ]
basicConstraints = CA:FALSE
nsCertType = server
nsComment = "OpenSSL Generated Server Certificate"
subjectKeyIdentifier = hash
authorityKeyIdentifier = keyid,issuer:always
keyUsage = critical, digitalSignature, keyEncipherment
extendedKeyUsage = serverAuth

[ crl_ext ]
authorityKeyIdentifier=keyid:always

[ ocsp ]
basicConstraints = CA:FALSE
subjectKeyIdentifier = hash
authorityKeyIdentifier = keyid,issuer
keyUsage = critical, digitalSignature
extendedKeyUsage = critical, OCSPSigning

EOF


openssl genrsa -out private/ca.key.pem 4096

openssl req -config openssl.cnf \
        -key private/ca.key.pem \
        -new -x509 -days 7300 -sha256 -extensions v3_ca \
        -out certs/ca.cert.pem \
        -subj "/C=us/ST=dummy/O=dummy/CN=root ca"

chmod 444 certs/ca.cert.pem


####
#### intermediate
####

cd $WORK_DIR

mkdir $ROOT_CA_DIR/intermediate

cd $ROOT_CA_DIR/intermediate
mkdir certs crl csr newcerts private
chmod 700 private
touch index.txt
echo 1000 > serial

echo 1000 > $ROOT_CA_DIR/intermediate/crlnumber

cat > openssl.cnf <<EOF
[ ca ]
default_ca = CA_default

[ CA_default ]
# Directory and file locations.
dir               = root_ca/intermediate
certs             = $ROOT_CA_DIR/intermediate/certs
crl_dir           = $ROOT_CA_DIR/intermediate/crl
new_certs_dir     = $ROOT_CA_DIR/intermediate/newcerts
database          = $ROOT_CA_DIR/intermediate/index.txt
serial            = $ROOT_CA_DIR/intermediate/serial
RANDFILE          = $ROOT_CA_DIR/intermediate/private/.rand

# The root key and root certificate.
private_key       = $ROOT_CA_DIR/intermediate/private/intermediate.key.pem
certificate       = $ROOT_CA_DIR/intermediate/certs/intermediate.cert.pem

# For certificate revocation lists.
crlnumber         = $ROOT_CA_DIR/intermediate/crlnumber
crl               = $ROOT_CA_DIR/intermediate/crl/intermediate.crl.pem
crl_extensions    = crl_ext
default_crl_days  = 30

# SHA-1 is deprecated, so use SHA-2 instead.
default_md        = sha256

name_opt          = ca_default
cert_opt          = ca_default
default_days      = 375
preserve          = no
policy            = policy_loose

[ policy_strict ]
countryName             = match
stateOrProvinceName     = match
organizationName        = match
organizationalUnitName  = optional
commonName              = supplied
emailAddress            = optional

[ policy_loose ]
countryName             = optional
stateOrProvinceName     = optional
localityName            = optional
organizationName        = optional
organizationalUnitName  = optional
commonName              = supplied
emailAddress            = optional

[ req ]
default_bits        = 2048
distinguished_name  = req_distinguished_name
string_mask         = utf8only

# SHA-1 is deprecated, so use SHA-2 instead.
default_md          = sha256

# Extension to add when the -x509 option is used.
x509_extensions     = v3_ca

[ req_distinguished_name ]
# See <https://en.wikipedia.org/wiki/Certificate_signing_request>.
countryName                     = Country Name (2 letter code)
stateOrProvinceName             = State or Province Name
localityName                    = Locality Name
0.organizationName              = Organization Name
organizationalUnitName          = Organizational Unit Name
commonName                      = Common Name
emailAddress                    = Email Address

# Optionally, specify some defaults.
countryName_default             = US
stateOrProvinceName_default     = dummy
localityName_default            = dummy
0.organizationName_default      = dummy
organizationalUnitName_default  = dummy
emailAddress_default            =

[ v3_ca ]
subjectKeyIdentifier = hash
authorityKeyIdentifier = keyid:always,issuer
basicConstraints = critical, CA:true
keyUsage = critical, digitalSignature, cRLSign, keyCertSign

[ v3_intermediate_ca ]
subjectKeyIdentifier = hash
authorityKeyIdentifier = keyid:always,issuer
basicConstraints = critical, CA:true, pathlen:0
keyUsage = critical, digitalSignature, cRLSign, keyCertSign

[ usr_cert ]
basicConstraints = CA:FALSE
nsCertType = client, email
nsComment = "OpenSSL Generated Client Certificate"
subjectKeyIdentifier = hash
authorityKeyIdentifier = keyid,issuer
keyUsage = critical, nonRepudiation, digitalSignature, keyEncipherment
extendedKeyUsage = clientAuth, emailProtection

[ server_cert ]
basicConstraints = CA:FALSE
nsCertType = server
nsComment = "OpenSSL Generated Server Certificate"
subjectKeyIdentifier = hash
authorityKeyIdentifier = keyid,issuer:always
keyUsage = critical, digitalSignature, keyEncipherment
extendedKeyUsage = serverAuth

[ crl_ext ]
authorityKeyIdentifier=keyid:always

[ ocsp ]
basicConstraints = CA:FALSE
subjectKeyIdentifier = hash
authorityKeyIdentifier = keyid,issuer
keyUsage = critical, digitalSignature
extendedKeyUsage = critical, OCSPSigning

EOF

cd $ROOT_CA_DIR

openssl genrsa -out intermediate/private/intermediate.key.pem 4096

chmod 400 intermediate/private/intermediate.key.pem

openssl req -config intermediate/openssl.cnf -new -sha256 \
        -key intermediate/private/intermediate.key.pem \
        -out intermediate/csr/intermediate.csr.pem \
        -subj "/C=us/ST=dummy/O=dummy/CN=intermediate ca"


cd $ROOT_CA_DIR
yes | openssl ca -config openssl.cnf -extensions v3_intermediate_ca \
      -days 3650 -notext -md sha256 \
      -in intermediate/csr/intermediate.csr.pem \
      -out intermediate/certs/intermediate.cert.pem

chmod 444 intermediate/certs/intermediate.cert.pem

cat intermediate/certs/intermediate.cert.pem \
      certs/ca.cert.pem > intermediate/certs/ca-chain.cert.pem
chmod 444 intermediate/certs/ca-chain.cert.pem



###
### server certs
###

cd $ROOT_CA_DIR

for server_cert_num in `seq 3`; do
    openssl genrsa -out intermediate/private/localhost-${server_cert_num}.key.pem 2048
    chmod 400 intermediate/private/localhost-${server_cert_num}.key.pem

    openssl req -config intermediate/openssl.cnf \
            -key intermediate/private/localhost-${server_cert_num}.key.pem \
            -new -sha256 -out intermediate/csr/localhost-${server_cert_num}.csr.pem \
            -subj "/C=us/ST=dummy/O=dummy/OU=${server_cert_num}/CN=localhost"

    yes | openssl ca -config intermediate/openssl.cnf \
            -extensions server_cert -days 375 -notext -md sha256 \
            -in intermediate/csr/localhost-${server_cert_num}.csr.pem \
            -out intermediate/certs/localhost-${server_cert_num}.cert.pem
    chmod 444 intermediate/certs/localhost-${server_cert_num}.cert.pem
done


###
### client cert
###

cd $ROOT_CA_DIR

openssl genrsa -out intermediate/private/client.key.pem 2048
chmod 400 intermediate/private/client.key.pem

openssl req -config intermediate/openssl.cnf \
        -key intermediate/private/client.key.pem \
        -new -sha256 -out intermediate/csr/client.csr.pem \
        -subj "/C=us/ST=dummy/O=dummy/CN=client"

yes | openssl ca -config intermediate/openssl.cnf \
              -extensions usr_cert -days 375 -notext -md sha256 \
              -in intermediate/csr/client.csr.pem \
              -out intermediate/certs/client.cert.pem
chmod 444 intermediate/certs/client.cert.pem

