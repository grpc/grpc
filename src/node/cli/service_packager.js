/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

'use strict';

var fs = require('fs');
var path = require('path');

var _ = require('underscore');
var async = require('async');
var pbjs = require('protobufjs/cli/pbjs');
var parseArgs = require('minimist');
var Mustache = require('mustache');

var package_json = require('../package.json');

var template_path = path.resolve(__dirname, 'service_packager');

var package_tpl_path = path.join(template_path, 'package.json.template');

var arg_format = {
  string: ['include', 'out', 'name', 'version'],
  alias: {
    include: 'i',
    out: 'o',
    name: 'n',
    version: 'v'
  }
};

// TODO(mlumish): autogenerate README.md from proto file

/**
 * Render package.json file from template using provided parameters.
 * @param {Object} params Map of parameter names to values
 * @param {function(Error, string)} callback Callback to pass rendered template
 *     text to
 */
function generatePackage(params, callback) {
  fs.readFile(package_tpl_path, {encoding: 'utf-8'}, function(err, template) {
    if (err) {
      callback(err);
    } else {
      var rendered = Mustache.render(template, params);
      callback(null, rendered);
    }
  });
}

/**
 * Copy a file
 * @param {string} src_path The filepath to copy from
 * @param {string} dest_path The filepath to copy to
 */
function copyFile(src_path, dest_path) {
  fs.createReadStream(src_path).pipe(fs.createWriteStream(dest_path));
}

/**
 * Run the script. Copies the index.js and LICENSE files to the output path,
 * renders the package.json template to the output path, and generates a
 * service.json file from the input proto files using pbjs. The arguments are
 * taken directly from the command line, and handled as follows:
 * -i (--include) : An include path for pbjs (can be dpulicated)
 * -o (--output): The output path
 * -n (--name): The name of the package
 * -v (--version): The package version
 * @param {Array} argv The argument vector
 */
function main(argv) {
  var args = parseArgs(argv, arg_format);
  var out_path = path.resolve(args.out);
  var include_dirs = [];
  if (args.include) {
    include_dirs = _.map(_.flatten([args.include]), function(p) {
      return path.resolve(p);
    });
  }
  args.grpc_version = package_json.version;
  generatePackage(args, function(err, rendered) {
    if (err) throw err;
    fs.writeFile(path.join(out_path, 'package.json'), rendered, function(err) {
      if (err) throw err;
    });
  });
  copyFile(path.join(template_path, 'index.js'),
           path.join(out_path, 'index.js'));
  copyFile(path.join(__dirname, '..', 'LICENSE'),
           path.join(out_path, 'LICENSE'));

  var service_stream = fs.createWriteStream(path.join(out_path,
                                                      'service.json'));
  var pbjs_args = _.flatten(['node', 'pbjs',
                             args._[0],
                             '-legacy',
                             _.map(include_dirs, function(dir) {
                               return "-path=" + dir;
                             })]);
  var old_stdout = process.stdout;
  process.__defineGetter__('stdout', function() {
    return service_stream;
  });
  var pbjs_status = pbjs.main(pbjs_args);
  process.__defineGetter__('stdout', function() {
    return old_stdout;
  });
  if (pbjs_status !== pbjs.STATUS_OK) {
    throw new Error('pbjs failed with status code ' + pbjs_status);
  }
}

exports.main = main;
