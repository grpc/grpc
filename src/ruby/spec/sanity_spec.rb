# Copyright 2015 gRPC authors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

require 'find'

describe 'sanity' do
  it 'tests.txt is up to date' do
    tests_from_file = []
    File.open(File.join(File.dirname(__FILE__), 'tests.txt'), 'r') do |f|
      f.each_line do |line|
        tests_from_file << line.strip
      end
    end
    found_tests = Find.find(File.dirname(File.dirname(__FILE__))).select do |p|
      /.*_spec.rb$/ =~ p
    end
    found_tests.each do |line|
      found = line.split(File.dirname(__FILE__))[1].sub('/', '').strip
      expect(tests_from_file.include?(found)).to be(true)
    end
  end
end
