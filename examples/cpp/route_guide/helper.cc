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

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/log/log.h"

#ifdef BAZEL_BUILD
#include "examples/protos/route_guide.grpc.pb.h"
#else
#include "route_guide.grpc.pb.h"
#endif

#ifdef BAZEL_BUILD
ABSL_FLAG(std::string, db_path, "examples/cpp/route_guide/route_guide_db.json",
          "Path to db file");
#else
ABSL_FLAG(std::string, db_path, "route_guide_db.json", "Path to db file");
#endif

namespace routeguide {

std::string GetDbFileContent(int argc, char** argv) {
  std::string db_path = absl::GetFlag(FLAGS_db_path);
  std::ifstream db_file(db_path);
  if (!db_file.is_open()) {
    LOG(ERROR) << "Failed to open " << db_path;
    abort();
  }
  std::stringstream db;
  db << db_file.rdbuf();
  return db.str();
}

// A simple parser for the json db file. It requires the db file to have the
// exact form of [{"location":{"latitude":123,"longitude":456},"name":
// "the name can be empty"},{ ... }...
class Parser {
 public:
  explicit Parser(const std::string& db) : db_(db) {
    if (!Match("[")) {
      SetFailedAndReturnFalse();
    }
  }

  bool Finished() { return current_ >= db_.size(); }

  bool TryParseOne(Feature* feature) {
    if (failed_ || Finished() || !Match("{")) {
      return SetFailedAndReturnFalse();
    }
    if (!Match(location_) || !Match("{") || !Match(latitude_)) {
      return SetFailedAndReturnFalse();
    }
    long temp = 0;
    ReadLong(&temp);
    feature->mutable_location()->set_latitude(temp);
    if (!Match(",") || !Match(longitude_)) {
      return SetFailedAndReturnFalse();
    }
    ReadLong(&temp);
    feature->mutable_location()->set_longitude(temp);
    if (!Match("},") || !Match(name_) || !Match("\"")) {
      return SetFailedAndReturnFalse();
    }
    size_t name_start = current_;
    while (current_ != db_.size() && db_[current_++] != '"') {
    }
    if (current_ == db_.size()) {
      return SetFailedAndReturnFalse();
    }
    feature->set_name(db_.substr(name_start, current_ - name_start - 1));
    if (!Match("},")) {
      if (db_[current_ - 1] == ']' && current_ == db_.size()) {
        return true;
      }
      return SetFailedAndReturnFalse();
    }
    return true;
  }

 private:
  bool SetFailedAndReturnFalse() {
    failed_ = true;
    return false;
  }

  bool Match(const std::string& prefix) {
    bool eq = db_.substr(current_, prefix.size()) == prefix;
    current_ += prefix.size();
    return eq;
  }

  void ReadLong(long* l) {
    size_t start = current_;
    while (current_ != db_.size() && db_[current_] != ',' &&
           db_[current_] != '}') {
      current_++;
    }
    // It will throw an exception if fails.
    *l = std::stol(db_.substr(start, current_ - start));
  }

  bool failed_ = false;
  std::string db_;
  size_t current_ = 0;
  const std::string location_ = "\"location\":";
  const std::string latitude_ = "\"latitude\":";
  const std::string longitude_ = "\"longitude\":";
  const std::string name_ = "\"name\":";
};

// Minifies a JSON string by removing all whitespace characters outside of
// strings.
std::string MinifyJson(const std::string& json) {
  std::regex whitespaceOutsideQuotes(R"(\s+(?=(?:(?:[^"]*"){2})*[^"]*$))");
  // Replace all matches with an empty string
  return std::regex_replace(json, whitespaceOutsideQuotes, "");
}

void ParseDb(const std::string& db, std::vector<Feature>* feature_list) {
  feature_list->clear();
  std::string db_content(MinifyJson(db));

  Parser parser(db_content);
  Feature feature;
  while (!parser.Finished()) {
    feature_list->push_back(Feature());
    if (!parser.TryParseOne(&feature_list->back())) {
      LOG(ERROR) << "Error parsing the db file";
      feature_list->clear();
      break;
    }
  }
  LOG(INFO) << "DB parsed, loaded " << feature_list->size() << " features.";
}

}  // namespace routeguide
