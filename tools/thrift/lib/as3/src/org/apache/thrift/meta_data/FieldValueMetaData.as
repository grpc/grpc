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
 */

package org.apache.thrift.meta_data {

  import org.apache.thrift.protocol.TType;

  /**
   * FieldValueMetaData and collection of subclasses to store metadata about
   * the value(s) of a field
   */
  public class FieldValueMetaData {
  
    public var type:int;  
 
    public function FieldValueMetaData(type:int) {
      this.type = type;
    }
  
    public function isStruct():Boolean {
      return type == TType.STRUCT; 
    }
  
    public function isContainer():Boolean {
      return type == TType.LIST || type == TType.MAP || type == TType.SET;
    }
  }
}
