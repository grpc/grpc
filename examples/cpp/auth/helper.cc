/*
 *
 * Copyright 2024 gRPC authors.
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

#include "helper.h"

#include <fstream>
#include <iostream>
#include <sstream>

std::string LoadStringFromFile(std::string path) {
  std::ifstream file(path);
  if (!file.is_open()) {
    std::cout << "Failed to open " << path << std::endl;
    abort();
  }
  std::stringstream sstr;
  sstr << file.rdbuf();
  return sstr.str();
}
