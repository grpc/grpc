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
exports.Thrift = require('./thrift');

var connection = require('./connection');
exports.Connection = connection.Connection;
exports.createClient = connection.createClient;
exports.createConnection = connection.createConnection;
exports.createSSLConnection = connection.createSSLConnection;
exports.createStdIOClient = connection.createStdIOClient;
exports.createStdIOConnection = connection.createStdIOConnection;

var httpConnection = require('./http_connection');
exports.HttpConnection = httpConnection.HttpConnection;
exports.createHttpConnection = httpConnection.createHttpConnection;
exports.createHttpClient = httpConnection.createHttpClient;

var wsConnection = require('./ws_connection');
exports.WSConnection = wsConnection.WSConnection;
exports.createWSConnection = wsConnection.createWSConnection;
exports.createWSClient = wsConnection.createWSClient;

var xhrConnection = require('./xhr_connection');
exports.XHRConnection = xhrConnection.XHRConnection;
exports.createXHRConnection = xhrConnection.createXHRConnection;
exports.createXHRClient = xhrConnection.createXHRClient;

var server = require('./server');
exports.createServer = server.createServer;
exports.createMultiplexServer = server.createMultiplexServer;

var web_server = require('./web_server');
exports.createWebServer = web_server.createWebServer;

exports.Int64 = require('node-int64');
exports.Q = require('q');

var mprocessor = require('./multiplexed_processor');
var mprotocol = require('./multiplexed_protocol');
exports.Multiplexer = mprotocol.Multiplexer;
exports.MultiplexedProcessor = mprocessor.MultiplexedProcessor;

/*
 * Export transport and protocol so they can be used outside of a
 * cassandra/server context
 */
exports.TFramedTransport = require('./framed_transport');
exports.TBufferedTransport = require('./buffered_transport');
exports.TBinaryProtocol = require('./binary_protocol');
exports.TJSONProtocol = require('./json_protocol');
exports.TCompactProtocol = require('./compact_protocol');
