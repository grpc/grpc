<?php
/*
 *
 * Copyright 2017 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */
class ServerCredentialsTest extends PHPUnit_Framework_TestCase
{
    public function setUp()
    {
        $this->ca_data =
            file_get_contents(dirname(__FILE__).'/../data/ca.pem');
        $this->key_data =
            file_get_contents(dirname(__FILE__).'/../data/server1.key');
        $this->pem_data =
            file_get_contents(dirname(__FILE__).'/../data/server1.pem');
    }

    public function tearDown()
    {
    }

    public function testCreateSslWithStrAndArray()
    {
        $server_creds = Grpc\ServerCredentials::createSsl($this->ca_data,
            [['private_key' => $this->key_data,
              'cert_chain' => $this->pem_data, ]]);
        $this->assertNotNull($server_creds);
    }

    public function testCreateSslWithStrAndArrayAndBool()
    {
        $server_creds = Grpc\ServerCredentials::createSsl($this->ca_data,
            [['private_key' => $this->key_data,
              'cert_chain' => $this->pem_data, ]], true);
        $this->assertNotNull($server_creds);
    }

    public function testCreateSslWithNullAndArray()
    {
        $server_creds = Grpc\ServerCredentials::createSsl(null,
            [['private_key' => $this->key_data,
              'cert_chain' => $this->pem_data, ]]);
        $this->assertNotNull($server_creds);
    }

    public function testCreateSslWithArrays()
    {
        $server_creds = Grpc\ServerCredentials::createSsl(null,
            [['private_key' => $this->key_data,
              'cert_chain' => $this->pem_data, ],
             ['private_key' => $this->key_data,
              'cert_chain' => $this->pem_data, ], ]);
        $this->assertNotNull($server_creds);
    }

    /**
     * @expectedException InvalidArgumentException
     */
    public function testCreateSslWithStrNotArray()
    {
        $server_creds = Grpc\ServerCredentials::createSsl($this->ca_data,
                                                          'not_array');
    }

    /**
     * @expectedException InvalidArgumentException
     */
    public function testCreateSslWithNullArray()
    {
        $server_creds = Grpc\ServerCredentials::createSsl('test', []);
    }

    /**
     * @expectedException InvalidArgumentException
     */
    public function testCreateSslWithNullArrayAndNotBool()
    {
        $server_creds = Grpc\ServerCredentials::createSsl($this->ca_data, [],
                                                          'test');
    }

    /**
     * @expectedException InvalidArgumentException
     */
    public function testCreateSslWithNotArrayOfArray()
    {
        $server_creds = Grpc\ServerCredentials::createSsl(null, ['test']);
    }

    /**
     * @expectedException InvalidArgumentException
     */
    public function testCreateSslWithNotCertChainOfArray()
    {
        $server_creds = Grpc\ServerCredentials::createSsl(null,
            [['private_key' => $this->key_data]]);
    }
}
