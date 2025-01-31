<?php
/*
 *
 * Copyright 2020 gRPC authors.
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

namespace Grpc;

use Grpc\Exceptions\AbortedException;
use Grpc\Exceptions\AlreadyExistsException;
use Grpc\Exceptions\CancelledException;
use Grpc\Exceptions\DataLossException;
use Grpc\Exceptions\DeadlineExceededException;
use Grpc\Exceptions\FailedPreconditionException;
use Grpc\Exceptions\GrpcException;
use Grpc\Exceptions\InternalException;
use Grpc\Exceptions\InvalidArgumentException;
use Grpc\Exceptions\NotFoundException;
use Grpc\Exceptions\OutOfRangeException;
use Grpc\Exceptions\PermissionDeniedException;
use Grpc\Exceptions\ResourceExhaustedException;
use Grpc\Exceptions\UnauthenticatedException;
use Grpc\Exceptions\UnavailableException;
use Grpc\Exceptions\UnimplementedException;
use Grpc\Exceptions\UnknownException;

/**
 * This is an experimental and incomplete implementation of gRPC server
 * for PHP. APIs are _definitely_ going to be changed.
 *
 * DO NOT USE in production.
 */

/**
 * Class Status
 * @package Grpc
 */
class Status
{
  private static $exceptionMapping = [
      STATUS_CANCELLED => CancelledException::class,
      STATUS_UNKNOWN => UnknownException::class,
      STATUS_INVALID_ARGUMENT => InvalidArgumentException::class,
      STATUS_DEADLINE_EXCEEDED => DeadlineExceededException::class,
      STATUS_NOT_FOUND => NotFoundException::class,
      STATUS_ALREADY_EXISTS => AlreadyExistsException::class,
      STATUS_PERMISSION_DENIED => PermissionDeniedException::class,
      STATUS_RESOURCE_EXHAUSTED => ResourceExhaustedException::class,
      STATUS_FAILED_PRECONDITION => FailedPreconditionException::class,
      STATUS_ABORTED => AbortedException::class,
      STATUS_OUT_OF_RANGE => OutOfRangeException::class,
      STATUS_UNIMPLEMENTED => UnimplementedException::class,
      STATUS_INTERNAL => InternalException::class,
      STATUS_UNAVAILABLE => UnavailableException::class,
      STATUS_DATA_LOSS => DataLossException::class,
      STATUS_UNAUTHENTICATED => UnauthenticatedException::class,
  ];

    public static function status(int $code, string $details, array $metadata = null): array
    {
        $status = [
            'code' => $code,
            'details' => $details,
        ];
        if ($metadata) {
            $status['metadata'] = $metadata;
        }
        return $status;
    }

    public static function ok(array $metadata = null): array
    {
        return Status::status(STATUS_OK, 'OK', $metadata);
    }

    public static function unimplemented(): array
    {
        return Status::status(STATUS_UNIMPLEMENTED, 'UNIMPLEMENTED');
    }

  /**
   * Throw an exception if the provided status is an error one.
   *
   * @param mixed $status
   * @throws GrpcException
   * @return void
   */
    public static function throwIfError($status)
    {
        $code = $status['code'] ?? 0;
        if ($code == STATUS_OK) {
            return;
        }

        $details = $status['details'] ?? '';
        $metadata = $status['metadata'] ?? [];
        $exceptionClass = self::$exceptionMapping[$code] ?? GrpcException::class;
        throw new $exceptionClass($details, $code, $metadata);
    }
}
