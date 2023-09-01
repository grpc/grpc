<?php
/*
 *
 * Copyright 2015 gRPC authors.
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

require_once(dirname(__FILE__) . '/../../lib/Grpc/Status.php');

class StatusTest extends \PHPUnit\Framework\TestCase
{
    public function testStatusOk()
    {
        $status = [
            'code' => \Grpc\STATUS_OK,
            'details' => 'OK',
        ];
        $return = \Grpc\Status::ok();
        $this->assertEquals($status, $return);
    }

    public function testStatusOkWithMetadata()
    {
        $status = [
            'code' => \Grpc\STATUS_OK,
            'details' => 'OK',
            'metadata' => ['a' => 1],
        ];
        $return = \Grpc\Status::ok(['a' => 1]);
        $this->assertEquals($status, $return);
    }

    public function testStatusUnimplemented()
    {
        $status = [
            'code' => \Grpc\STATUS_UNIMPLEMENTED,
            'details' => 'UNIMPLEMENTED',
        ];
        $return = \Grpc\Status::unimplemented();
        $this->assertEquals($status, $return);
    }

    public function testStatus()
    {
        $status = [
            'code' => \Grpc\STATUS_INVALID_ARGUMENT,
            'details' => 'invalid argument',
        ];
        $return = \Grpc\Status::status(
            \Grpc\STATUS_INVALID_ARGUMENT,
            "invalid argument"
        );
        $this->assertEquals($status, $return);
    }

    public function testStatusWithMetadata()
    {
        $status = [
            'code' => \Grpc\STATUS_INVALID_ARGUMENT,
            'details' => 'invalid argument',
            'metadata' => ['trailiingMeta' => 100]
        ];
        $return = \Grpc\Status::status(
            \Grpc\STATUS_INVALID_ARGUMENT,
            "invalid argument",
            ['trailiingMeta' => 100]
        );
        $this->assertEquals($status, $return);
    }
}
