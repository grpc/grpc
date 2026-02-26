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

use Grpc\Status;
use PHPUnit\Framework\TestCase;
use const Grpc\STATUS_INVALID_ARGUMENT;
use const Grpc\STATUS_OK;
use const Grpc\STATUS_UNIMPLEMENTED;

class StatusTest extends TestCase
{
    public function testStatusOk()
    {
        $status = [
            'code' => STATUS_OK,
            'details' => 'OK',
        ];
        $return = Status::ok();
        $this->assertEquals($status, $return);
    }

    public function testStatusOkWithMetadata()
    {
        $status = [
            'code' => STATUS_OK,
            'details' => 'OK',
            'metadata' => ['a' => 1],
        ];
        $return = Status::ok(['a' => 1]);
        $this->assertEquals($status, $return);
    }

    public function testStatusUnimplemented()
    {
        $status = [
            'code' => STATUS_UNIMPLEMENTED,
            'details' => 'UNIMPLEMENTED',
        ];
        $return = Status::unimplemented();
        $this->assertEquals($status, $return);
    }

    public function testStatus()
    {
        $status = [
            'code' => STATUS_INVALID_ARGUMENT,
            'details' => 'invalid argument',
        ];
        $return = Status::status(
            STATUS_INVALID_ARGUMENT,
            "invalid argument"
        );
        $this->assertEquals($status, $return);
    }

    public function testStatusWithMetadata()
    {
        $status = [
            'code' => STATUS_INVALID_ARGUMENT,
            'details' => 'invalid argument',
            'metadata' => ['trailingMeta' => 100]
        ];
        $return = Status::status(
            STATUS_INVALID_ARGUMENT,
            "invalid argument",
            ['trailingMeta' => 100]
        );
        $this->assertEquals($status, $return);
    }
}
