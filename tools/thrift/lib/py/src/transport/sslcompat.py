#
# licensed to the apache software foundation (asf) under one
# or more contributor license agreements. see the notice file
# distributed with this work for additional information
# regarding copyright ownership. the asf licenses this file
# to you under the apache license, version 2.0 (the
# "license"); you may not use this file except in compliance
# with the license. you may obtain a copy of the license at
#
#   http://www.apache.org/licenses/license-2.0
#
# unless required by applicable law or agreed to in writing,
# software distributed under the license is distributed on an
# "as is" basis, without warranties or conditions of any
# KIND, either express or implied. See the License for the
# specific language governing permissions and limitations
# under the License.
#

import sys

from thrift.transport.TTransport import TTransportException


def legacy_validate_callback(self, cert, hostname):
    """legacy method to validate the peer's SSL certificate, and to check
    the commonName of the certificate to ensure it matches the hostname we
    used to make this connection.  Does not support subjectAltName records
    in certificates.

    raises TTransportException if the certificate fails validation.
    """
    if 'subject' not in cert:
        raise TTransportException(
            TTransportException.NOT_OPEN,
            'No SSL certificate found from %s:%s' % (self.host, self.port))
    fields = cert['subject']
    for field in fields:
        # ensure structure we get back is what we expect
        if not isinstance(field, tuple):
            continue
        cert_pair = field[0]
        if len(cert_pair) < 2:
            continue
        cert_key, cert_value = cert_pair[0:2]
        if cert_key != 'commonName':
            continue
        certhost = cert_value
        # this check should be performed by some sort of Access Manager
        if certhost == hostname:
            # success, cert commonName matches desired hostname
            return
        else:
            raise TTransportException(
                TTransportException.UNKNOWN,
                'Hostname we connected to "%s" doesn\'t match certificate '
                'provided commonName "%s"' % (self.host, certhost))
    raise TTransportException(
        TTransportException.UNKNOWN,
        'Could not validate SSL certificate from host "%s".  Cert=%s'
        % (hostname, cert))


try:
    import ipaddress  # noqa
    _match_has_ipaddress = True
except ImportError:
    _match_has_ipaddress = False

try:
    from backports.ssl_match_hostname import match_hostname
    _match_hostname = match_hostname
except ImportError:
    if sys.hexversion < 0x030500F0:
        _match_has_ipaddress = False
    try:
        from ssl import match_hostname
        _match_hostname = match_hostname
    except ImportError:
        _match_hostname = legacy_validate_callback
