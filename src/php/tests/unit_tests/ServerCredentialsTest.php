<?php
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
class ServerCredentialsTest extends PHPUnit_Framework_TestCase
{
    public function setUp()
    {
        $this->ca_data = file_get_contents(dirname(__FILE__).'/../data/ca.pem');
        $this->key_data =
            file_get_contents(dirname(__FILE__).'/../data/server1.key');
        $this->pem_data =
            file_get_contents(dirname(__FILE__).'/../data/server1.pem');
    }

    public function tearDown()
    {
    }

    public function testCreateSsl()
    {
        // accepts a string and array
        $server_creds = Grpc\ServerCredentials::createSsl($this->ca_data,
            array(array('private_key' => $this->key_data,
                        'cert_chain' => $this->pem_data)));
        $this->assertNotNull($server_creds);

        // accepts a bool as the third argument
        $server_creds = Grpc\ServerCredentials::createSsl($this->ca_data,
            array(array('private_key' => $this->key_data,
                        'cert_chain' => $this->pem_data)), true);
        $this->assertNotNull($server_creds);

        // accepts a array with 1 element as the second argument
        $server_creds = Grpc\ServerCredentials::createSsl(null,
            array(array('private_key' => $this->key_data,
            'cert_chain' => $this->pem_data)));
        $this->assertNotNull($server_creds);

        // accepts a array with multi elements as the second argument
        $server_creds = Grpc\ServerCredentials::createSsl(null,
            array(array('private_key' => $this->key_data,
                        'cert_chain' => $this->pem_data),
                  array('private_key' => $this->key_data,
                        'cert_chain' => $this->pem_data)));
        $this->assertNotNull($server_creds);
    }

    /**
     * @expectedException InvalidArgumentException
     */
    public function testCreateSslInvalidParam1()
    {
        $server_creds = Grpc\ServerCredentials::createSsl($this->ca_data,
                                                          'not_array');
    }

    /**
     * @expectedException InvalidArgumentException
     */
    public function testCreateSslInvalidParam2()
    {
        $server_creds = Grpc\ServerCredentials::createSsl('test', []);
    }

    /**
     * @expectedException InvalidArgumentException
     */
    public function testCreateSslInvalidParam3()
    {
        $server_creds = Grpc\ServerCredentials::createSsl($this->ca_data, [],
                                                          'test');
    }

    /**
     * @expectedException InvalidArgumentException
     */
    public function testCreateSslInvalidParam4()
    {
        $server_creds = Grpc\ServerCredentials::createSsl(null, ['test']);
    }

    /**
     * @expectedException InvalidArgumentException
     */
    public function testCreateSslInvalidParam5()
    {
        $server_creds = Grpc\ServerCredentials::createSsl(null,
            array(array('private_key' => 'test')));
    }

    /**
     * @expectedException InvalidArgumentException
     */
    public function testCreateSslInvalidParam6()
    {
        $server_creds = Grpc\ServerCredentials::createSsl(null,
            array(array('private_key' => $this->key_data)));
    }
}
