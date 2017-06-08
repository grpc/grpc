#!/usr/bin/env node
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

/**
 * This file is required because package.json cannot reference a file that
 * is not distributed with the package, and we use node-pre-gyp to distribute
 * the plugin binary
 */

'use strict';

var path = require('path');
var execFile = require('child_process').execFile;

var exe_ext = process.platform === 'win32' ? '.exe' : '';

var plugin = path.resolve(__dirname, 'grpc_node_plugin' + exe_ext);

var child_process = execFile(plugin, process.argv.slice(2), {encoding: 'buffer'}, function(error, stdout, stderr) {
  if (error) {
    throw error;
  }
});

process.stdin.pipe(child_process.stdin);
child_process.stdout.pipe(process.stdout);
child_process.stderr.pipe(process.stderr);
