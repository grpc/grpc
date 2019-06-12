//
//  GTMRegexTest.m
//
//  Copyright 2007-2008 Google Inc.
//
//  Licensed under the Apache License, Version 2.0 (the "License"); you may not
//  use this file except in compliance with the License.  You may obtain a copy
//  of the License at
//
//  http://www.apache.org/licenses/LICENSE-2.0
//
//  Unless required by applicable law or agreed to in writing, software
//  distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
//  WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
//  License for the specific language governing permissions and limitations under
//  the License.
//

#import "GTMSenTestCase.h"
#import "GTMRegex.h"

//
// NOTE:
//
// We don't really test any of the pattern matching since that's testing
// libregex, we just want to test our wrapper.
//

#pragma clang diagnostic push
// Ignore all of the deprecation warnings for GTMRegex
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

@interface GTMRegexTest : GTMTestCase
@end

@interface NSString_GTMRegexAdditions : GTMTestCase
@end

@implementation GTMRegexTest

- (void)testEscapedPatternForString {
  XCTAssertEqualStrings([GTMRegex escapedPatternForString:@"abcdefghijklmnopqrstuvwxyz0123456789"],
                        @"abcdefghijklmnopqrstuvwxyz0123456789");
  XCTAssertEqualStrings([GTMRegex escapedPatternForString:@"^.[$()|*+?{\\"],
                        @"\\^\\.\\[\\$\\(\\)\\|\\*\\+\\?\\{\\\\");
  XCTAssertEqualStrings([GTMRegex escapedPatternForString:@"a^b.c[d$e(f)g|h*i+j?k{l\\m"],
                        @"a\\^b\\.c\\[d\\$e\\(f\\)g\\|h\\*i\\+j\\?k\\{l\\\\m");

  XCTAssertNil([GTMRegex escapedPatternForString:nil]);
  XCTAssertEqualStrings([GTMRegex escapedPatternForString:@""], @"");
}


- (void)testInit {

  // fail cases
  XCTAssertNil([[[GTMRegex alloc] init] autorelease]);
  XCTAssertNil([[[GTMRegex alloc] initWithPattern:nil] autorelease]);
  XCTAssertNil([[[GTMRegex alloc] initWithPattern:nil
                                          options:kGTMRegexOptionIgnoreCase] autorelease]);
  XCTAssertNil([[[GTMRegex alloc] initWithPattern:@"(."] autorelease]);
  XCTAssertNil([[[GTMRegex alloc] initWithPattern:@"(."
                                          options:kGTMRegexOptionIgnoreCase] autorelease]);
  // fail cases w/ error param
  NSError *error = nil;
  XCTAssertNil([[[GTMRegex alloc] initWithPattern:nil
                                          options:kGTMRegexOptionIgnoreCase
                                        withError:&error] autorelease]);
  XCTAssertNil(error, @"no pattern, shouldn't get error object");
  XCTAssertNil([[[GTMRegex alloc] initWithPattern:@"(."
                                          options:kGTMRegexOptionIgnoreCase
                                        withError:&error] autorelease]);
  XCTAssertNotNil(error);
  XCTAssertEqualObjects([error domain], kGTMRegexErrorDomain);
  XCTAssertEqual([error code], (NSInteger)kGTMRegexPatternParseFailedError);
  NSDictionary *userInfo = [error userInfo];
  XCTAssertNotNil(userInfo, @"failed to get userInfo from error");
  XCTAssertEqualObjects([userInfo objectForKey:kGTMRegexPatternErrorPattern], @"(.");
  XCTAssertNotNil([userInfo objectForKey:kGTMRegexPatternErrorErrorString]);

  // basic pattern w/ options
  XCTAssertNotNil([[[GTMRegex alloc] initWithPattern:@"(.*)"] autorelease]);
  XCTAssertNotNil([[[GTMRegex alloc] initWithPattern:@"(.*)"
                                             options:0] autorelease]);
  XCTAssertNotNil([[[GTMRegex alloc] initWithPattern:@"(.*)"
                                             options:kGTMRegexOptionIgnoreCase] autorelease]);
  error = nil;
  XCTAssertNotNil([[[GTMRegex alloc] initWithPattern:@"(.*)"
                                             options:kGTMRegexOptionIgnoreCase
                                           withError:&error] autorelease]);
  XCTAssertNil(error, @"shouldn't have been any error");

  // fail cases (helper)
  XCTAssertNil([GTMRegex regexWithPattern:nil]);
  XCTAssertNil([GTMRegex regexWithPattern:nil
                                  options:0]);
  XCTAssertNil([GTMRegex regexWithPattern:@"(."]);
  XCTAssertNil([GTMRegex regexWithPattern:@"(."
                                  options:0]);
  // fail cases (helper) w/ error param
  XCTAssertNil([GTMRegex regexWithPattern:nil
                                  options:kGTMRegexOptionIgnoreCase
                                withError:&error]);
  XCTAssertNil(error, @"no pattern, shouldn't get error object");
  XCTAssertNil([GTMRegex regexWithPattern:@"(."
                                  options:kGTMRegexOptionIgnoreCase
                                withError:&error]);
  XCTAssertNotNil(error);
  XCTAssertEqualObjects([error domain], kGTMRegexErrorDomain);
  XCTAssertEqual([error code], (NSInteger)kGTMRegexPatternParseFailedError);
  userInfo = [error userInfo];
  XCTAssertNotNil(userInfo, @"failed to get userInfo from error");
  XCTAssertEqualObjects([userInfo objectForKey:kGTMRegexPatternErrorPattern], @"(.");
  XCTAssertNotNil([userInfo objectForKey:kGTMRegexPatternErrorErrorString]);

  // basic pattern w/ options (helper)
  XCTAssertNotNil([GTMRegex regexWithPattern:@"(.*)"]);
  XCTAssertNotNil([GTMRegex regexWithPattern:@"(.*)"
                                     options:0]);
  XCTAssertNotNil([GTMRegex regexWithPattern:@"(.*)"
                                     options:kGTMRegexOptionIgnoreCase]);
  error = nil;
  XCTAssertNotNil([GTMRegex regexWithPattern:@"(.*)"
                                     options:kGTMRegexOptionIgnoreCase
                                   withError:&error]);
  XCTAssertNil(error, @"shouldn't have been any error");

  // not really a test on GTMRegex, but make sure we block attempts to directly
  // alloc/init a GTMRegexStringSegment.
  XCTAssertThrowsSpecificNamed([[[GTMRegexStringSegment alloc] init] autorelease],
                               NSException, NSInvalidArgumentException,
                               @"shouldn't have been able to alloc/init a GTMRegexStringSegment");
}

- (void)testOptions {

  NSString *testString = @"aaa AAA\nbbb BBB\n aaa aAa\n bbb BbB";

  // default options
  GTMRegex *regex = [GTMRegex regexWithPattern:@"a+"];
  XCTAssertNotNil(regex);
  NSEnumerator *enumerator = [regex segmentEnumeratorForString:testString];
  XCTAssertNotNil(enumerator);
  // "aaa"
  GTMRegexStringSegment *seg = [enumerator nextObject];
  XCTAssertNotNil(seg);
  XCTAssertTrue([seg isMatch]);
  XCTAssertEqualStrings([seg string], @"aaa");
  // " AAA\nbbb BBB\n "
  seg = [enumerator nextObject];
  XCTAssertNotNil(seg);
  XCTAssertFalse([seg isMatch]);
  XCTAssertEqualStrings([seg string], @" AAA\nbbb BBB\n ");
  // "aaa"
  seg = [enumerator nextObject];
  XCTAssertNotNil(seg);
  XCTAssertTrue([seg isMatch]);
  XCTAssertEqualStrings([seg string], @"aaa");
  // " "
  seg = [enumerator nextObject];
  XCTAssertNotNil(seg);
  XCTAssertFalse([seg isMatch]);
  XCTAssertEqualStrings([seg string], @" ");
  // "a"
  seg = [enumerator nextObject];
  XCTAssertNotNil(seg);
  XCTAssertTrue([seg isMatch]);
  XCTAssertEqualStrings([seg string], @"a");
  // "A"
  seg = [enumerator nextObject];
  XCTAssertNotNil(seg);
  XCTAssertFalse([seg isMatch]);
  XCTAssertEqualStrings([seg string], @"A");
  // "a"
  seg = [enumerator nextObject];
  XCTAssertNotNil(seg);
  XCTAssertTrue([seg isMatch]);
  XCTAssertEqualStrings([seg string], @"a");
  // "\n bbb BbB"
  seg = [enumerator nextObject];
  XCTAssertNotNil(seg);
  XCTAssertFalse([seg isMatch]);
  XCTAssertEqualStrings([seg string], @"\n bbb BbB");
  // (end)
  seg = [enumerator nextObject];
  XCTAssertNil(seg);

  // kGTMRegexOptionIgnoreCase
  regex = [GTMRegex regexWithPattern:@"a+" options:kGTMRegexOptionIgnoreCase];
  XCTAssertNotNil(regex);
  enumerator = [regex segmentEnumeratorForString:testString];
  XCTAssertNotNil(enumerator);
  // "aaa"
  seg = [enumerator nextObject];
  XCTAssertNotNil(seg);
  XCTAssertTrue([seg isMatch]);
  XCTAssertEqualStrings([seg string], @"aaa");
  // " "
  seg = [enumerator nextObject];
  XCTAssertNotNil(seg);
  XCTAssertFalse([seg isMatch]);
  XCTAssertEqualStrings([seg string], @" ");
  // "AAA"
  seg = [enumerator nextObject];
  XCTAssertNotNil(seg);
  XCTAssertTrue([seg isMatch]);
  XCTAssertEqualStrings([seg string], @"AAA");
  // "\nbbb BBB\n "
  seg = [enumerator nextObject];
  XCTAssertNotNil(seg);
  XCTAssertFalse([seg isMatch]);
  XCTAssertEqualStrings([seg string], @"\nbbb BBB\n ");
  // "aaa"
  seg = [enumerator nextObject];
  XCTAssertNotNil(seg);
  XCTAssertTrue([seg isMatch]);
  XCTAssertEqualStrings([seg string], @"aaa");
  // " "
  seg = [enumerator nextObject];
  XCTAssertNotNil(seg);
  XCTAssertFalse([seg isMatch]);
  XCTAssertEqualStrings([seg string], @" ");
  // "aAa"
  seg = [enumerator nextObject];
  XCTAssertNotNil(seg);
  XCTAssertTrue([seg isMatch]);
  XCTAssertEqualStrings([seg string], @"aAa");
  // "\n bbb BbB"
  seg = [enumerator nextObject];
  XCTAssertNotNil(seg);
  XCTAssertFalse([seg isMatch]);
  XCTAssertEqualStrings([seg string], @"\n bbb BbB");
  // (end)
  seg = [enumerator nextObject];
  XCTAssertNil(seg);

  // defaults w/ '^'
  regex = [GTMRegex regexWithPattern:@"^a+"];
  XCTAssertNotNil(regex);
  enumerator = [regex segmentEnumeratorForString:testString];
  XCTAssertNotNil(enumerator);
  // "aaa"
  seg = [enumerator nextObject];
  XCTAssertNotNil(seg);
  XCTAssertTrue([seg isMatch]);
  XCTAssertEqualStrings([seg string], @"aaa");
  // " AAA\nbbb BBB\n aaa aAa\n bbb BbB"
  seg = [enumerator nextObject];
  XCTAssertNotNil(seg);
  XCTAssertFalse([seg isMatch]);
  XCTAssertEqualStrings([seg string], @" AAA\nbbb BBB\n aaa aAa\n bbb BbB");
  // (end)
  seg = [enumerator nextObject];
  XCTAssertNil(seg);

  // defaults w/ '$'
  regex = [GTMRegex regexWithPattern:@"B+$"];
  XCTAssertNotNil(regex);
  enumerator = [regex segmentEnumeratorForString:testString];
  XCTAssertNotNil(enumerator);
  // "aaa AAA\nbbb "
  seg = [enumerator nextObject];
  XCTAssertNotNil(seg);
  XCTAssertFalse([seg isMatch]);
  XCTAssertEqualStrings([seg string], @"aaa AAA\nbbb ");
  // "BBB"
  seg = [enumerator nextObject];
  XCTAssertNotNil(seg);
  XCTAssertTrue([seg isMatch]);
  XCTAssertEqualStrings([seg string], @"BBB");
  // "\n aaa aAa\n bbb Bb"
  seg = [enumerator nextObject];
  XCTAssertNotNil(seg);
  XCTAssertFalse([seg isMatch]);
  XCTAssertEqualStrings([seg string], @"\n aaa aAa\n bbb Bb");
  // "B"
  seg = [enumerator nextObject];
  XCTAssertNotNil(seg);
  XCTAssertTrue([seg isMatch]);
  XCTAssertEqualStrings([seg string], @"B");
  // (end)
  seg = [enumerator nextObject];
  XCTAssertNil(seg);

  // kGTMRegexOptionIgnoreCase w/ '$'
  regex = [GTMRegex regexWithPattern:@"B+$"
                             options:kGTMRegexOptionIgnoreCase];
  XCTAssertNotNil(regex);
  enumerator = [regex segmentEnumeratorForString:testString];
  XCTAssertNotNil(enumerator);
  // "aaa AAA\nbbb "
  seg = [enumerator nextObject];
  XCTAssertNotNil(seg);
  XCTAssertFalse([seg isMatch]);
  XCTAssertEqualStrings([seg string], @"aaa AAA\nbbb ");
  // "BBB"
  seg = [enumerator nextObject];
  XCTAssertNotNil(seg);
  XCTAssertTrue([seg isMatch]);
  XCTAssertEqualStrings([seg string], @"BBB");
  // "\n aaa aAa\n bbb "
  seg = [enumerator nextObject];
  XCTAssertNotNil(seg);
  XCTAssertFalse([seg isMatch]);
  XCTAssertEqualStrings([seg string], @"\n aaa aAa\n bbb ");
  // "BbB"
  seg = [enumerator nextObject];
  XCTAssertNotNil(seg);
  XCTAssertTrue([seg isMatch]);
  XCTAssertEqualStrings([seg string], @"BbB");
  // (end)
  seg = [enumerator nextObject];
  XCTAssertNil(seg);

  // test w/ kGTMRegexOptionSupressNewlineSupport and \n in the string
  regex = [GTMRegex regexWithPattern:@"a.*b" options:kGTMRegexOptionSupressNewlineSupport];
  XCTAssertNotNil(regex);
  enumerator = [regex segmentEnumeratorForString:testString];
  XCTAssertNotNil(enumerator);
  // "aaa AAA\nbbb BBB\n aaa aAa\n bbb Bb"
  seg = [enumerator nextObject];
  XCTAssertNotNil(seg);
  XCTAssertTrue([seg isMatch]);
  XCTAssertEqualStrings([seg string], @"aaa AAA\nbbb BBB\n aaa aAa\n bbb Bb");
  // "B"
  seg = [enumerator nextObject];
  XCTAssertNotNil(seg);
  XCTAssertFalse([seg isMatch]);
  XCTAssertEqualStrings([seg string], @"B");
  // (end)
  seg = [enumerator nextObject];
  XCTAssertNil(seg);

  // test w/o kGTMRegexOptionSupressNewlineSupport and \n in the string
  // (this is no match since it '.' can't match the '\n')
  regex = [GTMRegex regexWithPattern:@"a.*b"];
  XCTAssertNotNil(regex);
  enumerator = [regex segmentEnumeratorForString:testString];
  XCTAssertNotNil(enumerator);
  // "aaa AAA\nbbb BBB\n aaa aAa\n bbb BbB"
  seg = [enumerator nextObject];
  XCTAssertNotNil(seg);
  XCTAssertFalse([seg isMatch]);
  XCTAssertEqualStrings([seg string], @"aaa AAA\nbbb BBB\n aaa aAa\n bbb BbB");
  // (end)
  seg = [enumerator nextObject];
  XCTAssertNil(seg);

  // kGTMRegexOptionSupressNewlineSupport w/ '^'
  regex = [GTMRegex regexWithPattern:@"^a+" options:kGTMRegexOptionSupressNewlineSupport];
  XCTAssertNotNil(regex);
  enumerator = [regex segmentEnumeratorForString:testString];
  XCTAssertNotNil(enumerator);
  // "aaa"
  seg = [enumerator nextObject];
  XCTAssertNotNil(seg);
  XCTAssertTrue([seg isMatch]);
  XCTAssertEqualStrings([seg string], @"aaa");
  // " AAA\nbbb BBB\n aaa aAa\n bbb BbB"
  seg = [enumerator nextObject];
  XCTAssertNotNil(seg);
  XCTAssertFalse([seg isMatch]);
  XCTAssertEqualStrings([seg string], @" AAA\nbbb BBB\n aaa aAa\n bbb BbB");
  // (end)
  seg = [enumerator nextObject];
  XCTAssertNil(seg);

  // kGTMRegexOptionSupressNewlineSupport w/ '$'
  regex = [GTMRegex regexWithPattern:@"B+$" options:kGTMRegexOptionSupressNewlineSupport];
  XCTAssertNotNil(regex);
  enumerator = [regex segmentEnumeratorForString:testString];
  XCTAssertNotNil(enumerator);
  // "aaa AAA\nbbb BBB\n aaa aAa\n bbb Bb"
  seg = [enumerator nextObject];
  XCTAssertNotNil(seg);
  XCTAssertFalse([seg isMatch]);
  XCTAssertEqualStrings([seg string], @"aaa AAA\nbbb BBB\n aaa aAa\n bbb Bb");
  // "B"
  seg = [enumerator nextObject];
  XCTAssertNotNil(seg);
  XCTAssertTrue([seg isMatch]);
  XCTAssertEqualStrings([seg string], @"B");
  // (end)
  seg = [enumerator nextObject];
  XCTAssertNil(seg);
}

- (void)testSubPatternCount {
  XCTAssertEqual((NSUInteger)0, [[GTMRegex regexWithPattern:@".*"] subPatternCount]);
  XCTAssertEqual((NSUInteger)1, [[GTMRegex regexWithPattern:@"(.*)"] subPatternCount]);
  XCTAssertEqual((NSUInteger)1, [[GTMRegex regexWithPattern:@"[fo]*(.*)[bar]*"] subPatternCount]);
  XCTAssertEqual((NSUInteger)3,
                 [[GTMRegex regexWithPattern:@"([fo]*)(.*)([bar]*)"] subPatternCount]);
  XCTAssertEqual((NSUInteger)7,
                 [[GTMRegex regexWithPattern:@"(([bar]*)|([fo]*))(.*)(([bar]*)|([fo]*))"] subPatternCount]);
}

- (void)testMatchesString {
  // simple pattern
  GTMRegex *regex = [GTMRegex regexWithPattern:@"foo.*bar"];
  XCTAssertNotNil(regex);
  XCTAssertTrue([regex matchesString:@"foobar"]);
  XCTAssertTrue([regex matchesString:@"foobydoo spambar"]);
  XCTAssertFalse([regex matchesString:@"zzfoobarzz"]);
  XCTAssertFalse([regex matchesString:@"zzfoobydoo spambarzz"]);
  XCTAssertFalse([regex matchesString:@"abcdef"]);
  XCTAssertFalse([regex matchesString:@""]);
  XCTAssertFalse([regex matchesString:nil]);
  // pattern w/ sub patterns
  regex = [GTMRegex regexWithPattern:@"(foo)(.*)(bar)"];
  XCTAssertNotNil(regex);
  XCTAssertTrue([regex matchesString:@"foobar"]);
  XCTAssertTrue([regex matchesString:@"foobydoo spambar"]);
  XCTAssertFalse([regex matchesString:@"zzfoobarzz"]);
  XCTAssertFalse([regex matchesString:@"zzfoobydoo spambarzz"]);
  XCTAssertFalse([regex matchesString:@"abcdef"]);
  XCTAssertFalse([regex matchesString:@""]);
  XCTAssertFalse([regex matchesString:nil]);
}

- (void)testSubPatternsOfString {
  GTMRegex *regex = [GTMRegex regexWithPattern:@"(fo(o+))((bar)|(baz))"];
  XCTAssertNotNil(regex);
  XCTAssertEqual((NSUInteger)5, [regex subPatternCount]);
  NSArray *subPatterns = [regex subPatternsOfString:@"foooooobaz"];
  XCTAssertNotNil(subPatterns);
  XCTAssertEqual((NSUInteger)6, [subPatterns count]);
  XCTAssertEqualStrings(@"foooooobaz", [subPatterns objectAtIndex:0]);
  XCTAssertEqualStrings(@"foooooo", [subPatterns objectAtIndex:1]);
  XCTAssertEqualStrings(@"ooooo", [subPatterns objectAtIndex:2]);
  XCTAssertEqualStrings(@"baz", [subPatterns objectAtIndex:3]);
  XCTAssertEqualObjects([NSNull null], [subPatterns objectAtIndex:4]);
  XCTAssertEqualStrings(@"baz", [subPatterns objectAtIndex:5]);

  // not there
  subPatterns = [regex subPatternsOfString:@"aaa"];
  XCTAssertNil(subPatterns);

  // not extra stuff on either end
  subPatterns = [regex subPatternsOfString:@"ZZZfoooooobaz"];
  XCTAssertNil(subPatterns);
  subPatterns = [regex subPatternsOfString:@"foooooobazZZZ"];
  XCTAssertNil(subPatterns);
  subPatterns = [regex subPatternsOfString:@"ZZZfoooooobazZZZ"];
  XCTAssertNil(subPatterns);
}

- (void)testFirstSubStringMatchedInString {
  // simple pattern
  GTMRegex *regex = [GTMRegex regexWithPattern:@"foo.*bar"];
  XCTAssertNotNil(regex);
  XCTAssertEqualStrings([regex firstSubStringMatchedInString:@"foobar"],
                        @"foobar");
  XCTAssertEqualStrings([regex firstSubStringMatchedInString:@"foobydoo spambar"],
                        @"foobydoo spambar");
  XCTAssertEqualStrings([regex firstSubStringMatchedInString:@"zzfoobarzz"],
                        @"foobar");
  XCTAssertEqualStrings([regex firstSubStringMatchedInString:@"zzfoobydoo spambarzz"],
                        @"foobydoo spambar");
  XCTAssertNil([regex firstSubStringMatchedInString:@"abcdef"]);
  XCTAssertNil([regex firstSubStringMatchedInString:@""]);
  // pattern w/ sub patterns
  regex = [GTMRegex regexWithPattern:@"(foo)(.*)(bar)"];
  XCTAssertNotNil(regex);
  XCTAssertEqualStrings([regex firstSubStringMatchedInString:@"foobar"],
                        @"foobar");
  XCTAssertEqualStrings([regex firstSubStringMatchedInString:@"foobydoo spambar"],
                        @"foobydoo spambar");
  XCTAssertEqualStrings([regex firstSubStringMatchedInString:@"zzfoobarzz"],
                        @"foobar");
  XCTAssertEqualStrings([regex firstSubStringMatchedInString:@"zzfoobydoo spambarzz"],
                        @"foobydoo spambar");
  XCTAssertNil([regex firstSubStringMatchedInString:@"abcdef"]);
  XCTAssertNil([regex firstSubStringMatchedInString:@""]);
}

- (void)testMatchesSubStringInString {
  // simple pattern
  GTMRegex *regex = [GTMRegex regexWithPattern:@"foo.*bar"];
  XCTAssertNotNil(regex);
  XCTAssertTrue([regex matchesSubStringInString:@"foobar"]);
  XCTAssertTrue([regex matchesSubStringInString:@"foobydoo spambar"]);
  XCTAssertTrue([regex matchesSubStringInString:@"zzfoobarzz"]);
  XCTAssertTrue([regex matchesSubStringInString:@"zzfoobydoo spambarzz"]);
  XCTAssertFalse([regex matchesSubStringInString:@"abcdef"]);
  XCTAssertFalse([regex matchesSubStringInString:@""]);
  // pattern w/ sub patterns
  regex = [GTMRegex regexWithPattern:@"(foo)(.*)(bar)"];
  XCTAssertNotNil(regex);
  XCTAssertTrue([regex matchesSubStringInString:@"foobar"]);
  XCTAssertTrue([regex matchesSubStringInString:@"foobydoo spambar"]);
  XCTAssertTrue([regex matchesSubStringInString:@"zzfoobarzz"]);
  XCTAssertTrue([regex matchesSubStringInString:@"zzfoobydoo spambarzz"]);
  XCTAssertFalse([regex matchesSubStringInString:@"abcdef"]);
  XCTAssertFalse([regex matchesSubStringInString:@""]);
}

- (void)testSegmentEnumeratorForString {
  GTMRegex *regex = [GTMRegex regexWithPattern:@"foo+ba+r"];
  XCTAssertNotNil(regex);

  // test odd input
  NSEnumerator *enumerator = [regex segmentEnumeratorForString:@""];
  XCTAssertNotNil(enumerator);
  enumerator = [regex segmentEnumeratorForString:nil];
  XCTAssertNil(enumerator);

  // on w/ the normal tests
  enumerator = [regex segmentEnumeratorForString:@"afoobarbfooobaarfoobarzz"];
  XCTAssertNotNil(enumerator);
  // "a"
  GTMRegexStringSegment *seg = [enumerator nextObject];
  XCTAssertNotNil(seg);
  XCTAssertFalse([seg isMatch]);
  XCTAssertEqualStrings([seg string], @"a");
  // "foobar"
  seg = [enumerator nextObject];
  XCTAssertNotNil(seg);
  XCTAssertTrue([seg isMatch]);
  XCTAssertEqualStrings([seg string], @"foobar");
  // "b"
  seg = [enumerator nextObject];
  XCTAssertNotNil(seg);
  XCTAssertFalse([seg isMatch]);
  XCTAssertEqualStrings([seg string], @"b");
  // "fooobaar"
  seg = [enumerator nextObject];
  XCTAssertNotNil(seg);
  XCTAssertTrue([seg isMatch]);
  XCTAssertEqualStrings([seg string], @"fooobaar");
  // "foobar"
  seg = [enumerator nextObject];
  XCTAssertNotNil(seg);
  XCTAssertTrue([seg isMatch]);
  XCTAssertEqualStrings([seg string], @"foobar");
  // "zz"
  seg = [enumerator nextObject];
  XCTAssertNotNil(seg);
  XCTAssertFalse([seg isMatch]);
  XCTAssertEqualStrings([seg string], @"zz");
  // (end)
  seg = [enumerator nextObject];
  XCTAssertNil(seg);

  // test no match
  enumerator = [regex segmentEnumeratorForString:@"aaa"];
  XCTAssertNotNil(enumerator);
  seg = [enumerator nextObject];
  XCTAssertNotNil(seg);
  XCTAssertFalse([seg isMatch]);
  XCTAssertEqualStrings([seg string], @"aaa");
  seg = [enumerator nextObject];
  XCTAssertNil(seg);

  // test only match
  enumerator = [regex segmentEnumeratorForString:@"foobar"];
  XCTAssertNotNil(enumerator);
  seg = [enumerator nextObject];
  XCTAssertNotNil(seg);
  XCTAssertTrue([seg isMatch]);
  XCTAssertEqualStrings([seg string], @"foobar");
  seg = [enumerator nextObject];
  XCTAssertNil(seg);

  // now test the saved sub segments
  regex = [GTMRegex regexWithPattern:@"(foo)((bar)|(baz))"];
  XCTAssertNotNil(regex);
  XCTAssertEqual((NSUInteger)4, [regex subPatternCount]);
  enumerator = [regex segmentEnumeratorForString:@"foobarxxfoobaz"];
  XCTAssertNotNil(enumerator);
  // "foobar"
  seg = [enumerator nextObject];
  XCTAssertNotNil(seg);
  XCTAssertTrue([seg isMatch]);
  XCTAssertEqualStrings([seg string], @"foobar");
  XCTAssertEqualStrings([seg subPatternString:0], @"foobar");
  XCTAssertEqualStrings([seg subPatternString:1], @"foo");
  XCTAssertEqualStrings([seg subPatternString:2], @"bar");
  XCTAssertEqualStrings([seg subPatternString:3], @"bar");
  XCTAssertNil([seg subPatternString:4]); // nothing matched "(baz)"
  XCTAssertNil([seg subPatternString:5]);
  // "xx"
  seg = [enumerator nextObject];
  XCTAssertNotNil(seg);
  XCTAssertFalse([seg isMatch]);
  XCTAssertEqualStrings([seg string], @"xx");
  XCTAssertEqualStrings([seg subPatternString:0], @"xx");
  XCTAssertNil([seg subPatternString:1]);
  // "foobaz"
  seg = [enumerator nextObject];
  XCTAssertNotNil(seg);
  XCTAssertTrue([seg isMatch]);
  XCTAssertEqualStrings([seg string], @"foobaz");
  XCTAssertEqualStrings([seg subPatternString:0], @"foobaz");
  XCTAssertEqualStrings([seg subPatternString:1], @"foo");
  XCTAssertEqualStrings([seg subPatternString:2], @"baz");
  XCTAssertNil([seg subPatternString:3]); // (nothing matched "(bar)"
  XCTAssertEqualStrings([seg subPatternString:4], @"baz");
  XCTAssertNil([seg subPatternString:5]);
  // (end)
  seg = [enumerator nextObject];
  XCTAssertNil(seg);

  // test all objects
  regex = [GTMRegex regexWithPattern:@"foo+ba+r"];
  XCTAssertNotNil(regex);
  enumerator = [regex segmentEnumeratorForString:@"afoobarbfooobaarfoobarzz"];
  XCTAssertNotNil(enumerator);
  NSArray *allSegments = [enumerator allObjects];
  XCTAssertNotNil(allSegments);
  XCTAssertEqual((NSUInteger)6, [allSegments count]);

  // test we are getting the flags right for newline
  regex = [GTMRegex regexWithPattern:@"^a"];
  XCTAssertNotNil(regex);
  enumerator = [regex segmentEnumeratorForString:@"aa\naa"];
  XCTAssertNotNil(enumerator);
  // "a"
  seg = [enumerator nextObject];
  XCTAssertNotNil(seg);
  XCTAssertTrue([seg isMatch]);
  XCTAssertEqualStrings([seg string], @"a");
  // "a\n"
  seg = [enumerator nextObject];
  XCTAssertNotNil(seg);
  XCTAssertFalse([seg isMatch]);
  XCTAssertEqualStrings([seg string], @"a\n");
  // "a"
  seg = [enumerator nextObject];
  XCTAssertNotNil(seg);
  XCTAssertTrue([seg isMatch]);
  XCTAssertEqualStrings([seg string], @"a");
  // "a"
  seg = [enumerator nextObject];
  XCTAssertNotNil(seg);
  XCTAssertFalse([seg isMatch]);
  XCTAssertEqualStrings([seg string], @"a");
  // (end)
  seg = [enumerator nextObject];
  XCTAssertNil(seg);

  // test we are getting the flags right for newline, part 2
  regex = [GTMRegex regexWithPattern:@"^a*$"];
  XCTAssertNotNil(regex);
  enumerator = [regex segmentEnumeratorForString:@"aa\naa\nbb\naa"];
  XCTAssertNotNil(enumerator);
  // "aa"
  seg = [enumerator nextObject];
  XCTAssertNotNil(seg);
  XCTAssertTrue([seg isMatch]);
  XCTAssertEqualStrings([seg string], @"aa");
  // "\n"
  seg = [enumerator nextObject];
  XCTAssertNotNil(seg);
  XCTAssertFalse([seg isMatch]);
  XCTAssertEqualStrings([seg string], @"\n");
  // "aa"
  seg = [enumerator nextObject];
  XCTAssertNotNil(seg);
  XCTAssertTrue([seg isMatch]);
  XCTAssertEqualStrings([seg string], @"aa");
  // "\nbb\n"
  seg = [enumerator nextObject];
  XCTAssertNotNil(seg);
  XCTAssertFalse([seg isMatch]);
  XCTAssertEqualStrings([seg string], @"\nbb\n");
  // "aa"
  seg = [enumerator nextObject];
  XCTAssertNotNil(seg);
  XCTAssertTrue([seg isMatch]);
  XCTAssertEqualStrings([seg string], @"aa");
  // (end)
  seg = [enumerator nextObject];
  XCTAssertNil(seg);

  // make sure the enum cleans up if not walked to the end
  regex = [GTMRegex regexWithPattern:@"b+"];
  XCTAssertNotNil(regex);
  enumerator = [regex segmentEnumeratorForString:@"aabbcc"];
  XCTAssertNotNil(enumerator);
  // "aa"
  seg = [enumerator nextObject];
  XCTAssertNotNil(seg);
  XCTAssertFalse([seg isMatch]);
  XCTAssertEqualStrings([seg string], @"aa");
  // and done w/o walking the rest
}

- (void)testMatchSegmentEnumeratorForString {
  GTMRegex *regex = [GTMRegex regexWithPattern:@"foo+ba+r"];
  XCTAssertNotNil(regex);

  // test odd input
  NSEnumerator *enumerator = [regex matchSegmentEnumeratorForString:@""];
  XCTAssertNotNil(enumerator);
  enumerator = [regex matchSegmentEnumeratorForString:nil];
  XCTAssertNil(enumerator);

  // on w/ the normal tests
  enumerator = [regex matchSegmentEnumeratorForString:@"afoobarbfooobaarfoobarzz"];
  XCTAssertNotNil(enumerator);
  // "a" - skipped
  // "foobar"
  GTMRegexStringSegment *seg = [enumerator nextObject];
  XCTAssertNotNil(seg);
  XCTAssertTrue([seg isMatch]);
  XCTAssertEqualStrings([seg string], @"foobar");
  // "b" - skipped
  // "fooobaar"
  seg = [enumerator nextObject];
  XCTAssertNotNil(seg);
  XCTAssertTrue([seg isMatch]);
  XCTAssertEqualStrings([seg string], @"fooobaar");
  // "foobar"
  seg = [enumerator nextObject];
  XCTAssertNotNil(seg);
  XCTAssertTrue([seg isMatch]);
  XCTAssertEqualStrings([seg string], @"foobar");
  // "zz" - skipped
  // (end)
  seg = [enumerator nextObject];
  XCTAssertNil(seg);

  // test no match
  enumerator = [regex matchSegmentEnumeratorForString:@"aaa"];
  XCTAssertNotNil(enumerator);
  seg = [enumerator nextObject];
  XCTAssertNil(seg); // should have gotten nothing

  // test only match
  enumerator = [regex matchSegmentEnumeratorForString:@"foobar"];
  XCTAssertNotNil(enumerator);
  seg = [enumerator nextObject];
  XCTAssertNotNil(seg);
  XCTAssertTrue([seg isMatch]);
  XCTAssertEqualStrings([seg string], @"foobar");
  seg = [enumerator nextObject];
  XCTAssertNil(seg);

  // now test the saved sub segments
  regex = [GTMRegex regexWithPattern:@"(foo)((bar)|(baz))"];
  XCTAssertNotNil(regex);
  XCTAssertEqual((NSUInteger)4, [regex subPatternCount]);
  enumerator = [regex matchSegmentEnumeratorForString:@"foobarxxfoobaz"];
  XCTAssertNotNil(enumerator);
  // "foobar"
  seg = [enumerator nextObject];
  XCTAssertNotNil(seg);
  XCTAssertTrue([seg isMatch]);
  XCTAssertEqualStrings([seg string], @"foobar");
  XCTAssertEqualStrings([seg subPatternString:0], @"foobar");
  XCTAssertEqualStrings([seg subPatternString:1], @"foo");
  XCTAssertEqualStrings([seg subPatternString:2], @"bar");
  XCTAssertEqualStrings([seg subPatternString:3], @"bar");
  XCTAssertNil([seg subPatternString:4]); // nothing matched "(baz)"
  XCTAssertNil([seg subPatternString:5]);
  // "xx" - skipped
  // "foobaz"
  seg = [enumerator nextObject];
  XCTAssertNotNil(seg);
  XCTAssertTrue([seg isMatch]);
  XCTAssertEqualStrings([seg string], @"foobaz");
  XCTAssertEqualStrings([seg subPatternString:0], @"foobaz");
  XCTAssertEqualStrings([seg subPatternString:1], @"foo");
  XCTAssertEqualStrings([seg subPatternString:2], @"baz");
  XCTAssertNil([seg subPatternString:3]); // (nothing matched "(bar)"
  XCTAssertEqualStrings([seg subPatternString:4], @"baz");
  XCTAssertNil([seg subPatternString:5]);
  // (end)
  seg = [enumerator nextObject];
  XCTAssertNil(seg);

  // test all objects
  regex = [GTMRegex regexWithPattern:@"foo+ba+r"];
  XCTAssertNotNil(regex);
  enumerator = [regex matchSegmentEnumeratorForString:@"afoobarbfooobaarfoobarzz"];
  XCTAssertNotNil(enumerator);
  NSArray *allSegments = [enumerator allObjects];
  XCTAssertNotNil(allSegments);
  XCTAssertEqual((NSUInteger)3, [allSegments count]);

  // test we are getting the flags right for newline
  regex = [GTMRegex regexWithPattern:@"^a"];
  XCTAssertNotNil(regex);
  enumerator = [regex matchSegmentEnumeratorForString:@"aa\naa"];
  XCTAssertNotNil(enumerator);
  // "a"
  seg = [enumerator nextObject];
  XCTAssertNotNil(seg);
  XCTAssertTrue([seg isMatch]);
  XCTAssertEqualStrings([seg string], @"a");
  // "a\n" - skipped
  // "a"
  seg = [enumerator nextObject];
  XCTAssertNotNil(seg);
  XCTAssertTrue([seg isMatch]);
  XCTAssertEqualStrings([seg string], @"a");
  // "a" - skipped
  // (end)
  seg = [enumerator nextObject];
  XCTAssertNil(seg);

  // test we are getting the flags right for newline, part 2
  regex = [GTMRegex regexWithPattern:@"^a*$"];
  XCTAssertNotNil(regex);
  enumerator = [regex matchSegmentEnumeratorForString:@"aa\naa\nbb\naa"];
  XCTAssertNotNil(enumerator);
  // "aa"
  seg = [enumerator nextObject];
  XCTAssertNotNil(seg);
  XCTAssertTrue([seg isMatch]);
  XCTAssertEqualStrings([seg string], @"aa");
  // "\n" - skipped
  // "aa"
  seg = [enumerator nextObject];
  XCTAssertNotNil(seg);
  XCTAssertTrue([seg isMatch]);
  XCTAssertEqualStrings([seg string], @"aa");
  // "\nbb\n" - skipped
  // "aa"
  seg = [enumerator nextObject];
  XCTAssertNotNil(seg);
  XCTAssertTrue([seg isMatch]);
  XCTAssertEqualStrings([seg string], @"aa");
  // (end)
  seg = [enumerator nextObject];
  XCTAssertNil(seg);
}

- (void)testStringByReplacingMatchesInStringWithReplacement {
  GTMRegex *regex = [GTMRegex regexWithPattern:@"(foo)(.*)(bar)"];
  XCTAssertNotNil(regex);
  // the basics
  XCTAssertEqualStrings(@"weeZbarZbydoo spamZfooZdoggies",
                        [regex stringByReplacingMatchesInString:@"weefoobydoo spambardoggies"
                                                withReplacement:@"Z\\3Z\\2Z\\1Z"]);
  // nil/empty replacement
  XCTAssertEqualStrings(@"weedoggies",
                        [regex stringByReplacingMatchesInString:@"weefoobydoo spambardoggies"
                                                withReplacement:nil]);
  XCTAssertEqualStrings(@"weedoggies",
                        [regex stringByReplacingMatchesInString:@"weefoobydoo spambardoggies"
                                                withReplacement:@""]);
  XCTAssertEqualStrings(@"",
                        [regex stringByReplacingMatchesInString:@""
                                                withReplacement:@"abc"]);
  XCTAssertNil([regex stringByReplacingMatchesInString:nil
                                       withReplacement:@"abc"]);
  // use optional and invale subexpression parts to confirm that works
  regex = [GTMRegex regexWithPattern:@"(fo(o+))((bar)|(baz))"];
  XCTAssertNotNil(regex);
  XCTAssertEqualStrings(@"aaa baz bar bar foo baz aaa",
                        [regex stringByReplacingMatchesInString:@"aaa foooooobaz fooobar bar foo baz aaa"
                                                withReplacement:@"\\4\\5"]);
  XCTAssertEqualStrings(@"aaa ZZZ ZZZ bar foo baz aaa",
                        [regex stringByReplacingMatchesInString:@"aaa foooooobaz fooobar bar foo baz aaa"
                                                withReplacement:@"Z\\10Z\\12Z"]);
  // test slashes in replacement that aren't part of the subpattern reference
  regex = [GTMRegex regexWithPattern:@"a+"];
  XCTAssertNotNil(regex);
  XCTAssertEqualStrings(@"z\\\\0 \\\\a \\\\\\\\0z",
                        [regex stringByReplacingMatchesInString:@"zaz"
                                                withReplacement:@"\\\\0 \\\\\\0 \\\\\\\\0"]);
  XCTAssertEqualStrings(@"z\\\\a \\\\\\\\0 \\\\\\\\az",
                        [regex stringByReplacingMatchesInString:@"zaz"
                                                withReplacement:@"\\\\\\0 \\\\\\\\0 \\\\\\\\\\0"]);
  XCTAssertEqualStrings(@"z\\\\\\\\0 \\\\\\\\a \\\\\\\\\\\\0z",
                        [regex stringByReplacingMatchesInString:@"zaz"
                                                withReplacement:@"\\\\\\\\0 \\\\\\\\\\0 \\\\\\\\\\\\0"]);
}

- (void)testDescriptions {
  // default options
  GTMRegex *regex = [GTMRegex regexWithPattern:@"a+"];
  XCTAssertNotNil(regex);
  XCTAssertGreaterThan([[regex description] length], (NSUInteger)10,
                       @"failed to get a reasonable description for regex");
  // enumerator
  NSEnumerator *enumerator = [regex segmentEnumeratorForString:@"aaabbbccc"];
  XCTAssertNotNil(enumerator);
  XCTAssertGreaterThan([[enumerator description] length], (NSUInteger)10,
                       @"failed to get a reasonable description for regex enumerator");
  // string segment
  GTMRegexStringSegment *seg = [enumerator nextObject];
  XCTAssertNotNil(seg);
  XCTAssertGreaterThan([[seg description] length], (NSUInteger)10,
                       @"failed to get a reasonable description for regex string segment");
  // regex w/ other options
  regex = [GTMRegex regexWithPattern:@"a+"
                             options:(kGTMRegexOptionIgnoreCase | kGTMRegexOptionSupressNewlineSupport)];
  XCTAssertNotNil(regex);
  XCTAssertGreaterThan([[regex description] length], (NSUInteger)10,
                       @"failed to get a reasonable description for regex w/ options");
}

@end

@implementation NSString_GTMRegexAdditions
// Only partial tests to test that the call get through correctly since the
// above really tests them.

- (void)testMatchesPattern {
  // simple pattern
  XCTAssertTrue([@"foobar" gtm_matchesPattern:@"foo.*bar"]);
  XCTAssertTrue([@"foobydoo spambar" gtm_matchesPattern:@"foo.*bar"]);
  XCTAssertFalse([@"zzfoobarzz" gtm_matchesPattern:@"foo.*bar"]);
  XCTAssertFalse([@"zzfoobydoo spambarzz" gtm_matchesPattern:@"foo.*bar"]);
  XCTAssertFalse([@"abcdef" gtm_matchesPattern:@"foo.*bar"]);
  XCTAssertFalse([@"" gtm_matchesPattern:@"foo.*bar"]);
  // pattern w/ sub patterns
  XCTAssertTrue([@"foobar" gtm_matchesPattern:@"(foo)(.*)(bar)"]);
  XCTAssertTrue([@"foobydoo spambar" gtm_matchesPattern:@"(foo)(.*)(bar)"]);
  XCTAssertFalse([@"zzfoobarzz" gtm_matchesPattern:@"(foo)(.*)(bar)"]);
  XCTAssertFalse([@"zzfoobydoo spambarzz" gtm_matchesPattern:@"(foo)(.*)(bar)"]);
  XCTAssertFalse([@"abcdef" gtm_matchesPattern:@"(foo)(.*)(bar)"]);
  XCTAssertFalse([@"" gtm_matchesPattern:@"(foo)(.*)(bar)"]);
}

- (void)testSubPatternsOfPattern {
  NSArray *subPatterns = [@"foooooobaz" gtm_subPatternsOfPattern:@"(fo(o+))((bar)|(baz))"];
  XCTAssertNotNil(subPatterns);
  XCTAssertEqual((NSUInteger)6, [subPatterns count]);
  XCTAssertEqualStrings(@"foooooobaz", [subPatterns objectAtIndex:0]);
  XCTAssertEqualStrings(@"foooooo", [subPatterns objectAtIndex:1]);
  XCTAssertEqualStrings(@"ooooo", [subPatterns objectAtIndex:2]);
  XCTAssertEqualStrings(@"baz", [subPatterns objectAtIndex:3]);
  XCTAssertEqualObjects([NSNull null], [subPatterns objectAtIndex:4]);
  XCTAssertEqualStrings(@"baz", [subPatterns objectAtIndex:5]);

  // not there
  subPatterns = [@"aaa" gtm_subPatternsOfPattern:@"(fo(o+))((bar)|(baz))"];
  XCTAssertNil(subPatterns);

  // not extra stuff on either end
  subPatterns = [@"ZZZfoooooobaz" gtm_subPatternsOfPattern:@"(fo(o+))((bar)|(baz))"];
  XCTAssertNil(subPatterns);
  subPatterns = [@"foooooobazZZZ" gtm_subPatternsOfPattern:@"(fo(o+))((bar)|(baz))"];
  XCTAssertNil(subPatterns);
  subPatterns = [@"ZZZfoooooobazZZZ" gtm_subPatternsOfPattern:@"(fo(o+))((bar)|(baz))"];
  XCTAssertNil(subPatterns);
}

- (void)testFirstSubStringMatchedByPattern {
  // simple pattern
  XCTAssertEqualStrings([@"foobar" gtm_firstSubStringMatchedByPattern:@"foo.*bar"],
                        @"foobar");
  XCTAssertEqualStrings([@"foobydoo spambar" gtm_firstSubStringMatchedByPattern:@"foo.*bar"],
                        @"foobydoo spambar");
  XCTAssertEqualStrings([@"zzfoobarzz" gtm_firstSubStringMatchedByPattern:@"foo.*bar"],
                        @"foobar");
  XCTAssertEqualStrings([@"zzfoobydoo spambarzz" gtm_firstSubStringMatchedByPattern:@"foo.*bar"],
                        @"foobydoo spambar");
  XCTAssertNil([@"abcdef" gtm_firstSubStringMatchedByPattern:@"foo.*bar"]);
  XCTAssertNil([@"" gtm_firstSubStringMatchedByPattern:@"foo.*bar"]);
  // pattern w/ sub patterns
  XCTAssertEqualStrings([@"foobar" gtm_firstSubStringMatchedByPattern:@"(foo)(.*)(bar)"],
                        @"foobar");
  XCTAssertEqualStrings([@"foobydoo spambar" gtm_firstSubStringMatchedByPattern:@"(foo)(.*)(bar)"],
                        @"foobydoo spambar");
  XCTAssertEqualStrings([@"zzfoobarzz" gtm_firstSubStringMatchedByPattern:@"(foo)(.*)(bar)"],
                        @"foobar");
  XCTAssertEqualStrings([@"zzfoobydoo spambarzz" gtm_firstSubStringMatchedByPattern:@"(foo)(.*)(bar)"],
                        @"foobydoo spambar");
  XCTAssertNil([@"abcdef" gtm_firstSubStringMatchedByPattern:@"(foo)(.*)(bar)"]);
  XCTAssertNil([@"" gtm_firstSubStringMatchedByPattern:@"(foo)(.*)(bar)"]);
}

- (void)testSubStringMatchesPattern {
  // simple pattern
  XCTAssertTrue([@"foobar" gtm_subStringMatchesPattern:@"foo.*bar"]);
  XCTAssertTrue([@"foobydoo spambar" gtm_subStringMatchesPattern:@"foo.*bar"]);
  XCTAssertTrue([@"zzfoobarzz" gtm_subStringMatchesPattern:@"foo.*bar"]);
  XCTAssertTrue([@"zzfoobydoo spambarzz" gtm_subStringMatchesPattern:@"foo.*bar"]);
  XCTAssertFalse([@"abcdef" gtm_subStringMatchesPattern:@"foo.*bar"]);
  XCTAssertFalse([@"" gtm_subStringMatchesPattern:@"foo.*bar"]);
  // pattern w/ sub patterns
  XCTAssertTrue([@"foobar" gtm_subStringMatchesPattern:@"(foo)(.*)(bar)"]);
  XCTAssertTrue([@"foobydoo spambar" gtm_subStringMatchesPattern:@"(foo)(.*)(bar)"]);
  XCTAssertTrue([@"zzfoobarzz" gtm_subStringMatchesPattern:@"(foo)(.*)(bar)"]);
  XCTAssertTrue([@"zzfoobydoo spambarzz" gtm_subStringMatchesPattern:@"(foo)(.*)(bar)"]);
  XCTAssertFalse([@"abcdef" gtm_subStringMatchesPattern:@"(foo)(.*)(bar)"]);
  XCTAssertFalse([@"" gtm_subStringMatchesPattern:@"(foo)(.*)(bar)"]);
}

- (void)testSegmentEnumeratorForPattern {
  NSEnumerator *enumerator =
    [@"afoobarbfooobaarfoobarzz" gtm_segmentEnumeratorForPattern:@"foo+ba+r"];
  XCTAssertNotNil(enumerator);
  // "a"
  GTMRegexStringSegment *seg = [enumerator nextObject];
  XCTAssertNotNil(seg);
  XCTAssertFalse([seg isMatch]);
  XCTAssertEqualStrings([seg string], @"a");
  // "foobar"
  seg = [enumerator nextObject];
  XCTAssertNotNil(seg);
  XCTAssertTrue([seg isMatch]);
  XCTAssertEqualStrings([seg string], @"foobar");
  // "b"
  seg = [enumerator nextObject];
  XCTAssertNotNil(seg);
  XCTAssertFalse([seg isMatch]);
  XCTAssertEqualStrings([seg string], @"b");
  // "fooobaar"
  seg = [enumerator nextObject];
  XCTAssertNotNil(seg);
  XCTAssertTrue([seg isMatch]);
  XCTAssertEqualStrings([seg string], @"fooobaar");
  // "foobar"
  seg = [enumerator nextObject];
  XCTAssertNotNil(seg);
  XCTAssertTrue([seg isMatch]);
  XCTAssertEqualStrings([seg string], @"foobar");
  // "zz"
  seg = [enumerator nextObject];
  XCTAssertNotNil(seg);
  XCTAssertFalse([seg isMatch]);
  XCTAssertEqualStrings([seg string], @"zz");
  // (end)
  seg = [enumerator nextObject];
  XCTAssertNil(seg);

  // test no match
  enumerator = [@"aaa" gtm_segmentEnumeratorForPattern:@"foo+ba+r"];
  XCTAssertNotNil(enumerator);
  seg = [enumerator nextObject];
  XCTAssertNotNil(seg);
  XCTAssertFalse([seg isMatch]);
  XCTAssertEqualStrings([seg string], @"aaa");
  seg = [enumerator nextObject];
  XCTAssertNil(seg);

  // test only match
  enumerator = [@"foobar" gtm_segmentEnumeratorForPattern:@"foo+ba+r"];
  XCTAssertNotNil(enumerator);
  seg = [enumerator nextObject];
  XCTAssertNotNil(seg);
  XCTAssertTrue([seg isMatch]);
  XCTAssertEqualStrings([seg string], @"foobar");
  seg = [enumerator nextObject];
  XCTAssertNil(seg);

  // now test the saved sub segments
  enumerator =
    [@"foobarxxfoobaz" gtm_segmentEnumeratorForPattern:@"(foo)((bar)|(baz))"];
  XCTAssertNotNil(enumerator);
  // "foobar"
  seg = [enumerator nextObject];
  XCTAssertNotNil(seg);
  XCTAssertTrue([seg isMatch]);
  XCTAssertEqualStrings([seg string], @"foobar");
  XCTAssertEqualStrings([seg subPatternString:0], @"foobar");
  XCTAssertEqualStrings([seg subPatternString:1], @"foo");
  XCTAssertEqualStrings([seg subPatternString:2], @"bar");
  XCTAssertEqualStrings([seg subPatternString:3], @"bar");
  XCTAssertNil([seg subPatternString:4]); // nothing matched "(baz)"
  XCTAssertNil([seg subPatternString:5]);
  // "xx"
  seg = [enumerator nextObject];
  XCTAssertNotNil(seg);
  XCTAssertFalse([seg isMatch]);
  XCTAssertEqualStrings([seg string], @"xx");
  XCTAssertEqualStrings([seg subPatternString:0], @"xx");
  XCTAssertNil([seg subPatternString:1]);
  // "foobaz"
  seg = [enumerator nextObject];
  XCTAssertNotNil(seg);
  XCTAssertTrue([seg isMatch]);
  XCTAssertEqualStrings([seg string], @"foobaz");
  XCTAssertEqualStrings([seg subPatternString:0], @"foobaz");
  XCTAssertEqualStrings([seg subPatternString:1], @"foo");
  XCTAssertEqualStrings([seg subPatternString:2], @"baz");
  XCTAssertNil([seg subPatternString:3]); // (nothing matched "(bar)"
  XCTAssertEqualStrings([seg subPatternString:4], @"baz");
  XCTAssertNil([seg subPatternString:5]);
  // (end)
  seg = [enumerator nextObject];
  XCTAssertNil(seg);

  // test all objects
  enumerator = [@"afoobarbfooobaarfoobarzz" gtm_segmentEnumeratorForPattern:@"foo+ba+r"];
  XCTAssertNotNil(enumerator);
  NSArray *allSegments = [enumerator allObjects];
  XCTAssertNotNil(allSegments);
  XCTAssertEqual((NSUInteger)6, [allSegments count]);
}

- (void)testMatchSegmentEnumeratorForPattern {
  NSEnumerator *enumerator =
    [@"afoobarbfooobaarfoobarzz" gtm_matchSegmentEnumeratorForPattern:@"foo+ba+r"];
  XCTAssertNotNil(enumerator);
  // "a" - skipped
  // "foobar"
  GTMRegexStringSegment *seg = [enumerator nextObject];
  XCTAssertNotNil(seg);
  XCTAssertTrue([seg isMatch]);
  XCTAssertEqualStrings([seg string], @"foobar");
  // "b" - skipped
  // "fooobaar"
  seg = [enumerator nextObject];
  XCTAssertNotNil(seg);
  XCTAssertTrue([seg isMatch]);
  XCTAssertEqualStrings([seg string], @"fooobaar");
  // "foobar"
  seg = [enumerator nextObject];
  XCTAssertNotNil(seg);
  XCTAssertTrue([seg isMatch]);
  XCTAssertEqualStrings([seg string], @"foobar");
  // "zz" - skipped
  // (end)
  seg = [enumerator nextObject];
  XCTAssertNil(seg);

  // test no match
  enumerator = [@"aaa" gtm_matchSegmentEnumeratorForPattern:@"foo+ba+r"];
  XCTAssertNotNil(enumerator);
  seg = [enumerator nextObject];
  XCTAssertNil(seg);

  // test only match
  enumerator = [@"foobar" gtm_matchSegmentEnumeratorForPattern:@"foo+ba+r"];
  XCTAssertNotNil(enumerator);
  seg = [enumerator nextObject];
  XCTAssertNotNil(seg);
  XCTAssertTrue([seg isMatch]);
  XCTAssertEqualStrings([seg string], @"foobar");
  seg = [enumerator nextObject];
  XCTAssertNil(seg);

  // now test the saved sub segments
  enumerator =
    [@"foobarxxfoobaz" gtm_matchSegmentEnumeratorForPattern:@"(foo)((bar)|(baz))"];
  XCTAssertNotNil(enumerator);
  // "foobar"
  seg = [enumerator nextObject];
  XCTAssertNotNil(seg);
  XCTAssertTrue([seg isMatch]);
  XCTAssertEqualStrings([seg string], @"foobar");
  XCTAssertEqualStrings([seg subPatternString:0], @"foobar");
  XCTAssertEqualStrings([seg subPatternString:1], @"foo");
  XCTAssertEqualStrings([seg subPatternString:2], @"bar");
  XCTAssertEqualStrings([seg subPatternString:3], @"bar");
  XCTAssertNil([seg subPatternString:4]); // nothing matched "(baz)"
  XCTAssertNil([seg subPatternString:5]);
  // "xx" - skipped
  // "foobaz"
  seg = [enumerator nextObject];
  XCTAssertNotNil(seg);
  XCTAssertTrue([seg isMatch]);
  XCTAssertEqualStrings([seg string], @"foobaz");
  XCTAssertEqualStrings([seg subPatternString:0], @"foobaz");
  XCTAssertEqualStrings([seg subPatternString:1], @"foo");
  XCTAssertEqualStrings([seg subPatternString:2], @"baz");
  XCTAssertNil([seg subPatternString:3]); // (nothing matched "(bar)"
  XCTAssertEqualStrings([seg subPatternString:4], @"baz");
  XCTAssertNil([seg subPatternString:5]);
  // (end)
  seg = [enumerator nextObject];
  XCTAssertNil(seg);

  // test all objects
  enumerator = [@"afoobarbfooobaarfoobarzz" gtm_matchSegmentEnumeratorForPattern:@"foo+ba+r"];
  XCTAssertNotNil(enumerator);
  NSArray *allSegments = [enumerator allObjects];
  XCTAssertNotNil(allSegments);
  XCTAssertEqual((NSUInteger)3, [allSegments count]);
}

- (void)testAllSubstringsMatchedByPattern {
  NSArray *segments =
    [@"afoobarbfooobaarfoobarzz" gtm_allSubstringsMatchedByPattern:@"foo+ba+r"];
  XCTAssertNotNil(segments);
  XCTAssertEqual((NSUInteger)3, [segments count]);
  XCTAssertEqualStrings([segments objectAtIndex:0], @"foobar");
  XCTAssertEqualStrings([segments objectAtIndex:1], @"fooobaar");
  XCTAssertEqualStrings([segments objectAtIndex:2], @"foobar");

  // test no match
  segments = [@"aaa" gtm_allSubstringsMatchedByPattern:@"foo+ba+r"];
  XCTAssertNotNil(segments);
  XCTAssertEqual((NSUInteger)0, [segments count]);

  // test only match
  segments = [@"foobar" gtm_allSubstringsMatchedByPattern:@"foo+ba+r"];
  XCTAssertNotNil(segments);
  XCTAssertEqual((NSUInteger)1, [segments count]);
  XCTAssertEqualStrings([segments objectAtIndex:0], @"foobar");
}

- (void)testStringByReplacingMatchesOfPatternWithReplacement {
  // the basics
  XCTAssertEqualStrings(@"weeZbarZbydoo spamZfooZdoggies",
                        [@"weefoobydoo spambardoggies" gtm_stringByReplacingMatchesOfPattern:@"(foo)(.*)(bar)"
                                                                            withReplacement:@"Z\\3Z\\2Z\\1Z"]);
  // nil/empty replacement
  XCTAssertEqualStrings(@"weedoggies",
                        [@"weefoobydoo spambardoggies" gtm_stringByReplacingMatchesOfPattern:@"(foo)(.*)(bar)"
                                                                             withReplacement:nil]);
  XCTAssertEqualStrings(@"weedoggies",
                        [@"weefoobydoo spambardoggies" gtm_stringByReplacingMatchesOfPattern:@"(foo)(.*)(bar)"
                                                                             withReplacement:@""]);
  XCTAssertEqualStrings(@"",
                        [@"" gtm_stringByReplacingMatchesOfPattern:@"(foo)(.*)(bar)"
                                                   withReplacement:@"abc"]);
  // use optional and invale subexpression parts to confirm that works
  XCTAssertEqualStrings(@"aaa baz bar bar foo baz aaa",
                        [@"aaa foooooobaz fooobar bar foo baz aaa" gtm_stringByReplacingMatchesOfPattern:@"(fo(o+))((bar)|(baz))"
                                                                                         withReplacement:@"\\4\\5"]);
  XCTAssertEqualStrings(@"aaa ZZZ ZZZ bar foo baz aaa",
                        [@"aaa foooooobaz fooobar bar foo baz aaa" gtm_stringByReplacingMatchesOfPattern:@"(fo(o+))((bar)|(baz))"
                                                                                         withReplacement:@"Z\\10Z\\12Z"]);
  // test slashes in replacement that aren't part of the subpattern reference
  XCTAssertEqualStrings(@"z\\\\0 \\\\a \\\\\\\\0z",
                        [@"zaz" gtm_stringByReplacingMatchesOfPattern:@"a+"
                                                      withReplacement:@"\\\\0 \\\\\\0 \\\\\\\\0"]);
  XCTAssertEqualStrings(@"z\\\\a \\\\\\\\0 \\\\\\\\az",
                        [@"zaz" gtm_stringByReplacingMatchesOfPattern:@"a+"
                                                      withReplacement:@"\\\\\\0 \\\\\\\\0 \\\\\\\\\\0"]);
  XCTAssertEqualStrings(@"z\\\\\\\\0 \\\\\\\\a \\\\\\\\\\\\0z",
                        [@"zaz" gtm_stringByReplacingMatchesOfPattern:@"a+"
                                                      withReplacement:@"\\\\\\\\0 \\\\\\\\\\0 \\\\\\\\\\\\0"]);
}

@end

#pragma clang diagnostic pop
