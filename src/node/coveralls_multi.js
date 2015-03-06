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

var _ = require('underscore');
var parseArgs = require('minimist');
var convertLcovToCoveralls = require('coveralls/lib/convertLcovToCoveralls.js');
var getOptions = require('coveralls/lib/getOptions.js').getOptions;
var sendToCoveralls = require('coveralls/lib/sendToCoveralls.js');
var async = require('async');

/**
 * Merge a list of coveralls API objects into a single one
 * @param merge_list Array of API objects
 * @return Single API object that combines all test runs
 */
function mergeCoveralls(merge_list) {
  var file_map = {};
  var result = {};
  _.each(merge_list, function(test) {
    if (result.service_job_id !== undefined &&
        test.service_job_id !== result.service_job_id) {
      throw new Error('Inconsistent job IDs');
    }
    result.service_job_id = test.service_job_id;
    if (result.service_name !== undefined &&
        test.service_name !== result.service_name) {
      throw new Error('Inconsistent service name');
    }
    result.service_name = test.service_name;
    _.each(test.source_files, function(file) {
      if (file_map.hasOwnProperty(file.name)) {
        var saved_file = file_map[file.name];
        if (saved_file.source !== file.source) {
          throw new Error('Inconsistent files: ' + file.name);
        }

        _.each(file.coverage, function(count, line) {
          // If one is null, the other must also be null
          if ((saved_file.coverage[line] === null) !==
              (count === null)) {
            throw new Error('Inconsistent line relevance: ' + file.name + ':' +
                line);
          }
          if (count !== null) {
            saved_file.coverage[line] += count;
          }
        });
      } else {
        file_map[file.name] = file;
      }
    });
  });
  result.source_files = _.map(file_map, _.identity);
  return result;
}

var argv = parseArgs(process.argv.slice(2), {'--': true});

var coveralls_files = argv._;
var lcov_files = argv['--'];

getOptions(function(err, options) {
  delete options.filepath;
  async.parallel(_.map(coveralls_files, function(name) {
    return _.partial(fs.readFile, name);
  }).concat(_.map(lcov_files, function(name) {
    return _.partial(convertLcovToCoveralls, name, options);
  })), function(err, list) {
    if (err) {
      throw err;
    }
    var merged = mergeCoveralls(list);
    sendToCoveralls(merged, function(err, response, body) {
      if (err) {
        throw err;
      }
      if (response.statusCode >= 400) {
        throw new Error("Bad response: " + response.statusCode + " " + body);
      }
    });
  });
});
