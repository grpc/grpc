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

namespace Grpc\Exceptions;

use Exception;

class GrpcException extends Exception
{
    /** @var mixed */
    protected $metadata;

  /**
   * @param string $message
   * @param int $code
   * @param mixed $metadata
   */
    public function __construct($message = "", $code = 0, $metadata = null)
    {
        parent::__construct($message, $code);
        $this->metadata = $metadata;
    }

  /**
   * @return mixed
   */
    public function getMetadata()
    {
        return $this->metadata;
    }
}
