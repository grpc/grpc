/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#import "GRPCSecureChannel.h"

#import <grpc/grpc_security.h>

static const char *kCertificates =
"# Issuer: CN=GTE CyberTrust Global Root O=GTE Corporation OU=GTE CyberTrust Solutions, Inc.\n"
"# Subject: CN=GTE CyberTrust Global Root O=GTE Corporation OU=GTE CyberTrust Solutions, Inc.\n"
"# Label: \"GTE CyberTrust Global Root\"\n"
"# Serial: 421\n"
"# MD5 Fingerprint: ca:3d:d3:68:f1:03:5c:d0:32:fa:b8:2b:59:e8:5a:db\n"
"# SHA1 Fingerprint: 97:81:79:50:d8:1c:96:70:cc:34:d8:09:cf:79:44:31:36:7e:f4:74\n"
"# SHA256 Fingerprint: a5:31:25:18:8d:21:10:aa:96:4b:02:c7:b7:c6:da:32:03:17:08:94:e5:fb:71:ff:fb:66:67:d5:e6:81:0a:36\n"
"-----BEGIN CERTIFICATE-----\n"
"MIICWjCCAcMCAgGlMA0GCSqGSIb3DQEBBAUAMHUxCzAJBgNVBAYTAlVTMRgwFgYD\n"
"VQQKEw9HVEUgQ29ycG9yYXRpb24xJzAlBgNVBAsTHkdURSBDeWJlclRydXN0IFNv\n"
"bHV0aW9ucywgSW5jLjEjMCEGA1UEAxMaR1RFIEN5YmVyVHJ1c3QgR2xvYmFsIFJv\n"
"b3QwHhcNOTgwODEzMDAyOTAwWhcNMTgwODEzMjM1OTAwWjB1MQswCQYDVQQGEwJV\n"
"UzEYMBYGA1UEChMPR1RFIENvcnBvcmF0aW9uMScwJQYDVQQLEx5HVEUgQ3liZXJU\n"
"cnVzdCBTb2x1dGlvbnMsIEluYy4xIzAhBgNVBAMTGkdURSBDeWJlclRydXN0IEds\n"
"b2JhbCBSb290MIGfMA0GCSqGSIb3DQEBAQUAA4GNADCBiQKBgQCVD6C28FCc6HrH\n"
"iM3dFw4usJTQGz0O9pTAipTHBsiQl8i4ZBp6fmw8U+E3KHNgf7KXUwefU/ltWJTS\n"
"r41tiGeA5u2ylc9yMcqlHHK6XALnZELn+aks1joNrI1CqiQBOeacPwGFVw1Yh0X4\n"
"04Wqk2kmhXBIgD8SFcd5tB8FLztimQIDAQABMA0GCSqGSIb3DQEBBAUAA4GBAG3r\n"
"GwnpXtlR22ciYaQqPEh346B8pt5zohQDhT37qw4wxYMWM4ETCJ57NE7fQMh017l9\n"
"3PR2VX2bY1QY6fDq81yx2YtCHrnAlU66+tXifPVoYb+O7AWXX1uw16OFNMQkpw0P\n"
"lZPvy5TYnh+dXIVtx6quTx8itc2VrbqnzPmrC3p/\n"
"-----END CERTIFICATE-----\n";


@implementation GRPCSecureChannel

- (instancetype)initWithHost:(NSString *)host {
  // TODO(jcanizales): Get the certificates here.
  NSURL *url = [[NSBundle mainBundle] URLForResource:@"gRPC.bundle/roots" withExtension:@"pem"];
  NSData *fontData = [NSData dataWithContentsOfURL:url];
  NSString *str = [[NSString alloc] initWithData:fontData encoding:NSUTF8StringEncoding];
  NSLog(@"Certs:\n%@", str);
  grpc_credentials *credentials = grpc_ssl_credentials_create(str.UTF8String, NULL);
  return (self = [super initWithChannel:grpc_secure_channel_create(credentials,
                                                                   host.UTF8String,
                                                                   NULL)]);
}

@end
