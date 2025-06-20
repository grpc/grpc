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
    public static function status(int $code, string $details, ?array $metadata = null): array
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

    public static function ok(?array $metadata = null): array
    {
        return Status::status(STATUS_OK, 'OK', $metadata);
    }
    public static function unimplemented(): array
    {
        return Status::status(STATUS_UNIMPLEMENTED, 'UNIMPLEMENTED');
    }
}
