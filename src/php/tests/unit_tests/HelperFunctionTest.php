<?php
/*
 *
 * Copyright 2018 gRPC authors.
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

require_once(dirname(__FILE__).'/../../lib/Grpc/Helper.php');

class HelperFunctionTest extends PHPUnit_Framework_TestCase
{
  public function setUp()
  {
  }

  public function tearDown()
  {
  }

  public function testPRCStatusCodeToString()
  {
    $status_code = [
        \Grpc\STATUS_OK,
        \Grpc\STATUS_CANCELLED,
        \Grpc\STATUS_UNKNOWN,
        \Grpc\STATUS_INVALID_ARGUMENT,
        \Grpc\STATUS_DEADLINE_EXCEEDED,
        \Grpc\STATUS_NOT_FOUND,
        \Grpc\STATUS_ALREADY_EXISTS,
        \Grpc\STATUS_PERMISSION_DENIED,
        \Grpc\STATUS_UNAUTHENTICATED,
        \Grpc\STATUS_RESOURCE_EXHAUSTED,
        \Grpc\STATUS_FAILED_PRECONDITION,
        \Grpc\STATUS_ABORTED,
        \Grpc\STATUS_OUT_OF_RANGE,
        \Grpc\STATUS_UNIMPLEMENTED,
        \Grpc\STATUS_INTERNAL,
        \Grpc\STATUS_UNAVAILABLE,
        \Grpc\STATUS_DATA_LOSS,
    ];
    $status_code_string = [
        'STATUS_OK',
        'STATUS_CANCELLED',
        'STATUS_UNKNOWN',
        'STATUS_INVALID_ARGUMENT',
        'STATUS_DEADLINE_EXCEEDED',
        'STATUS_NOT_FOUND',
        'STATUS_ALREADY_EXISTS',
        'STATUS_PERMISSION_DENIED',
        'STATUS_UNAUTHENTICATED',
        'STATUS_RESOURCE_EXHAUSTED',
        'STATUS_FAILED_PRECONDITION',
        'STATUS_ABORTED',
        'STATUS_OUT_OF_RANGE',
        'STATUS_UNIMPLEMENTED',
        'STATUS_INTERNAL',
        'STATUS_UNAVAILABLE',
        'STATUS_DATA_LOSS',
    ];
    for ($i = 0; $i < count($status_code); $i++) {
        $this->assertSame(\Grpc\RPCStatusCodeToString($status_code[$i]),
                          $status_code_string[$i]);
    }
  }

  public function testCallErrorToString()
  {
      $call_error = [
          \Grpc\CALL_OK,
          \Grpc\CALL_ERROR,
          \Grpc\CALL_ERROR_NOT_ON_SERVER,
          \Grpc\CALL_ERROR_NOT_ON_CLIENT,
          \Grpc\CALL_ERROR_ALREADY_INVOKED,
          \Grpc\CALL_ERROR_NOT_INVOKED,
          \Grpc\CALL_ERROR_ALREADY_FINISHED,
          \Grpc\CALL_ERROR_TOO_MANY_OPERATIONS,
          \Grpc\CALL_ERROR_INVALID_FLAGS,
      ];
      $call_error_string = [
          'CALL_OK',
          'CALL_ERROR',
          'CALL_ERROR_NOT_ON_SERVER',
          'CALL_ERROR_NOT_ON_CLIENT',
          'CALL_ERROR_ALREADY_INVOKED',
          'CALL_ERROR_NOT_INVOKED',
          'CALL_ERROR_ALREADY_FINISHED',
          'CALL_ERROR_TOO_MANY_OPERATIONS',
          'CALL_ERROR_INVALID_FLAGS',
      ];
      for ($i = 0; $i < count($call_error); $i++) {
          $this->assertSame(\Grpc\CallErrorToString($call_error[$i]),
                            $call_error_string[$i]);
      }
  }

  public function testChannelStatusToString()
  {
      $channel_status = [
          \Grpc\CHANNEL_IDLE,
          \Grpc\CHANNEL_CONNECTING,
          \Grpc\CHANNEL_READY,
          \Grpc\CHANNEL_TRANSIENT_FAILURE,
          \Grpc\CHANNEL_FATAL_FAILURE,
      ];
      $channel_status_string = [
          'CHANNEL_IDLE',
          'CHANNEL_CONNECTING',
          'CHANNEL_READY',
          'CHANNEL_TRANSIENT_FAILURE',
          'CHANNEL_FATAL_FAILURE',
      ];
      for ($i = 0; $i < count($channel_status); $i++) {
          $this->assertSame(\Grpc\ChannelStatusToString($channel_status[$i]),
                            $channel_status_string[$i]);
      }
  }
}
