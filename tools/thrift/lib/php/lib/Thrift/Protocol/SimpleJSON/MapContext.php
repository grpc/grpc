<?php
/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements. See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership. The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License. You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied. See the License for the
 * specific language governing permissions and limitations
 * under the License.
 *
 * @package thrift.protocol
 */

namespace Thrift\Protocol\SimpleJSON;

use Thrift\Protocol\TSimpleJSONProtocol;

class MapContext extends StructContext
{
    protected $isKey = true;
    private $p_;

    public function __construct($p)
    {
        parent::__construct($p);
    }

    public function write()
    {
        parent::write();
        $this->isKey = !$this->isKey;
    }

    public function isMapKey()
    {
        // we want to coerce map keys to json strings regardless
        // of their type
        return $this->isKey;
    }
}


