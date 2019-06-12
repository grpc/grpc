//
//  GTMRegex.m
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

#define GTMREGEX_DEFINE_GLOBALS 1
#import "GTMRegex.h"
#import "GTMDefines.h"

#pragma clang diagnostic push
// Ignore all of the deprecation warnings for GTMRegex
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#pragma clang diagnostic ignored "-Wdeprecated-implementations"

// This is the pattern to use for walking replacement text when doing
// substitutions.
//
// This pattern may look over-escaped, but remember the compiler will consume
// one layer of slashes, and then we have to escape the slashes for them to be
// seen as we want in the pattern.
static NSString *const kReplacementPattern =
  @"((^|[^\\\\])(\\\\\\\\)*)(\\\\([0-9]+))";
#define kReplacementPatternLeadingTextIndex       1
#define kReplacementPatternSubpatternNumberIndex  5

@interface GTMRegex (PrivateMethods)
- (NSString *)errorMessage:(int)errCode;
- (BOOL)runRegexOnUTF8:(const char*)utf8Str
                nmatch:(size_t)nmatch
                pmatch:(regmatch_t *)pmatch
                 flags:(int)flags;
@end

// private enumerator as impl detail
@interface GTMRegexEnumerator : NSEnumerator {
 @private
  GTMRegex *regex_;
  NSData *utf8StrBuf_;
  BOOL allSegments_;
  BOOL treatStartOfNewSegmentAsBeginningOfString_;
  regoff_t curParseIndex_;
  __strong regmatch_t *savedRegMatches_;
}
- (id)initWithRegex:(GTMRegex *)regex
      processString:(NSString *)str
        allSegments:(BOOL)allSegments;
- (void)treatStartOfNewSegmentAsBeginningOfString:(BOOL)yesNo;
@end

@interface GTMRegexStringSegment (PrivateMethods)
- (id)initWithUTF8StrBuf:(NSData *)utf8StrBuf
              regMatches:(regmatch_t *)regMatches
           numRegMatches:(NSUInteger)numRegMatches
                 isMatch:(BOOL)isMatch;
@end

@implementation GTMRegex

+ (id)regexWithPattern:(NSString *)pattern {
  return [[[self alloc] initWithPattern:pattern] autorelease];
}

+ (id)regexWithPattern:(NSString *)pattern options:(GTMRegexOptions)options {
  return [[[self alloc] initWithPattern:pattern
                                options:options] autorelease];
}

+ (id)regexWithPattern:(NSString *)pattern
               options:(GTMRegexOptions)options
             withError:(NSError **)outErrorOrNULL {
  return [[[self alloc] initWithPattern:pattern
                                options:options
                              withError:outErrorOrNULL] autorelease];
}

+ (NSString *)escapedPatternForString:(NSString *)str {
  if (str == nil)
    return nil;

  // NOTE: this could be done more efficiently by fetching the whole string into
  // a unichar buffer and scanning that, along w/ pushing the data over in
  // chunks (when possible).

  NSUInteger len = [str length];
  NSMutableString *result = [NSMutableString stringWithCapacity:len];

  for (NSUInteger x = 0; x < len; ++x) {
    unichar ch = [str characterAtIndex:x];
    switch (ch) {
      case '^':
      case '.':
      case '[':
      case '$':
      case '(':
      case ')':
      case '|':
      case '*':
      case '+':
      case '?':
      case '{':
      case '\\':
        [result appendFormat:@"\\%C", ch];
        break;
      default:
        [result appendFormat:@"%C", ch];
        break;
    }
  }

  return result;
}

- (id)init {
  return [self initWithPattern:nil];
}

- (id)initWithPattern:(NSString *)pattern {
  return [self initWithPattern:pattern options:0];
}

- (id)initWithPattern:(NSString *)pattern options:(GTMRegexOptions)options {
  return [self initWithPattern:pattern options:options withError:nil];
}

- (id)initWithPattern:(NSString *)pattern
              options:(GTMRegexOptions)options
            withError:(NSError **)outErrorOrNULL {

  self = [super init];
  if (!self) return nil;

  if (outErrorOrNULL) *outErrorOrNULL = nil;

  if ([pattern length] == 0) {
    [self release];
    return nil;
  }

  // figure out the flags
  options_ = options;
  int flags = REG_EXTENDED;
  if (options_ & kGTMRegexOptionIgnoreCase)
    flags |= REG_ICASE;
  if ((options_ & kGTMRegexOptionSupressNewlineSupport) == 0)
    flags |= REG_NEWLINE;

  // even if regcomp failes we need a flags that we did call regcomp so we'll
  // call regfree (because the structure can get filled in some to allow better
  // error info).  we use pattern_ as this flag.
  pattern_ = [pattern copy];
  if (!pattern_) {
     // COV_NF_START - no real way to force this in a unittest
    [self release];
    return nil;
    // COV_NF_END
  }

  // compile it
  int compResult = regcomp(&regexData_, [pattern_ UTF8String], flags);
  if (compResult != 0) {
    NSString *errorStr = [self errorMessage:compResult];
    if (outErrorOrNULL) {
      // include the pattern and patternError message in the userInfo.
      NSDictionary *userInfo = [NSDictionary dictionaryWithObjectsAndKeys:
                                pattern_, kGTMRegexPatternErrorPattern,
                                errorStr, kGTMRegexPatternErrorErrorString,
                                nil];
      *outErrorOrNULL = [NSError errorWithDomain:kGTMRegexErrorDomain
                                            code:kGTMRegexPatternParseFailedError
                                        userInfo:userInfo];
    } else {
      // if caller didn't get us an NSError to fill in, we log the error to help
      // debugging.
      _GTMDevLog(@"Invalid pattern \"%@\", error: \"%@\"",
                 pattern_, errorStr);
    }

    [self release];
    return nil;
  }

  return self;
}

- (void)dealloc {
  // we used pattern_ as our flag that we initialized the regex_t
  if (pattern_) {
    regfree(&regexData_);
    [pattern_ release];
    // play it safe and clear it since we use it as a flag for regexData_
    pattern_ = nil;
  }
  [super dealloc];
}

- (NSUInteger)subPatternCount {
  return regexData_.re_nsub;
}

- (BOOL)matchesString:(NSString *)str {
  regmatch_t regMatch;
  if (![self runRegexOnUTF8:[str UTF8String]
                     nmatch:1
                     pmatch:&regMatch
                      flags:0]) {
    // no match
    return NO;
  }

  // make sure the match is the full string
  return (regMatch.rm_so == 0) &&
    (regMatch.rm_eo == (regoff_t)[str lengthOfBytesUsingEncoding:NSUTF8StringEncoding]);
}

- (NSArray *)subPatternsOfString:(NSString *)str {
  NSArray *result = nil;

  NSUInteger count = regexData_.re_nsub + 1;
  regmatch_t *regMatches = malloc(sizeof(regmatch_t) * count);
  if (!regMatches)
    return nil; // COV_NF_LINE - no real way to force this in a unittest

  // wrap it all in a try so we don't leak the malloc
  @try {
    const char *utf8Str = [str UTF8String];
    if (![self runRegexOnUTF8:utf8Str
                       nmatch:count
                       pmatch:regMatches
                        flags:0]) {
      // no match
      return nil;
    }

    // make sure the match is the full string
    if ((regMatches[0].rm_so != 0) ||
        (regMatches[0].rm_eo != (regoff_t)[str lengthOfBytesUsingEncoding:NSUTF8StringEncoding])) {
      // only matched a sub part of the string
      return nil;
    }

    NSMutableArray *buildResult = [NSMutableArray arrayWithCapacity:count];

    for (NSUInteger x = 0 ; x < count ; ++x) {
      if ((regMatches[x].rm_so == -1) && (regMatches[x].rm_eo == -1)) {
        // add NSNull since it wasn't used
        [buildResult addObject:[NSNull null]];
      } else {
        // fetch the string
        const char *base = utf8Str + regMatches[x].rm_so;
        regoff_t len = regMatches[x].rm_eo - regMatches[x].rm_so;
        NSString *sub =
          [[[NSString alloc] initWithBytes:base
                                    length:(NSUInteger)len
                                  encoding:NSUTF8StringEncoding] autorelease];
        [buildResult addObject:sub];
      }
    }

    result = buildResult;
  } // COV_NF_LINE - radar 5851992 only reachable w/ an uncaught exception which isn't testable
  @finally {
    free(regMatches);
  }

  return result;
}

- (NSString *)firstSubStringMatchedInString:(NSString *)str {
  NSString *result = nil;

  regmatch_t regMatch;
  const char *utf8Str = [str UTF8String];
  if ([self runRegexOnUTF8:utf8Str
                    nmatch:1
                    pmatch:&regMatch
                     flags:0]) {
    // fetch the string
    const char *base = utf8Str + regMatch.rm_so;
    regoff_t len = regMatch.rm_eo - regMatch.rm_so;
    result =
      [[[NSString alloc] initWithBytes:base
                                length:(NSUInteger)len
                              encoding:NSUTF8StringEncoding] autorelease];
  }
  return result;
}

- (BOOL)matchesSubStringInString:(NSString *)str {
  regmatch_t regMatch;
  if ([self runRegexOnUTF8:[str UTF8String]
                    nmatch:1
                    pmatch:&regMatch
                     flags:0]) {
    // don't really care what matched, just report the match
    return YES;
  }
  return NO;
}

- (NSEnumerator *)segmentEnumeratorForString:(NSString *)str {
  return [[[GTMRegexEnumerator alloc] initWithRegex:self
                                      processString:str
                                        allSegments:YES] autorelease];
}

- (NSEnumerator *)matchSegmentEnumeratorForString:(NSString *)str {
  return [[[GTMRegexEnumerator alloc] initWithRegex:self
                                      processString:str
                                        allSegments:NO] autorelease];
}

- (NSString *)stringByReplacingMatchesInString:(NSString *)str
                               withReplacement:(NSString *)replacementPattern {
  if (!str)
    return nil;

  // if we have a replacement, we go ahead and crack it now.  if the replacement
  // is just an empty string (or nil), just use the nil marker.
  NSArray *replacements = nil;
  if ([replacementPattern length]) {
    // don't need newline support, just match the start of the pattern for '^'
    GTMRegex *replacementRegex =
      [GTMRegex regexWithPattern:kReplacementPattern
                         options:kGTMRegexOptionSupressNewlineSupport];
#ifdef DEBUG
    if (!replacementRegex) {
      _GTMDevLog(@"failed to parse out replacement regex!!!"); // COV_NF_LINE
    }
#endif
    GTMRegexEnumerator *relacementEnumerator =
      [[[GTMRegexEnumerator alloc] initWithRegex:replacementRegex
                                        processString:replacementPattern
                                          allSegments:YES] autorelease];
    // We turn on treatStartOfNewSegmentAsBeginningOfLine for this enumerator.
    // As complex as kReplacementPattern is, it can't completely do what we want
    // with the normal string walk.  The problem is this, backreferences are a
    // slash follow by a number ("\0"), but the replacement pattern might
    // actually need to use backslashes (they have to be escaped).  So if a
    // replacement were "\\0", then there is no backreference, instead the
    // replacement is a backslash and a zero.  Generically this means an even
    // number of backslashes are all escapes, and an odd are some number of
    // literal backslashes followed by our backreference.  Think of it as a "an
    // odd number of slashes that comes after a non-backslash character."  There
    // is no way to rexpress this in re_format(7) extended expressions.  Instead
    // we look for a non-blackslash or string start followed by an optional even
    // number of slashes followed by the backreference; and use the special
    // flag; so after each match, we restart claiming it's the start of the
    // string.  (the problem match w/o this flag is a substition of "\2\1")
    [relacementEnumerator treatStartOfNewSegmentAsBeginningOfString:YES];
    // pull them all into an array so we can walk this as many times as needed.
    replacements = [relacementEnumerator allObjects];
    if (!replacements) {
      // COV_NF_START - no real way to force this in a unittest
      _GTMDevLog(@"failed to create the replacements for substitutions");
      return nil;
      // COV_NF_END
    }
  }

  NSMutableString *result = [NSMutableString stringWithCapacity:[str length]];

  NSEnumerator *enumerator = [self segmentEnumeratorForString:str];
  GTMRegexStringSegment *segment = nil;
  while ((segment = [enumerator nextObject]) != nil) {
    if (![segment isMatch]) {
      // not a match, just move this chunk over
      [result appendString:[segment string]];
    } else {
      // match...
      if (!replacements) {
        // no replacements, they want to eat matches, nothing to do
      } else {
        // spin over the split up replacement
        GTMRegexStringSegment *replacementSegment = nil;
        for (replacementSegment in replacements) {
          if (![replacementSegment isMatch]) {
            // not a match, raw text to put in
            [result appendString:[replacementSegment string]];
          } else {
            // match...

            // first goes any leading text
            NSString *leading =
              [replacementSegment subPatternString:kReplacementPatternLeadingTextIndex];
            if (leading)
              [result appendString:leading];
            // then use the subpattern number to find what goes in from the
            // original string match.
            int subPatternNum =
              [[replacementSegment subPatternString:kReplacementPatternSubpatternNumberIndex] intValue];
            NSString *matchSubPatStr = [segment subPatternString:subPatternNum];
            // handle an unused subpattern (ie-nil result)
            if (matchSubPatStr)
              [result appendString:matchSubPatStr];
          }
        }
      }
    }
  }
  return result;
}

- (NSString *)description {
  NSMutableString *result =
    [NSMutableString stringWithFormat:@"%@<%p> { pattern=\"%@\", rawNumSubPatterns=%zu, options=(",
      [self class], self, pattern_, regexData_.re_nsub];
  if (options_) {
    if (options_ & kGTMRegexOptionIgnoreCase)
      [result appendString:@" IgnoreCase"];
    if ((options_ & kGTMRegexOptionSupressNewlineSupport) == kGTMRegexOptionSupressNewlineSupport)
      [result appendString:@" NoNewlineSupport"];
  } else {
    [result appendString:@" None(Default)"];
  }
  [result appendString:@" ) }"];
  return result;
}

@end

@implementation GTMRegex (PrivateMethods)

- (NSString *)errorMessage:(int)errCode {
  NSString *result = @"internal error";

  // size the buffer we need
  size_t len = regerror(errCode, &regexData_, NULL, 0);
  char *buffer = (char*)malloc(sizeof(char) * len);
  if (buffer) {
    // fetch the error
    if (len == regerror(errCode, &regexData_, buffer, len)) {
      NSString *generatedError = [NSString stringWithUTF8String:buffer];
      if (generatedError)
        result = generatedError;
    }
    free(buffer);
  }
  return result;
}

// private helper to run the regex on a block
- (BOOL)runRegexOnUTF8:(const char*)utf8Str
                nmatch:(size_t)nmatch
                pmatch:(regmatch_t *)pmatch
                 flags:(int)flags {
  if (!utf8Str)
    return NO;

  int execResult = regexec(&regexData_, utf8Str, nmatch, pmatch, flags);
  if (execResult != 0) {
#ifdef DEBUG
    if (execResult != REG_NOMATCH) {
      // COV_NF_START - no real way to force this in a unittest
      NSString *errorStr = [self errorMessage:execResult];
      _GTMDevLog(@"%@: matching string \"%.20s...\", had error: \"%@\"",
                 self, utf8Str, errorStr);
      // COV_NF_END
    }
#endif
    return NO;
  }
  return YES;
}

@end

@implementation GTMRegexEnumerator

// we don't block init because the class isn't exported, so no one can
// create one, or if they do, they get whatever happens...

- (id)initWithRegex:(GTMRegex *)regex
      processString:(NSString *)str
        allSegments:(BOOL)allSegments {
  self = [super init];
  if (!self) return nil;

  // collect args
  regex_ = [regex retain];
  utf8StrBuf_ = [[str dataUsingEncoding:NSUTF8StringEncoding] retain];
  allSegments_ = allSegments;

  // arg check
  if (!regex_ || !utf8StrBuf_) {
    [self release];
    return nil;
  }

  // parsing state initialized to zero for us by object creation

  return self;
}

// Don't need a finalize because savedRegMatches_ is marked __strong
- (void)dealloc {
  free(savedRegMatches_);
  [regex_ release];
  [utf8StrBuf_ release];
  [super dealloc];
}

- (void)treatStartOfNewSegmentAsBeginningOfString:(BOOL)yesNo {
  // The way regexec works, it assumes the first char it's looking at to the
  // start of the string.  In normal use, this makes sense; but in this case,
  // we're going to walk the entry string splitting it up by our pattern.  That
  // means for the first call, it is the string start, but for all future calls,
  // it is NOT the string start, so we will pass regexec the flag to let it
  // know.  However, (you knew that was coming), there are some cases where you
  // actually want the each pass to be considered as the start of the string
  // (usually the cases are where a pattern can't express what's needed w/o
  // this).  There is no really good way to explain this behavior w/o all this
  // text and lot of examples, so for now this is not in the public api, and
  // just here. (Hint: see what w/in this file uses this for why we have it)
  treatStartOfNewSegmentAsBeginningOfString_ = yesNo;
}

- (id)nextObject {

  GTMRegexStringSegment *result = nil;
  regmatch_t *nextMatches = nil;
  BOOL isMatch = NO;

  // we do all this w/in a try, so if something throws, the memory we malloced
  // will still get cleaned up
  @try {

    // if we have a saved match, use that...
    if (savedRegMatches_) {
      nextMatches = savedRegMatches_;
      savedRegMatches_ = nil;
      isMatch = YES; // if we have something saved, it was a pattern match
    }
    // have we reached the end?
    else if (curParseIndex_ >= (regoff_t)[utf8StrBuf_ length]) {
      // done, do nothing, we'll return nil
    }
    // do the search.
    else {

      // alloc the match structure (extra space for the zero (full) match)
      size_t matchBufSize = ([regex_ subPatternCount] + 1) * sizeof(regmatch_t);
      nextMatches = malloc(matchBufSize);
      if (!nextMatches)
        return nil; // COV_NF_LINE - no real way to force this in a unittest

      // setup our range to work on
      nextMatches[0].rm_so = curParseIndex_;
      nextMatches[0].rm_eo = [utf8StrBuf_ length];

      // figure out our flags
      int flags = REG_STARTEND;
      if ((!treatStartOfNewSegmentAsBeginningOfString_) &&
          (curParseIndex_ != 0)) {
        // see -treatStartOfNewSegmentAsBeginningOfString: for why we have
        // this check here.
        flags |= REG_NOTBOL;
      }

      // call for the match
      if ([regex_ runRegexOnUTF8:[utf8StrBuf_ bytes]
                          nmatch:([regex_ subPatternCount] + 1)
                          pmatch:nextMatches
                           flags:flags]) {
        // match

        if (allSegments_ &&
            (nextMatches[0].rm_so != curParseIndex_)) {
          // we should return all segments (not just matches), and there was
          // something before this match.  So safe off this match for later
          // and create a range for this.

          savedRegMatches_ = nextMatches;
          nextMatches = malloc(matchBufSize);
          if (!nextMatches)
            return nil; // COV_NF_LINE - no real way to force this in a unittest

          isMatch = NO;
          // mark everything but the zero slot w/ not used
          for (NSUInteger x = [regex_ subPatternCount]; x > 0; --x) {
            nextMatches[x].rm_so = nextMatches[x].rm_eo = -1;
          }
          nextMatches[0].rm_so = curParseIndex_;
          nextMatches[0].rm_eo = savedRegMatches_[0].rm_so;

          // advance our marker
          curParseIndex_ = savedRegMatches_[0].rm_eo;

        } else {
          // we only return matches or are pointed at a match

          // no real work to do, just fall through to return to return the
          // current match.
          isMatch = YES;

          // advance our marker
          curParseIndex_ = nextMatches[0].rm_eo;
        }

      } else {
        // no match

        // should we return the last non matching segment?
        if (allSegments_) {
          isMatch = NO;
          // mark everything but the zero slot w/ not used
          for (NSUInteger x = [regex_ subPatternCount]; x > 0; --x) {
            nextMatches[x].rm_so = nextMatches[x].rm_eo = -1;
          }
          nextMatches[0].rm_so = curParseIndex_;
          nextMatches[0].rm_eo = [utf8StrBuf_ length];
        } else {
          // drop match set, we don't want it
          free(nextMatches);
          nextMatches = nil;
        }

        // advance our marker since we're done
        curParseIndex_ = [utf8StrBuf_ length];

      }
    }

    // create the segment to return
    if (nextMatches) {
      result =
        [[[GTMRegexStringSegment alloc] initWithUTF8StrBuf:utf8StrBuf_
                                                regMatches:nextMatches
                                             numRegMatches:[regex_ subPatternCount]
                                                   isMatch:isMatch] autorelease];
      nextMatches = nil;
    }
  } @catch (id e) { // COV_NF_START - no real way to force this in a test
    _GTMDevLog(@"Exceptions while trying to advance enumeration (%@)", e);
    // if we still have something in our temp, free it
    free(nextMatches);
  } // COV_NF_END

  return result;
}

- (NSString *)description {
  return [NSString stringWithFormat:@"%@<%p> { regex=\"%@\", allSegments=%s, string=\"%.20s...\" }",
    [self class], self,
    regex_,
    (allSegments_ ? "YES" : "NO"),
    [utf8StrBuf_ bytes]];
}

@end

@implementation GTMRegexStringSegment

- (id)init {
  // make sure init is never called, the class in in the header so someone
  // could try to create it by mistake.
  // Call super init and release so we don't leak
  [[super init] autorelease];
  [self doesNotRecognizeSelector:_cmd];
  return nil; // COV_NF_LINE - return is just here to keep gcc happy
}

- (void)dealloc {
  free(regMatches_);
  [utf8StrBuf_ release];
  [super dealloc];
}

- (BOOL)isMatch {
  return isMatch_;
}

- (NSString *)string {
  // fetch match zero
  return [self subPatternString:0];
}

- (NSString *)subPatternString:(NSUInteger)patternIndex {
  if (patternIndex > numRegMatches_)
    return nil;

  // pick off when it wasn't found
  if ((regMatches_[patternIndex].rm_so == -1) &&
      (regMatches_[patternIndex].rm_eo == -1))
    return nil;

  // fetch the string
  const char *base = (const char*)[utf8StrBuf_ bytes]
    + regMatches_[patternIndex].rm_so;
  regoff_t len = regMatches_[patternIndex].rm_eo
    - regMatches_[patternIndex].rm_so;
  return [[[NSString alloc] initWithBytes:base
                                   length:(NSUInteger)len
                                 encoding:NSUTF8StringEncoding] autorelease];
}

- (NSString *)description {
  NSMutableString *result =
    [NSMutableString stringWithFormat:@"%@<%p> { isMatch=\"%s\", subPatterns=(",
      [self class], self, (isMatch_ ? "YES" : "NO")];
  for (NSUInteger x = 0; x <= numRegMatches_; ++x) {
    int length = (int)(regMatches_[x].rm_eo - regMatches_[x].rm_so);
    const char* string
      = (((const char*)[utf8StrBuf_ bytes]) + regMatches_[x].rm_so);
    if (x == 0) {
      [result appendFormat:@" \"%.*s\"", length , string];
    } else {
      [result appendFormat:@", \"%.*s\"", length , string];
    }
  }
  [result appendString:@" ) }"];

  return result;
}

@end

@implementation GTMRegexStringSegment (PrivateMethods)

- (id)initWithUTF8StrBuf:(NSData *)utf8StrBuf
              regMatches:(regmatch_t *)regMatches
           numRegMatches:(NSUInteger)numRegMatches
                 isMatch:(BOOL)isMatch {
  self = [super init];
  if (!self) return nil;

  utf8StrBuf_ = [utf8StrBuf retain];
  regMatches_ = regMatches;
  numRegMatches_ = numRegMatches;
  isMatch_ = isMatch;

  // check the args
  if (!utf8StrBuf_ || !regMatches_) {
    // COV_NF_START
    // this could only happen something messed w/ our internal state.
    [self release];
    return nil;
    // COV_NF_END
  }

  return self;
}

@end

@implementation NSString (GTMRegexAdditions)

- (BOOL)gtm_matchesPattern:(NSString *)pattern {
  GTMRegex *regex = [GTMRegex regexWithPattern:pattern];
  return [regex matchesString:self];
}

- (NSArray *)gtm_subPatternsOfPattern:(NSString *)pattern {
  GTMRegex *regex = [GTMRegex regexWithPattern:pattern];
  return [regex subPatternsOfString:self];
}

- (NSString *)gtm_firstSubStringMatchedByPattern:(NSString *)pattern {
  GTMRegex *regex = [GTMRegex regexWithPattern:pattern];
  return [regex firstSubStringMatchedInString:self];
}

- (BOOL)gtm_subStringMatchesPattern:(NSString *)pattern {
  GTMRegex *regex = [GTMRegex regexWithPattern:pattern];
  return [regex matchesSubStringInString:self];
}

- (NSArray *)gtm_allSubstringsMatchedByPattern:(NSString *)pattern {
  NSEnumerator *enumerator = [self gtm_matchSegmentEnumeratorForPattern:pattern];
  NSArray *allSegments = [enumerator allObjects];
  return [allSegments valueForKey:@"string"];
}

- (NSEnumerator *)gtm_segmentEnumeratorForPattern:(NSString *)pattern {
  GTMRegex *regex = [GTMRegex regexWithPattern:pattern];
  return [regex segmentEnumeratorForString:self];
}

- (NSEnumerator *)gtm_matchSegmentEnumeratorForPattern:(NSString *)pattern {
  GTMRegex *regex = [GTMRegex regexWithPattern:pattern];
  return [regex matchSegmentEnumeratorForString:self];
}

- (NSString *)gtm_stringByReplacingMatchesOfPattern:(NSString *)pattern
                                    withReplacement:(NSString *)replacementPattern {
  GTMRegex *regex = [GTMRegex regexWithPattern:pattern];
  return [regex stringByReplacingMatchesInString:self
                                 withReplacement:replacementPattern];
}

@end

#pragma clang diagnostic push
