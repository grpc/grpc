//
//  GTMSQLite.m
//
//  Convenience wrapper for SQLite storage see the header for details.
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


#import <Foundation/Foundation.h>
#import "GTMSQLite.h"
#import "GTMMethodCheck.h"
#import "GTMDefines.h"
#include <limits.h>

typedef struct {
  BOOL upperCase;
  int  textRep;
} UpperLowerUserArgs;

typedef struct {
  BOOL            reverse;
  CFOptionFlags   compareOptions;
  int             textRep;
} CollateUserArgs;

typedef struct {
  CFOptionFlags   *compareOptionPtr;
  int             textRep;
} LikeGlobUserArgs;

// Helper inline for SQLite text type to CF endcoding
GTM_INLINE CFStringEncoding SqliteTextEncodingToCFStringEncoding(int enc) {
  // Default should never happen, but assume UTF 8
  CFStringEncoding encoding = kCFStringEncodingUTF8;
  _GTMDevAssert(enc == SQLITE_UTF16BE ||
                enc == SQLITE_UTF16LE,
                @"Passed in encoding was not a UTF16 encoding");
  switch(enc) {
    case SQLITE_UTF16BE:
      encoding = kCFStringEncodingUTF16BE;
      break;
    case SQLITE_UTF16LE:
      encoding = kCFStringEncodingUTF16LE;
      break;
  }
  return encoding;
}

// Helper inline for filtering CFStringCompareFlags
GTM_INLINE CFOptionFlags FilteredStringCompareFlags(CFOptionFlags inOptions) {
  CFOptionFlags outOptions = 0;
  if (inOptions & kCFCompareCaseInsensitive) {
    outOptions |= kCFCompareCaseInsensitive;
  }
  if (inOptions & kCFCompareNonliteral) outOptions |= kCFCompareNonliteral;
  if (inOptions & kCFCompareLocalized) outOptions |= kCFCompareLocalized;
  if (inOptions & kCFCompareNumerically) outOptions |= kCFCompareNumerically;
  if (inOptions & kCFCompareDiacriticInsensitive) {
    outOptions |= kCFCompareDiacriticInsensitive;
  }
  if (inOptions & kCFCompareWidthInsensitive) {
    outOptions |= kCFCompareWidthInsensitive;
  }
  return outOptions;
}

//  Function prototypes for our custom implementations of UPPER/LOWER using
//  CFString so that we handle Unicode and localization more cleanly than
//  native SQLite.
static void UpperLower8(sqlite3_context *context,
                        int argc,
                        sqlite3_value **argv);
static void UpperLower16(sqlite3_context *context,
                         int argc,
                         sqlite3_value **argv);

//  Function prototypes for CFString-based collation sequences
static void CollateNeeded(void *userContext, sqlite3 *db,
                          int textRep, const char *name);
static int Collate8(void *userContext, int length1, const void *str1,
                    int length2, const void *str2);
static int Collate16(void *userContext, int length1, const void *str1,
                     int length2, const void *str2);

//  Function prototypes for CFString LIKE and GLOB
static void Like8(sqlite3_context *context, int argc, sqlite3_value **argv);
static void Like16(sqlite3_context *context, int argc, sqlite3_value **argv);
static void Glob8(sqlite3_context *context, int argc, sqlite3_value **argv);
static void Glob16(sqlite3_context *context, int argc, sqlite3_value **argv);

//  The CFLocale of the current user at process start
static CFLocaleRef gCurrentLocale = NULL;

// Private methods
@interface GTMSQLiteDatabase (PrivateMethods)

- (int)installCFAdditions;
- (void)collationArgumentRetain:(NSData *)collationArgs;
//  Convenience method to clean up resources.  Called from both
//  dealloc & finalize
//
- (void)cleanupDB;
@end

@implementation GTMSQLiteDatabase

+ (void)initialize {
  // Need the locale for some CFString enhancements
  gCurrentLocale = CFLocaleCopyCurrent();
}

+ (int)sqliteVersionNumber {
  return sqlite3_libversion_number();
}

+ (NSString *)sqliteVersionString {
  return [NSString stringWithUTF8String:sqlite3_libversion()];
}

- (id)initWithPath:(NSString *)path
   withCFAdditions:(BOOL)additions
              utf8:(BOOL)useUTF8
         errorCode:(int *)err {
  int rc = SQLITE_INTERNAL;

  if ((self = [super init])) {
    path_ = [path copy];
    if (useUTF8) {
      rc = sqlite3_open([path_ fileSystemRepresentation], &db_);
    } else {
      CFStringEncoding cfEncoding;
#if TARGET_RT_BIG_ENDIAN
      cfEncoding = kCFStringEncodingUTF16BE;
#else
      cfEncoding = kCFStringEncodingUTF16LE;
#endif
      NSStringEncoding nsEncoding
        = CFStringConvertEncodingToNSStringEncoding(cfEncoding);
      NSData *data = [path dataUsingEncoding:nsEncoding];
      // Using -[NSString cStringUsingEncoding] causes sqlite3_open16
      // to fail because it expects 2 null-terminating bytes and
      // cStringUsingEncoding only has 1
      NSMutableData *mutable = [NSMutableData dataWithData:data];
      [mutable increaseLengthBy:2];
      rc = sqlite3_open16([mutable bytes], &db_);
    }

    if ((rc == SQLITE_OK) && db_) {
      if (additions) {
        userArgDataPool_ = [[NSMutableArray array] retain];
        if (!userArgDataPool_) {
          // Leave *err as internal err
          // COV_NF_START - not sure how to fail Cocoa initializers
          [self release];
          return nil;
          // COV_NF_END
        }
        rc = [self installCFAdditions];
      }
    }

    if (err) *err = rc;

    if (rc != SQLITE_OK) {
      // COV_NF_START
      [self release];
      self = nil;
      // COV_NF_END
    }
  }

  return self;
}

- (id)initInMemoryWithCFAdditions:(BOOL)additions
                             utf8:(BOOL)useUTF8
                        errorCode:(int *)err {
  return [self initWithPath:@":memory:"
            withCFAdditions:additions
                       utf8:useUTF8
                  errorCode:err];
}

- (void)dealloc {
  [self cleanupDB];
  [super dealloc];
}

- (void)cleanupDB {
  if (db_) {
    int rc = sqlite3_close(db_);
    if (rc != SQLITE_OK) {
      _GTMDevLog(@"Unable to close \"%@\", error code: %d\r"
                 @"Did you forget to call -[GTMSQLiteStatement"
                 @" finalizeStatement] on one of your statements?",
                 self, rc);
    }
  }
  [path_ release];
  [userArgDataPool_ release];
}

//  Private method to install our custom CoreFoundation additions to SQLite
//  behavior
- (int)installCFAdditions {
  int rc = SQLITE_OK;
  // Install our custom functions for improved text handling
  // UPPER/LOWER
  const struct {
    const char           *sqlName;
    UpperLowerUserArgs   userArgs;
    void                 *function;
  } customUpperLower[] = {
    { "upper", { YES, SQLITE_UTF8 }, &UpperLower8 },
    { "upper", { YES, SQLITE_UTF16 }, &UpperLower16 },
    { "upper", { YES, SQLITE_UTF16BE }, &UpperLower16 },
    { "upper", { YES, SQLITE_UTF16LE }, &UpperLower16 },
    { "lower", { NO, SQLITE_UTF8 }, &UpperLower8 },
    { "lower", { NO, SQLITE_UTF16 }, &UpperLower16 },
    { "lower", { NO, SQLITE_UTF16BE }, &UpperLower16 },
    { "lower", { NO, SQLITE_UTF16LE }, &UpperLower16 },
  };

  for (size_t i = 0;
       i < (sizeof(customUpperLower) / sizeof(customUpperLower[0]));
       i++) {
    rc = sqlite3_create_function(db_,
                                 customUpperLower[i].sqlName,
                                 1,
                                 customUpperLower[i].userArgs.textRep,
                                 (void *)&customUpperLower[i].userArgs,
                                 customUpperLower[i].function,
                                 NULL,
                                 NULL);
    if (rc != SQLITE_OK)
      return rc; // COV_NF_LINE because sqlite3_create_function is
                 // called with input defined at compile-time
  }

  // Fixed collation sequences
  const struct {
    const char           *sqlName;
    CollateUserArgs      userArgs;
    void                 *function;
  } customCollationSequence[] = {
    { "nocase", { NO, kCFCompareCaseInsensitive, SQLITE_UTF8 }, &Collate8 },
    { "nocase", { NO, kCFCompareCaseInsensitive, SQLITE_UTF16 }, &Collate16 },
    { "nocase", { NO, kCFCompareCaseInsensitive, SQLITE_UTF16BE }, &Collate16 },
    { "nocase", { NO, kCFCompareCaseInsensitive, SQLITE_UTF16LE }, &Collate16 },
  };

  for (size_t i = 0;
       i < (sizeof(customCollationSequence) / sizeof(customCollationSequence[0]));
       i++) {
    rc = sqlite3_create_collation(db_,
                                  customCollationSequence[i].sqlName,
                                  customCollationSequence[i].userArgs.textRep,
                                  (void *)&customCollationSequence[i].userArgs,
                                  customCollationSequence[i].function);
    if (rc != SQLITE_OK)
      return rc; // COV_NF_LINE because the input to
                 // sqlite3_create_collation is set at compile time
  }

  // Install handler for dynamic collation sequences
  const struct {
    const char          *sqlName;
    int                 numArgs;
    int                 textRep;
    void                *function;
  } customLike[] = {
    { "like", 2, SQLITE_UTF8, &Like8 },
    { "like", 2, SQLITE_UTF16, &Like16 },
    { "like", 2, SQLITE_UTF16BE, &Like16 },
    { "like", 2, SQLITE_UTF16LE, &Like16 },
    { "like", 3, SQLITE_UTF8, &Like8 },
    { "like", 3, SQLITE_UTF16, &Like16 },
    { "like", 3, SQLITE_UTF16BE, &Like16 },
    { "like", 3, SQLITE_UTF16LE, &Like16 },
  };

  rc = sqlite3_collation_needed(db_, self, &CollateNeeded);
  if (rc != SQLITE_OK)
    return rc; // COV_NF_LINE because input to
               // sqlite3_collation_needed is static

  // Start LIKE as case-insensitive and non-literal
  // (sqlite defaults LIKE to case-insensitive)
  likeOptions_ = kCFCompareCaseInsensitive | kCFCompareNonliteral;
  for (size_t i = 0; i < (sizeof(customLike) / sizeof(customLike[0])); i++) {
    // Each implementation gets its own user args
    NSMutableData *argsData
      = [NSMutableData dataWithLength:sizeof(LikeGlobUserArgs)];
    if (!argsData) return SQLITE_INTERNAL;
    [userArgDataPool_ addObject:argsData];
    LikeGlobUserArgs *args = (LikeGlobUserArgs *)[argsData bytes];
    args->compareOptionPtr = &likeOptions_;
    args->textRep = customLike[i].textRep;
    rc = sqlite3_create_function(db_,
                                 customLike[i].sqlName,
                                 customLike[i].numArgs,
                                 customLike[i].textRep,
                                 args,
                                 customLike[i].function,
                                 NULL,
                                 NULL);
    if (rc != SQLITE_OK)
      return rc; // COV_NF_LINE because input to
                 // sqlite3_create_function is static
  }

  // Start GLOB just non-literal but case-sensitive (same as SQLite defaults)
  const struct {
    const char          *sqlName;
    int                 textRep;
    void                *function;
  } customGlob[] = {
    { "glob", SQLITE_UTF8, &Glob8 },
    { "glob", SQLITE_UTF16, &Glob16 },
    { "glob", SQLITE_UTF16BE, &Glob16 },
    { "glob", SQLITE_UTF16LE, &Glob16 },
  };

  globOptions_ = kCFCompareNonliteral;
  for (size_t i = 0; i < (sizeof(customGlob) / sizeof(customGlob[0])); i++) {
    // Each implementation gets its own user args
    NSMutableData *argsData
      = [NSMutableData dataWithLength:sizeof(LikeGlobUserArgs)];
    if (!argsData) return SQLITE_INTERNAL;
    [userArgDataPool_ addObject:argsData];
    LikeGlobUserArgs *args = (LikeGlobUserArgs *)[argsData bytes];
    args->compareOptionPtr = &globOptions_;
    args->textRep = customGlob[i].textRep;
    rc = sqlite3_create_function(db_,
                                 customGlob[i].sqlName,
                                 2,
                                 customGlob[i].textRep,
                                 args,
                                 customGlob[i].function,
                                 NULL,
                                 NULL);
    if (rc != SQLITE_OK)
      return rc; // COV_NF_LINE because input to
                 // sqlite3_create_function is static
  }

  hasCFAdditions_ = YES;
  return SQLITE_OK;
}

// Private method used by collation creation callback
- (void)collationArgumentRetain:(NSData *)collationArgs {
  [userArgDataPool_ addObject:collationArgs];
}

- (sqlite3 *)sqlite3DB {
  return db_;
}

- (void)synchronousMode:(BOOL)enable {
  if (enable) {
    [self executeSQL:@"PRAGMA synchronous = NORMAL;"];
    [self executeSQL:@"PRAGMA fullfsync = 1;"];
  } else {
    [self executeSQL:@"PRAGMA fullfsync = 0;"];
    [self executeSQL:@"PRAGMA synchronous = OFF;"];
  }
}

- (BOOL)hasCFAdditions {
  return hasCFAdditions_;
}

- (void)setLikeComparisonOptions:(CFOptionFlags)options {
  if (hasCFAdditions_)
    likeOptions_ = FilteredStringCompareFlags(options);
}

- (CFOptionFlags)likeComparisonOptions {
  CFOptionFlags flags = 0;
  if (hasCFAdditions_)
    flags = likeOptions_;
  return flags;
}

- (void)setGlobComparisonOptions:(CFOptionFlags)options {
  if (hasCFAdditions_)
    globOptions_ = FilteredStringCompareFlags(options);
}

- (CFOptionFlags)globComparisonOptions {
  CFOptionFlags globOptions = 0;
  if (hasCFAdditions_)
    globOptions = globOptions_;
  return globOptions;
}

- (int)lastErrorCode {
  return sqlite3_errcode(db_);
}

- (NSString *)lastErrorString {
  const char *errMsg = sqlite3_errmsg(db_);
  if (!errMsg) return nil;
  return [NSString stringWithCString:errMsg encoding:NSUTF8StringEncoding];
}

- (int)lastChangeCount {
  return sqlite3_changes(db_);
}

- (int)totalChangeCount {
  return sqlite3_total_changes(db_);
}

- (unsigned long long)lastInsertRowID {
  return sqlite3_last_insert_rowid(db_);
}

- (void)interrupt {
  sqlite3_interrupt(db_);
}

- (int)setBusyTimeoutMS:(int)timeoutMS {
  int rc = sqlite3_busy_timeout(db_, timeoutMS);
  if (rc == SQLITE_OK) {
    timeoutMS_ = timeoutMS;
  }
  return rc;
}

- (int)busyTimeoutMS {
  return timeoutMS_;
}

- (int)executeSQL:(NSString *)sql {
  int rc;
  // Sanity
  if (!sql) {
    rc = SQLITE_MISUSE;  // Reasonable return for this case
  } else {
    if (hasCFAdditions_) {
      rc = sqlite3_exec(db_,
                        [[sql precomposedStringWithCanonicalMapping]
                          UTF8String],
                        NULL, NULL, NULL);
    } else {
      rc = sqlite3_exec(db_, [sql UTF8String], NULL, NULL, NULL);
    }
  }
  return rc;
}

- (BOOL)beginDeferredTransaction {
  int err;
  err = [self executeSQL:@"BEGIN DEFERRED TRANSACTION;"];
  return (err == SQLITE_OK) ? YES : NO;
}

- (BOOL)rollback {
  int err = [self executeSQL:@"ROLLBACK TRANSACTION;"];
  return (err == SQLITE_OK) ? YES : NO;
}

- (BOOL)commit {
  int err = [self executeSQL:@"COMMIT TRANSACTION;"];
  return (err == SQLITE_OK) ? YES : NO;
}

- (NSString *)description {
  return [NSString stringWithFormat:@"<%@: %p - %@>",
          [self class], self, path_];
}
@end


#pragma mark Upper/Lower

// Private helper to handle upper/lower conversions for UTF8
static void UpperLower8(sqlite3_context *context, int argc, sqlite3_value **argv) {
  // Args
  if ((argc < 1) || (sqlite3_value_type(argv[0]) == SQLITE_NULL)) {
    // COV_NF_START
    sqlite3_result_error(context, "LOWER/UPPER CF implementation got bad args",
                         -1);
    return;
    // COV_NF_END
  }
  const char *sqlText8 = (void *)sqlite3_value_text(argv[0]);
  if (!sqlText8) {
    // COV_NF_START
    sqlite3_result_error(context, "LOWER/UPPER CF implementation no input UTF8",
                         -1);
    return;
    // COV_NF_END
  }

  // Get user data
  UpperLowerUserArgs *userArgs = sqlite3_user_data(context);
  if (!userArgs) {
    // COV_NF_START
    sqlite3_result_error(context, "LOWER/UPPER CF implementation no user args",
                         -1);
    return;
    // COV_NF_END
  }

  _GTMDevAssert(userArgs->textRep == SQLITE_UTF8,
                @"Received non UTF8 encoding in UpperLower8");

  // Worker string, must be mutable for case conversion so order our calls
  // to only copy once
  CFMutableStringRef workerString =
    CFStringCreateMutable(kCFAllocatorDefault, 0);
  GTMCFAutorelease(workerString);
  if (!workerString) {
    // COV_NF_START
    sqlite3_result_error(context,
                         "LOWER/UPPER CF implementation failed " \
                         "to allocate CFMutableStringRef", -1);
    return;
    // COV_NF_END
  }
  CFStringAppendCString(workerString, sqlText8, kCFStringEncodingUTF8);

  // Perform the upper/lower
  if (userArgs->upperCase) {
    CFStringUppercase(workerString, gCurrentLocale);
  } else {
    CFStringLowercase(workerString, gCurrentLocale);
  }

  // Convert to our canonical composition
  CFStringNormalize(workerString, kCFStringNormalizationFormC);

  // Get the bytes we will return, using the more efficient accessor if we can
  const char *returnString = CFStringGetCStringPtr(workerString,
                                                   kCFStringEncodingUTF8);
  if (returnString) {
    // COV_NF_START
    // Direct buffer, but have SQLite copy it
    sqlite3_result_text(context, returnString, -1, SQLITE_TRANSIENT);
    // COV_NF_END
  } else {
    // Need to get a copy
    CFIndex workerLength = CFStringGetLength(workerString);
    CFIndex bufferSize =
      CFStringGetMaximumSizeForEncoding(workerLength,
                                        kCFStringEncodingUTF8);
    void *returnBuffer = malloc(bufferSize);
    if (!returnBuffer) {
      // COV_NF_START
      sqlite3_result_error(context,
                           "LOWER/UPPER failed to allocate return buffer", -1);
      return;
      // COV_NF_END
    }
    CFIndex convertedBytes = 0;
    CFIndex convertedChars = CFStringGetBytes(workerString,
                                              CFRangeMake(0, workerLength),
                                              kCFStringEncodingUTF8,
                                              0,
                                              false,
                                              returnBuffer,
                                              bufferSize,
                                              &convertedBytes);
    if (convertedChars != workerLength) {
      // COV_NF_START
      free(returnBuffer);
      sqlite3_result_error(context,
                           "CFStringGetBytes() failed to " \
                           "convert all characters", -1);
      // COV_NF_END
    } else {
      // Set the result, letting SQLite take ownership and using free() as
      // the destructor
      // We cast the 3rd parameter to an int because sqlite3 doesn't appear
      // to support 64-bit mode.
      sqlite3_result_text(context, returnBuffer, (int)convertedBytes, &free);
    }
  }
}

// Private helper to handle upper/lower conversions for UTF16 variants
static void UpperLower16(sqlite3_context *context,
                         int argc, sqlite3_value **argv) {
  // Args
  if ((argc < 1) || (sqlite3_value_type(argv[0]) == SQLITE_NULL)) {
    // COV_NF_START
    sqlite3_result_error(context, "LOWER/UPPER CF implementation got bad args", -1);
    return;
    // COV_NF_END
  }

  // For UTF16 variants we want our working string to be in native-endian
  // UTF16. This gives us the fewest number of copies (since SQLite converts
  // in-place). There is no advantage to breaking out the string construction
  // to use UTF16BE or UTF16LE because all that does is move the conversion
  // work into the CFString constructor, so just use simple code.
  int sqlText16ByteCount = sqlite3_value_bytes16(argv[0]);
  const UniChar *sqlText16 = (void *)sqlite3_value_text16(argv[0]);
  if (!sqlText16ByteCount || !sqlText16) {
    // COV_NF_START
    sqlite3_result_error(context,
                         "LOWER/UPPER CF implementation no input UTF16", -1);
    return;
    // COV_NF_END
  }

  // Get user data
  UpperLowerUserArgs *userArgs = sqlite3_user_data(context);
  if (!userArgs) {
    // COV_NF_START
    sqlite3_result_error(context, "LOWER/UPPER CF implementation no user args", -1);
    return;
    // COV_NF_END
  }
  CFStringEncoding encoding = SqliteTextEncodingToCFStringEncoding(userArgs->textRep);

  // Mutable worker for upper/lower
  CFMutableStringRef workerString = CFStringCreateMutable(kCFAllocatorDefault, 0);
  GTMCFAutorelease(workerString);
  if (!workerString) {
    // COV_NF_START
    sqlite3_result_error(context,
                         "LOWER/UPPER CF implementation failed " \
                         "to allocate CFMutableStringRef", -1);
    return;
    // COV_NF_END
  }
  CFStringAppendCharacters(workerString, sqlText16,
                           sqlText16ByteCount / sizeof(UniChar));
  // Perform the upper/lower
  if (userArgs->upperCase) {
    CFStringUppercase(workerString, gCurrentLocale);
  } else {
    CFStringLowercase(workerString, gCurrentLocale);
  }
  // Convert to our canonical composition
  CFStringNormalize(workerString, kCFStringNormalizationFormC);

  // Length after normalization matters
  CFIndex workerLength = CFStringGetLength(workerString);

  // If we can give direct byte access use it
  const UniChar *returnString = CFStringGetCharactersPtr(workerString);
  if (returnString) {
    // COV_NF_START details of whether cfstringgetcharactersptr returns
    // a buffer or NULL are internal; not something we can depend on.
    // When building for Leopard+, CFIndex is a 64-bit type, which is
    // why we cast it to an int when we call the sqlite api.
    _GTMDevAssert((workerLength * sizeof(UniChar) <= INT_MAX),
                  @"sqlite methods do not support buffers greater "
                  @"than 32 bit sizes");
    // Direct access to the internal buffer, hand it to sqlite for copy and
    // conversion
    sqlite3_result_text16(context, returnString,
                          (int)(workerLength * sizeof(UniChar)),
                          SQLITE_TRANSIENT);
    // COV_NF_END
  } else {
    // Need to get a copy since we can't get direct access
    CFIndex bufferSize = CFStringGetMaximumSizeForEncoding(workerLength,
                                                           encoding);
    void *returnBuffer = malloc(bufferSize);
    if (!returnBuffer) {
      // COV_NF_START
      sqlite3_result_error(context,
                           "LOWER/UPPER CF implementation failed " \
                           "to allocate return buffer", -1);
      return;
      // COV_NF_END
    }
    CFIndex convertedBytes = 0;
    CFIndex convertedChars = CFStringGetBytes(workerString,
                                              CFRangeMake(0, workerLength),
                                              encoding,
                                              0,
                                              false,
                                              returnBuffer,
                                              bufferSize,
                                              &convertedBytes);
    if (convertedChars != workerLength) {
      // COV_NF_START
      free(returnBuffer);
      sqlite3_result_error(context,
                           "LOWER/UPPER CF implementation CFStringGetBytes() " \
                           "failed to convert all characters", -1);
      // COV_NF_END
    } else {
      // When building for Leopard+, CFIndex is a 64-bit type, but
      // sqlite3's functions all take ints.  Assert the error for dev
      // builds and cast down.
      _GTMDevAssert((convertedBytes <= INT_MAX),
                    @"sqlite methods do not support buffers greater "
                    @"than 32-bit sizes");
      int convertedBytesForSQLite = (int)convertedBytes;
      // Set the result, letting SQLite take ownership and using free() as
      // the destructor. For output since we're copying out the bytes anyway
      // we might as well use the preferred encoding of the original call.
      _GTMDevAssert(userArgs->textRep == SQLITE_UTF16BE ||
                    userArgs->textRep == SQLITE_UTF16LE,
                    @"Received non UTF8 encoding in UpperLower8");
      switch (userArgs->textRep) {
      case SQLITE_UTF16BE:
        sqlite3_result_text16be(context, returnBuffer,
                                convertedBytesForSQLite, &free);
        break;
      case SQLITE_UTF16LE:
        sqlite3_result_text16le(context, returnBuffer,
                                convertedBytesForSQLite, &free);
        break;
      default:
        free(returnBuffer);
        // COV_NF_START no way to tell sqlite to not use utf8 or utf16?
        sqlite3_result_error(context,
                             "LOWER/UPPER CF implementation " \
                             "had unhandled encoding", -1);
        // COV_NF_END
      }
    }
  }
}


#pragma mark Collations

static void CollateNeeded(void *userContext, sqlite3 *db, int textRep,
                          const char *name) {
  // Cast
  GTMSQLiteDatabase *gtmdb = (GTMSQLiteDatabase *)userContext;
  _GTMDevAssert(gtmdb, @"Invalid database parameter from sqlite");

  // Create space for the collation args
  NSMutableData *collationArgsData =
    [NSMutableData dataWithLength:sizeof(CollateUserArgs)];
  CollateUserArgs *userArgs = (CollateUserArgs *)[collationArgsData bytes];
  bzero(userArgs, sizeof(CollateUserArgs));
  userArgs->textRep = textRep;

  // Parse the name into the flags we need
  NSString *collationName =
    [[NSString stringWithUTF8String:name] lowercaseString];
  NSArray *collationComponents =
    [collationName componentsSeparatedByString:@"_"];
  NSString *collationFlag = nil;
  BOOL atLeastOneValidFlag = NO;
  for (collationFlag in collationComponents) {
    if ([collationFlag isEqualToString:@"reverse"]) {
      userArgs->reverse = YES;
      atLeastOneValidFlag = YES;
    } else if ([collationFlag isEqualToString:@"nocase"]) {
      userArgs->compareOptions |= kCFCompareCaseInsensitive;
      atLeastOneValidFlag = YES;
    } else if ([collationFlag isEqualToString:@"nonliteral"]) {
      userArgs->compareOptions |= kCFCompareNonliteral;
      atLeastOneValidFlag = YES;
    } else if ([collationFlag isEqualToString:@"localized"]) {
      userArgs->compareOptions |= kCFCompareLocalized;
      atLeastOneValidFlag = YES;
    } else if ([collationFlag isEqualToString:@"numeric"]) {
      userArgs->compareOptions |= kCFCompareNumerically;
      atLeastOneValidFlag = YES;
    } else if ([collationFlag isEqualToString:@"nodiacritic"]) {
      userArgs->compareOptions |= kCFCompareDiacriticInsensitive;
      atLeastOneValidFlag = YES;
    } else if ([collationFlag isEqualToString:@"widthinsensitive"]) {
      userArgs->compareOptions |= kCFCompareWidthInsensitive;
      atLeastOneValidFlag = YES;
    }
  }

  // No valid tokens means nothing to do
  if (!atLeastOneValidFlag) return;

  int err;
  // Add the collation
  switch (textRep) {
    case SQLITE_UTF8:
      err = sqlite3_create_collation([gtmdb sqlite3DB], name,
                                     textRep, userArgs, &Collate8);
      if (err != SQLITE_OK) return;
      break;
    case SQLITE_UTF16:
    case SQLITE_UTF16BE:
    case SQLITE_UTF16LE:
      err = sqlite3_create_collation([gtmdb sqlite3DB], name,
                                     textRep, userArgs, &Collate16);
      if (err != SQLITE_OK) return;
      break;
    default:
      return;
  }

  // Have the db retain our collate function args
  [gtmdb collationArgumentRetain:collationArgsData];
}

static int Collate8(void *userContext, int length1, const void *str1,
                    int length2, const void *str2) {
  // User args
  CollateUserArgs *userArgs = (CollateUserArgs *)userContext;
  _GTMDevAssert(userArgs, @"Invalid user arguments from sqlite");

  // Sanity and zero-lengths
  if (!(str1 && str2) || (!length1 && !length2)) {
    return kCFCompareEqualTo;  // Best we can do and stable sort
  }
  if (!length1 && length2) {
    if (userArgs->reverse) {
      return kCFCompareGreaterThan;
    } else {
      return kCFCompareLessThan;
    }
  } else if (length1 && !length2) {
    if (userArgs->reverse) {
      return kCFCompareLessThan;
    } else {
      return kCFCompareGreaterThan;
    }
  }

  // We have UTF8 strings with no terminating null, we want to compare
  // with as few copies as possible. Leopard introduced a no-copy string
  // creation function, we'll use it when we can but we want to stay compatible
  // with Tiger.
  CFStringRef string1 = NULL, string2 = NULL;
  string1 = CFStringCreateWithBytesNoCopy(kCFAllocatorDefault,
                                          str1,
                                          length1,
                                          kCFStringEncodingUTF8,
                                          false,
                                          kCFAllocatorNull);
  string2 = CFStringCreateWithBytesNoCopy(kCFAllocatorDefault,
                                          str2,
                                          length2,
                                          kCFStringEncodingUTF8,
                                          false,
                                          kCFAllocatorNull);
  GTMCFAutorelease(string1);
  GTMCFAutorelease(string2);
  // Allocation failures can't really be sanely handled from a collator
  int sqliteResult;
  if (!(string1 && string2)) {
    // COV_NF_START
    sqliteResult = (int)kCFCompareEqualTo;
    // COV_NF_END
  } else {
    // Compare
    // We have to cast to int because SQLite takes functions that
    // return an int, but when compiling for Leopard+,
    // CFComparisonResult is a signed long, but on Tiger it's an int
    CFComparisonResult result;
    result = CFStringCompare(string1,
                             string2,
                             userArgs->compareOptions);
    sqliteResult = (int)result;
    // Reverse
    if (userArgs->reverse && sqliteResult) {
      sqliteResult = -sqliteResult;
    }

  }
  return sqliteResult;
}

static int Collate16(void *userContext, int length1, const void *str1,
                     int length2, const void *str2) {
  // User args
  CollateUserArgs *userArgs = (CollateUserArgs *)userContext;
  _GTMDevAssert(userArgs, @"Invalid user arguments from sqlite");

  // Sanity and zero-lengths
  if (!(str1 && str2) || (!length1 && !length2)) {
    return kCFCompareEqualTo;  // Best we can do and stable sort
  }
  if (!length1 && length2) {
    if (userArgs->reverse) {
      return kCFCompareGreaterThan;
    } else {
      return kCFCompareLessThan;
    }
  } else if (length1 && !length2) {
    if (userArgs->reverse) {
      return kCFCompareLessThan;
    } else {
      return kCFCompareGreaterThan;
    }
  }

  // Target encoding
  CFStringEncoding encoding =
    SqliteTextEncodingToCFStringEncoding(userArgs->textRep);

  // We have UTF16 strings, we want to compare with as few copies as
  // possible.  Since endianness matters we want to use no-copy
  // variants where possible and copy (and endian convert) only when
  // we must.
  CFStringRef string1 = NULL, string2 = NULL;
  if ((userArgs->textRep == SQLITE_UTF16) ||
#if TARGET_RT_BIG_ENDIAN
      (userArgs->textRep == SQLITE_UTF16BE)
#else
      (userArgs->textRep == SQLITE_UTF16LE)
#endif
  ) {
    string1 = CFStringCreateWithCharactersNoCopy(kCFAllocatorDefault,
                                                 str1,
                                                 length1 / sizeof(UniChar),
                                                 kCFAllocatorNull);
    string2 = CFStringCreateWithCharactersNoCopy(kCFAllocatorDefault,
                                                 str2,
                                                 length2 / sizeof(UniChar),
                                                 kCFAllocatorNull);
  } else {
    // No point in using the "no copy" version of the call here. If the
    // bytes were in the native order we'd be in the other part of this
    // conditional, so we know we have to copy the string to endian convert
    // it.
    string1 = CFStringCreateWithBytes(kCFAllocatorDefault,
                                      str1,
                                      length1,
                                      encoding,
                                      false);
    string2 = CFStringCreateWithBytes(kCFAllocatorDefault,
                                      str2,
                                      length2,
                                      encoding,
                                      false);
  }

  GTMCFAutorelease(string1);
  GTMCFAutorelease(string2);
  int sqliteResult;
  // Allocation failures can't really be sanely handled from a collator
  if (!(string1 && string2)) {
    // COV_NF_START
    sqliteResult = (int)kCFCompareEqualTo;
    // COV_NF_END
  } else {
    // Compare
    // We cast the return value to an int because CFComparisonResult
    // is a long in Leopard+ builds.  I have no idea why we need
    // 64-bits for a 3-value enum, but that's how it is...
    CFComparisonResult result;
    result = CFStringCompare(string1,
                             string2,
                             userArgs->compareOptions);

    sqliteResult = (int)result;
    //Reverse
    if (userArgs->reverse && sqliteResult) {
      sqliteResult = -sqliteResult;
    }
  }

  return sqliteResult;
}


#pragma mark Like/Glob

// Private helper to handle LIKE and GLOB with different encodings. This
// is essentially a reimplementation of patternCompare() in func.c of the
// SQLite sources.
static void LikeGlobCompare(sqlite3_context *context,
                            CFStringRef pattern,
                            CFStringRef targetString,
                            UniChar matchAll,
                            UniChar matchOne,
                            UniChar escape,
                            BOOL setSupport,
                            CFOptionFlags compareOptions) {
  // Setup for pattern walk
  CFIndex patternLength = CFStringGetLength(pattern);
  CFStringInlineBuffer patternBuffer;
  CFStringInitInlineBuffer(pattern,
                           &patternBuffer,
                           CFRangeMake(0, patternLength));
  UniChar patternChar;
  CFIndex patternIndex = 0;
  CFIndex targetStringLength = CFStringGetLength(targetString);
  CFIndex targetStringIndex = 0;
  BOOL isAnchored = YES;

  size_t dataSize = patternLength * sizeof(UniChar);
  NSMutableData *tempData = [NSMutableData dataWithLength:dataSize];
  // Temp string buffer can be no larger than the whole pattern
  UniChar *findBuffer = [tempData mutableBytes];
  if (!findBuffer) {
    // COV_NF_START
    sqlite3_result_error(context,
                         "LIKE or GLOB CF implementation failed to " \
                         "allocate temporary buffer", -1);
    return;
    // COV_NF_END
  }

  // We'll use a mutable string we can just reset as we wish
  CFMutableStringRef findString =
    CFStringCreateMutableWithExternalCharactersNoCopy(kCFAllocatorDefault,
                                                      NULL,
                                                      0,
                                                      0,
                                                      kCFAllocatorNull);
  GTMCFAutorelease(findString);
  if (!findString) {
    // COV_NF_START
    sqlite3_result_error(context,
                         "LIKE or GLOB CF implementation failed to " \
                         "allocate temporary CFString", -1);
    return;
    // COV_NF_END
  }
  // Walk the pattern
  while (patternIndex < patternLength) {
    patternChar = CFStringGetCharacterFromInlineBuffer(&patternBuffer,
                                                       patternIndex);
    // Match all character has no effect other than to unanchor the search
    if (patternChar == matchAll) {
      isAnchored = NO;
      patternIndex++;
      continue;
    }
    // Match one character pushes the string index forward by one composed
    // character
    if (patternChar == matchOne) {
      // If this single char match would walk us off the end of the string
      // we're already done, no match
      if (targetStringIndex >= targetStringLength) {
        sqlite3_result_int(context, 0);
        return;
      }
      // There's still room in the string, so move the string index forward one
      // composed character and go back around.
      CFRange nextCharRange =
        CFStringGetRangeOfComposedCharactersAtIndex(targetString,
                                                    targetStringIndex);
      targetStringIndex = nextCharRange.location + nextCharRange.length;
      patternIndex++;
      continue;
    }
    // Character set matches require the parsing of the character set
    if (setSupport && (patternChar == 0x5B)) {  // "["
      // A character set must match one character, if there's not at least one
      // character left in the string, don't bother
      if (targetStringIndex >= targetStringLength) {
        sqlite3_result_int(context, 0);
        return;
      }
      // There's at least one character, try to match the remainder of the
      // string using a CFCharacterSet
      CFMutableCharacterSetRef charSet
        = CFCharacterSetCreateMutable(kCFAllocatorDefault);
      GTMCFAutorelease(charSet);
      if (!charSet) {
        // COV_NF_START
        sqlite3_result_error(context,
                             "LIKE or GLOB CF implementation failed to " \
                             "allocate temporary CFMutableCharacterSet", -1);
        return;
        // COV_NF_END
      }

      BOOL invert = NO;
      // Walk one character forward
      patternIndex++;
      if (patternIndex >= patternLength) {
        // Oops, out of room
        sqlite3_result_error(context,
                             "LIKE or GLOB CF implementation found " \
                             "unclosed character set", -1);
        return;
      }
      // First character after pattern open is special-case
      patternChar = CFStringGetCharacterFromInlineBuffer(&patternBuffer,
                                                         patternIndex);
      if (patternChar == 0x5E) {  // "^"
        invert = YES;
        // Bump forward one character, can still be an unescaped "]" after
        // negation
        patternIndex++;
        if (patternIndex >= patternLength) {
          // Oops, out of room
          sqlite3_result_error(context,
                               "LIKE or GLOB CF implementation found " \
                               "unclosed character set after negation", -1);
          return;
        }
        patternChar = CFStringGetCharacterFromInlineBuffer(&patternBuffer,
                                                           patternIndex);
      }
      // First char in set or first char in negation can be a literal "]" not
      // considered a close
      if (patternChar == 0x5D) {  // "]"
        CFCharacterSetAddCharactersInRange(charSet,
                                           CFRangeMake(patternChar, 1));
        patternIndex++;
        if (patternIndex >= patternLength) {
          // Oops, out of room
          sqlite3_result_error(context,
                               "LIKE or GLOB CF implementation found " \
                               "unclosed character set after escaped ]", -1);
          return;
        }
        patternChar = CFStringGetCharacterFromInlineBuffer(&patternBuffer,
                                                           patternIndex);
      }
      while ((patternIndex < patternLength) &&
             patternChar &&
             (patternChar != 0x5D)) {  // "]"
        // Check for possible character range, for this to be true we
        // must have a hyphen at the next position and at least 3
        // characters of room (for hyphen, range end, and set
        // close). Hyphens at the end without a trailing range are
        // treated as literals
        if (((patternLength - patternIndex) >= 3) &&
            // Second char must be "-"
            (CFStringGetCharacterFromInlineBuffer(&patternBuffer,
                                                  // 0x2D is "-"
                                                  patternIndex + 1) == 0x2D) &&
            // And third char must be anything other than set close in
            // case the hyphen is at the end of the set and needs to
            // be treated as a literal
            (CFStringGetCharacterFromInlineBuffer(&patternBuffer,
                                                  patternIndex + 2)
             != 0x5D)) {  // "]"
          // Get the range close
          UniChar rangeClose =
            CFStringGetCharacterFromInlineBuffer(&patternBuffer,
                                                 patternIndex + 2);
          // Add the whole range
          int rangeLen = rangeClose - patternChar + 1;
          CFCharacterSetAddCharactersInRange(charSet,
                                             CFRangeMake(patternChar,
                                                         rangeLen));
          // Move past the end of the range
          patternIndex += 3;
        } else {
          // Single Raw character
          CFCharacterSetAddCharactersInRange(charSet,
                                             CFRangeMake(patternChar, 1));
          patternIndex++;
        }
        // Load next char for loop
        if (patternIndex < patternLength) {
          patternChar =
            CFStringGetCharacterFromInlineBuffer(&patternBuffer, patternIndex);
        } else {
          patternChar = 0;
        }
      }
      // Check for closure
      if (patternChar != 0x5D) {  // "]"
        sqlite3_result_error(context,
                             "LIKE or GLOB CF implementation found " \
                             "unclosed character set", -1);
        return;
      } else {
        // Increment past the end of the set
        patternIndex++;
      }
      // Invert the set if needed
      if (invert) CFCharacterSetInvert(charSet);
      // Do the search
      CFOptionFlags findOptions = 0;
      if (isAnchored) findOptions |= kCFCompareAnchored;
      CFRange foundRange;
      unsigned long rangeLen = targetStringLength - targetStringIndex;
      BOOL found = CFStringFindCharacterFromSet(targetString,
                                                charSet,
                                                CFRangeMake(targetStringIndex,
                                                            rangeLen),
                                                findOptions,
                                                &foundRange);
      // If no match then the whole pattern fails
      if (!found) {
        sqlite3_result_int(context, 0);
        return;
      }
      // If we did match then we need to push the string index to the
      // character past the end of the match and then go back around
      // the loop.
      targetStringIndex = foundRange.location + foundRange.length;
      // At this point patternIndex is either at the end of the
      // string, or at the next special character which will be picked
      // up and handled at the top of the loop. We do, however, need
      // to reset the anchor status
      isAnchored = YES;
      // End of character sets, back around
      continue;
    }
    // Otherwise the pattern character is a normal or escaped
    // character we should consume and match with normal string
    // matching
    CFIndex findBufferIndex = 0;
    while ((patternIndex < patternLength) && patternChar &&
           !((patternChar == matchAll) || (patternChar == matchOne) ||
           (setSupport && (patternChar == 0x5B)))) {  // "["
      if (patternChar == escape) {
        // No matter what the character follows the escape copy it to the
        // buffer
        patternIndex++;
        if (patternIndex >= patternLength) {
          // COV_NF_START
          // Oops, escape came at end of pattern, that's an error
          sqlite3_result_error(context,
                               "LIKE or GLOB CF implementation found " \
                               "escape character at end of pattern", -1);
          return;
          // COV_NF_END
        }
        patternChar = CFStringGetCharacterFromInlineBuffer(&patternBuffer,
                                                           patternIndex);
      }
      // At this point the patternChar is either the escaped character or the
      // original normal character
      findBuffer[findBufferIndex++] = patternChar;
      // Set up for next loop
      patternIndex++;
      if (patternIndex < patternLength) {
        patternChar = CFStringGetCharacterFromInlineBuffer(&patternBuffer,
                                                           patternIndex);
      } else {
        patternChar = 0;
      }
    }
    // On loop exit we have a string ready for comparision, if that
    // string is too long then it can't be a match.
    if (findBufferIndex > (targetStringLength - targetStringIndex)) {
      sqlite3_result_int(context, 0);
      return;
    }

    // We actually need to do a comparison
    CFOptionFlags findOptions = compareOptions;
    if (isAnchored) findOptions |= kCFCompareAnchored;
    CFStringSetExternalCharactersNoCopy(findString,
                                        findBuffer,
                                        findBufferIndex,
                                        findBufferIndex);
    CFRange foundRange;
    unsigned long rangeLen = targetStringLength - targetStringIndex;
    BOOL found = CFStringFindWithOptions(targetString,
                                         findString,
                                         CFRangeMake(targetStringIndex,
                                                     rangeLen),
                                         findOptions,
                                         &foundRange);
    // If no match then the whole pattern fails
    if (!found) {
      sqlite3_result_int(context, 0);
      return;
    }
    // If we did match then we need to push the string index to the
    // character past the end of the match and then go back around the
    // loop.
    targetStringIndex = foundRange.location + foundRange.length;
    // At this point patternIndex is either at the end of the string,
    // or at the next special character which will be picked up and
    // handled at the top of the loop. We do, however, need to reset
    // the anchor status
    isAnchored = YES;
  }
  // On loop exit all pattern characters have been considered. If we're still
  // alive it means that we've matched the entire pattern, except for trailing
  // wildcards, we need to handle that case.
  if (isAnchored) {
    // If we're still anchored there was no trailing matchAll, in which case
    // we have to have run to exactly the end of the string
    if (targetStringIndex == targetStringLength) {
      sqlite3_result_int(context, 1);
    } else {
      sqlite3_result_int(context, 0);
    }
  } else {
    // If we're not anchored any remaining characters are OK
    sqlite3_result_int(context, 1);
  }
}

static void Like8(sqlite3_context *context, int argc, sqlite3_value **argv) {
  // Get our LIKE options
  LikeGlobUserArgs *likeArgs = sqlite3_user_data(context);
  if (!likeArgs) {
    // COV_NF_START
    sqlite3_result_error(context, "LIKE CF implementation no user args", -1);
    return;
    // COV_NF_END
  }

  // Read the strings
  const char *pattern = (const char *)sqlite3_value_text(argv[0]);
  const char *target = (const char *)sqlite3_value_text(argv[1]);
  if (!pattern || !target) {
    // COV_NF_START
    sqlite3_result_error(context,
                         "LIKE CF implementation missing pattern or value", -1);
    return;
    // COV_NF_END
  }
  CFStringRef patternString =
    CFStringCreateWithCStringNoCopy(kCFAllocatorDefault,
                                    pattern,
                                    kCFStringEncodingUTF8,
                                    kCFAllocatorNull);
  CFStringRef targetString =
    CFStringCreateWithCStringNoCopy(kCFAllocatorDefault,
                                    target,
                                    kCFStringEncodingUTF8,
                                    kCFAllocatorNull);
  GTMCFAutorelease(patternString);
  GTMCFAutorelease(targetString);
  if (!(patternString && targetString)) {
    // COV_NF_START
    sqlite3_result_error(context,
                         "LIKE CF implementation failed " \
                         "to allocate CFStrings", -1);
    return;
    // COV_NF_END
  }

  UniChar escapeChar = 0;
  // If there is a third argument it is the escape character
  if (argc == 3) {
    const char *escape = (const char *)sqlite3_value_text(argv[2]);
    if (!escape) {
      sqlite3_result_error(context,
                           "LIKE CF implementation missing " \
                           "escape character", -1);
      return;
    }
    CFStringRef escapeString =
      CFStringCreateWithCStringNoCopy(kCFAllocatorDefault,
                                      escape,
                                      kCFStringEncodingUTF8,
                                      kCFAllocatorNull);
    GTMCFAutorelease(escapeString);
    if (!escapeString) {
      // COV_NF_START
      sqlite3_result_error(context,
                           "LIKE CF implementation failed to " \
                           "allocate CFString for ESCAPE", -1);
      return;
      // COV_NF_END
    }
    if (CFStringGetLength(escapeString) != 1) {
      sqlite3_result_error(context,
                           "CF implementation ESCAPE expression " \
                           "must be single character", -1);
      return;
    }
    escapeChar = CFStringGetCharacterAtIndex(escapeString, 0);
  }

  // Do the compare
  LikeGlobCompare(context,
                  patternString,
                  targetString,
                  0x25,  // %
                  0x5F,  // _
                  escapeChar,
                  NO,  // LIKE does not support character sets
                  *(likeArgs->compareOptionPtr));
}

static void Like16(sqlite3_context *context, int argc, sqlite3_value **argv) {
  // Get our LIKE options
  LikeGlobUserArgs *likeArgs = sqlite3_user_data(context);
  if (!likeArgs) {
    // COV_NF_START - sql parser chokes if we feed any input
    // that could trigger this
    sqlite3_result_error(context, "LIKE CF implementation no user args", -1);
    return;
    // COV_NF_END
  }

  // For UTF16 variants we want our working string to be in native-endian
  // UTF16. This gives us the fewest number of copies (since SQLite converts
  // in-place). There is no advantage to breaking out the string construction
  // to use UTF16BE or UTF16LE because all that does is move the conversion
  // work into the CFString constructor, so just use simple code.
  int patternByteCount = sqlite3_value_bytes16(argv[0]);
  const UniChar *patternText = (void *)sqlite3_value_text16(argv[0]);
  int targetByteCount = sqlite3_value_bytes16(argv[1]);
  const UniChar *targetText = (void *)sqlite3_value_text16(argv[1]);
  if (!patternByteCount || !patternText || !targetByteCount || !targetText) {
    // COV_NF_START
    sqlite3_result_error(context,
                         "LIKE CF implementation missing pattern or value", -1);
    return;
    // COV_NF_END
  }
  CFStringRef patternString =
    CFStringCreateWithCharactersNoCopy(kCFAllocatorDefault,
                                       patternText,
                                       patternByteCount / sizeof(UniChar),
                                       kCFAllocatorNull);
  CFStringRef targetString =
    CFStringCreateWithCharactersNoCopy(kCFAllocatorDefault,
                                       targetText,
                                       targetByteCount / sizeof(UniChar),
                                       kCFAllocatorNull);
  GTMCFAutorelease(patternString);
  GTMCFAutorelease(targetString);
  if (!(patternString && targetString)) {
    // COV_NF_START
    sqlite3_result_error(context,
                         "LIKE CF implementation failed " \
                         "to allocate CFStrings", -1);
    return;
    // COV_NF_END
  }

  // If there is a third argument it is the escape character, force a
  // UTF8 conversion for simplicity
  UniChar escapeChar = 0;
  if (argc == 3) {
    const char *escape = (const char *)sqlite3_value_text(argv[2]);
    if (!escape) {
      sqlite3_result_error(context,
                           "LIKE CF implementation " \
                           "missing escape character", -1);
      return;
    }
    CFStringRef escapeString =
      CFStringCreateWithCStringNoCopy(kCFAllocatorDefault,
                                      escape,
                                      kCFStringEncodingUTF8,
                                      kCFAllocatorNull);
    GTMCFAutorelease(escapeString);
    if (!escapeString) {
      // COV_NF_START
      sqlite3_result_error(context,
                           "LIKE CF implementation failed to " \
                           "allocate CFString for ESCAPE", -1);
      return;
      // COV_NF_END
    }
    if (CFStringGetLength(escapeString) != 1) {
      sqlite3_result_error(context,
                           "CF implementation ESCAPE expression " \
                           "must be single character", -1);
      return;
    }
    escapeChar = CFStringGetCharacterAtIndex(escapeString, 0);
  }

  // Do the compare
  LikeGlobCompare(context,
                  patternString,
                  targetString,
                  0x25,  // %
                  0x5F,  // _
                  escapeChar,
                  NO,  // LIKE does not support character sets
                  *(likeArgs->compareOptionPtr));
}

static void Glob8(sqlite3_context *context, int argc, sqlite3_value **argv) {
  // Get our GLOB options
  LikeGlobUserArgs *globArgs = sqlite3_user_data(context);
  if (!globArgs) {
    // COV_NF_START
    sqlite3_result_error(context, "GLOB CF implementation no user args", -1);
    return;
    // COV_NF_END
  }

  // Read the strings
  const char *pattern = (const char *)sqlite3_value_text(argv[0]);
  const char *target = (const char *)sqlite3_value_text(argv[1]);
  if (!pattern || !target) {
    // COV_NF_START
    sqlite3_result_error(context,
                         "GLOB CF implementation missing " \
                         "pattern or value", -1);
    return;
    // COV_NF_END
  }
  CFStringRef patternString =
    CFStringCreateWithCStringNoCopy(kCFAllocatorDefault,
                                    pattern,
                                    kCFStringEncodingUTF8,
                                    kCFAllocatorNull);
  CFStringRef targetString =
    CFStringCreateWithCStringNoCopy(kCFAllocatorDefault,
                                    target,
                                    kCFStringEncodingUTF8,
                                    kCFAllocatorNull);
  GTMCFAutorelease(patternString);
  GTMCFAutorelease(targetString);

  if (!(patternString && targetString)) {
    // COV_NF_START
    sqlite3_result_error(context,
                         "GLOB CF implementation failed to " \
                         "allocate CFStrings", -1);
    // COV_NF_END
  } else {
    // Do the compare
    LikeGlobCompare(context,
                    patternString,
                    targetString,
                    0x2A,  // *
                    0x3F,  // ?
                    0,     // GLOB does not support escape characters
                    YES,   // GLOB supports character sets
                    *(globArgs->compareOptionPtr));
  }
}

static void Glob16(sqlite3_context *context, int argc, sqlite3_value **argv) {
  // Get our GLOB options
  LikeGlobUserArgs *globArgs = sqlite3_user_data(context);
  if (!globArgs) {
    // COV_NF_START
    sqlite3_result_error(context, "GLOB CF implementation no user args", -1);
    return;
    // COV_NF_END
  }

  // For UTF16 variants we want our working string to be in
  // native-endian UTF16. This gives us the fewest number of copies
  // (since SQLite converts in-place). There is no advantage to
  // breaking out the string construction to use UTF16BE or UTF16LE
  // because all that does is move the conversion work into the
  // CFString constructor, so just use simple code.
  int patternByteCount = sqlite3_value_bytes16(argv[0]);
  const UniChar *patternText = (void *)sqlite3_value_text16(argv[0]);
  int targetByteCount = sqlite3_value_bytes16(argv[1]);
  const UniChar *targetText = (void *)sqlite3_value_text16(argv[1]);
  if (!patternByteCount || !patternText || !targetByteCount || !targetText) {
    // COV_NF_START
    sqlite3_result_error(context,
                         "GLOB CF implementation missing pattern or value", -1);
    return;
    // COV_NF_END
  }
  CFStringRef patternString =
    CFStringCreateWithCharactersNoCopy(kCFAllocatorDefault,
                                       patternText,
                                       patternByteCount / sizeof(UniChar),
                                       kCFAllocatorNull);
  CFStringRef targetString =
    CFStringCreateWithCharactersNoCopy(kCFAllocatorDefault,
                                       targetText,
                                       targetByteCount / sizeof(UniChar),
                                       kCFAllocatorNull);
  GTMCFAutorelease(patternString);
  GTMCFAutorelease(targetString);
  if (!(patternString && targetString)) {
    // COV_NF_START
    sqlite3_result_error(context,
                         "GLOB CF implementation failed to  "\
                         "allocate CFStrings", -1);
    // COV_NF_END
  } else {
    // Do the compare
    LikeGlobCompare(context,
                    patternString,
                    targetString,
                    0x2A,  // *
                    0x3F,  // ?
                    0,     // GLOB does not support escape characters
                    YES,   // GLOB supports character sets
                    *(globArgs->compareOptionPtr));
  }
}

// -----------------------------------------------------------------------------

@implementation GTMSQLiteStatement

#pragma mark Creation, Access and Finalization

+ (id)statementWithSQL:(NSString *)sql
            inDatabase:(GTMSQLiteDatabase *)gtmdb
             errorCode:(int *)err {
  return [[[GTMSQLiteStatement alloc] initWithSQL:sql
                                       inDatabase:gtmdb
                                        errorCode:err]
            autorelease];
}

- (id)initWithSQL:(NSString *)sql
       inDatabase:(GTMSQLiteDatabase *)gtmdb
        errorCode:(int *)err {
  int rc;
  id obj;
  if ((self = [super init])) {
    // Sanity
    obj = self;
    if (sql && gtmdb) {
      // Find out if the database is using our CF extensions
      hasCFAdditions_ = [gtmdb hasCFAdditions];

      // Prepare
      if (hasCFAdditions_) {
        sql = [sql precomposedStringWithCanonicalMapping];
      }
      if (sql) {
        rc = sqlite3_prepare([gtmdb sqlite3DB],
                             [sql UTF8String],
                             -1,
                             &statement_,
                             NULL);
        if (rc != SQLITE_OK) {
          [self release];
          obj = nil;
        }
      } else {
        // COV_NF_START
        rc = SQLITE_INTERNAL;
        [self release];
        obj = nil;
        // COV_NF_END
      }
    } else {
      rc = SQLITE_MISUSE;
      [self release];
      obj = nil;
    }
  } else {
    // COV_NF_START
    rc = SQLITE_INTERNAL;
    obj = nil;
    // COV_NF_END
  }
  if (err) *err = rc;
  return obj;
}

- (void)dealloc {
  if (statement_) {
    _GTMDevLog(@"-[GTMSQLiteStatement finalizeStatement] must be called when"
               @" statement is no longer needed");
  }
  [super dealloc];
}

- (sqlite3_stmt *)sqlite3Statement {
  return statement_;
}

- (int)finalizeStatement {
  if (!statement_) return SQLITE_MISUSE;
  int rc = sqlite3_finalize(statement_);
  statement_ = NULL;
  return rc;
}

#pragma mark Parameters and Binding

- (int)parameterCount {
  if (!statement_) return -1;
  return sqlite3_bind_parameter_count(statement_);
}

- (int)positionOfParameterNamed:(NSString *)paramName {
  if (!statement_) return -1;
  if (hasCFAdditions_) {
    NSString *cleanedString =
      [paramName precomposedStringWithCanonicalMapping];
    if (!cleanedString) return -1;
    return sqlite3_bind_parameter_index(statement_, [cleanedString UTF8String]);
  } else {
    return sqlite3_bind_parameter_index(statement_, [paramName UTF8String]);
  }
}

- (NSString *)nameOfParameterAtPosition:(int)position {
  if ((position < 1) || !statement_) return nil;
  const char *name = sqlite3_bind_parameter_name(statement_, position);
  if (!name) return nil;
  NSString *nameString = [NSString stringWithUTF8String:name];
  if (hasCFAdditions_) {
    return [nameString precomposedStringWithCanonicalMapping];
  } else {
    return nameString;
  }
}

- (int)bindSQLNullAtPosition:(int)position {
  if (!statement_) return SQLITE_MISUSE;
  return sqlite3_bind_null(statement_, position);
}

- (int)bindBlobAtPosition:(int)position bytes:(void *)bytes length:(int)length {
  if (!statement_ || !bytes || !length) return SQLITE_MISUSE;
  return sqlite3_bind_blob(statement_,
                           position,
                           bytes,
                           length,
                           SQLITE_TRANSIENT);
}

- (int)bindBlobAtPosition:(int)position data:(NSData *)data {
  if (!statement_ || !data || !position) return SQLITE_MISUSE;
  int blobLength = (int)[data length];
  _GTMDevAssert((blobLength < INT_MAX),
                @"sqlite methods do not support data lengths "
                @"exceeding 32 bit sizes");
  return [self bindBlobAtPosition:position
                            bytes:(void *)[data bytes]
                           length:blobLength];
}

- (int)bindDoubleAtPosition:(int)position value:(double)value {
  if (!statement_) return SQLITE_MISUSE;
  return sqlite3_bind_double(statement_, position, value);
}

- (int)bindNumberAsDoubleAtPosition:(int)position number:(NSNumber *)number {
  if (!number || !statement_) return SQLITE_MISUSE;
  return sqlite3_bind_double(statement_, position, [number doubleValue]);
}

- (int)bindInt32AtPosition:(int)position value:(int)value {
  if (!statement_) return SQLITE_MISUSE;
  return sqlite3_bind_int(statement_, position, value);
}

- (int)bindNumberAsInt32AtPosition:(int)position number:(NSNumber *)number {
  if (!number || !statement_) return SQLITE_MISUSE;
  return sqlite3_bind_int(statement_, position, [number intValue]);
}

- (int)bindLongLongAtPosition:(int)position value:(long long)value {
  if (!statement_) return SQLITE_MISUSE;
  return sqlite3_bind_int64(statement_, position, value);
}

- (int)bindNumberAsLongLongAtPosition:(int)position number:(NSNumber *)number {
  if (!number || !statement_) return SQLITE_MISUSE;
  return sqlite3_bind_int64(statement_, position, [number longLongValue]);
}

- (int)bindStringAtPosition:(int)position string:(NSString *)string {
  if (!string || !statement_) return SQLITE_MISUSE;
  if (hasCFAdditions_) {
    string = [string precomposedStringWithCanonicalMapping];
    if (!string) return SQLITE_INTERNAL;
  }
  return sqlite3_bind_text(statement_,
                           position,
                           [string UTF8String],
                           -1,
                           SQLITE_TRANSIENT);
}

#pragma mark Results

- (int)resultColumnCount {
  if (!statement_) return -1;
  return sqlite3_column_count(statement_);
}

- (NSString *)resultColumnNameAtPosition:(int)position {
  if (!statement_) return nil;
  const char *name = sqlite3_column_name(statement_, position);
  if (!name) return nil;
  NSString *nameString = [NSString stringWithUTF8String:name];
  if (hasCFAdditions_) {
    return [nameString precomposedStringWithCanonicalMapping];
  } else {
    return nameString;
  }
}

- (int)rowDataCount {
  if (!statement_) return -1;
  return sqlite3_data_count(statement_);
}

- (int)resultColumnTypeAtPosition:(int)position {
  if (!statement_) return -1;
  return sqlite3_column_type(statement_, position);
}

- (NSData *)resultBlobDataAtPosition:(int)position {
  if (!statement_) return nil;
  const void *bytes = sqlite3_column_blob(statement_, position);
  int length = sqlite3_column_bytes(statement_, position);
  if (!(bytes && length)) return nil;
  return [NSData dataWithBytes:bytes length:length];
}

- (double)resultDoubleAtPosition:(int)position {
  if (!statement_) return 0;
  return sqlite3_column_double(statement_, position);
}

- (int)resultInt32AtPosition:(int)position {
  if (!statement_) return 0;
  return sqlite3_column_int(statement_, position);
}

- (long long)resultLongLongAtPosition:(int)position {
  if (!statement_) return 0;
  return sqlite3_column_int64(statement_, position);
}

- (NSNumber *)resultNumberAtPosition:(int)position {
  if (!statement_) return nil;
  int type = [self resultColumnTypeAtPosition:position];
  if (type == SQLITE_FLOAT) {
    // Special case for floats
    return [NSNumber numberWithDouble:[self resultDoubleAtPosition:position]];
  } else {
    // Everything else is cast to int
    long long result = [self resultLongLongAtPosition:position];
    return [NSNumber numberWithLongLong:result];
  }
}

- (NSString *)resultStringAtPosition:(int)position {
  if (!statement_) return nil;
  const char *text = (const char *)sqlite3_column_text(statement_, position);
  if (!text) return nil;
  NSString *result = [NSString stringWithUTF8String:text];
  if (hasCFAdditions_) {
    return [result precomposedStringWithCanonicalMapping];
  } else {
    return result;
  }
}

- (id)resultFoundationObjectAtPosition:(int)position {
  if (!statement_) return nil;
  int type = [self resultColumnTypeAtPosition:position];
  id result = nil;
  switch (type) {
    case SQLITE_INTEGER:
    case SQLITE_FLOAT:
      result = [self resultNumberAtPosition:position];
      break;
    case SQLITE_TEXT:
      result = [self resultStringAtPosition:position];
      break;
    case SQLITE_BLOB:
      result = [self resultBlobDataAtPosition:position];
      break;
    case SQLITE_NULL:
      result = [NSNull null];
      break;
  }
  return result;
}

- (NSArray *)resultRowArray {
  int count = [self rowDataCount];
  if (count < 1) return nil;

  NSMutableArray *finalArray = [NSMutableArray array];
  for (int i = 0; i < count; i++) {
    id coldata = [self resultFoundationObjectAtPosition:i];
    if (!coldata) return nil;  // Oops
    [finalArray addObject:coldata];
  }

  if (![finalArray count]) return nil;
  return finalArray;
}

- (NSDictionary *)resultRowDictionary {
  int count = [self rowDataCount];
  if (count < 1) return nil;

  NSMutableDictionary *finalDict = [NSMutableDictionary dictionary];
  for (int i = 0; i < count; i++) {
    id coldata = [self resultFoundationObjectAtPosition:i];
    NSString *colname = [self resultColumnNameAtPosition:i];
    if (!(coldata && colname)) continue;
    [finalDict setObject:coldata forKey:colname];
  }
  if (![finalDict count]) return nil;
  return finalDict;
}

#pragma mark Rows

- (int)stepRow {
  int rc = SQLITE_BUSY;
  while (rc == SQLITE_BUSY) {
    rc = [self stepRowWithTimeout];
  }
  return rc;
}

- (int)stepRowWithTimeout {
  if (!statement_) return SQLITE_MISUSE;
  return sqlite3_step(statement_);
}

- (int)reset {
  if (!statement_) return SQLITE_MISUSE;
  return sqlite3_reset(statement_);
}

+ (BOOL)isCompleteStatement:(NSString *)statement {
  BOOL isComplete = NO;
  if (statement) {
    isComplete = (sqlite3_complete([statement UTF8String]) ? YES : NO);
  }
  return isComplete;
}

+ (NSString*)quoteAndEscapeString:(NSString *)string {
  char *quoted = sqlite3_mprintf("'%q'", [string UTF8String]);
  if (!quoted) return nil;
  NSString *quotedString = [NSString stringWithUTF8String:quoted];
  sqlite3_free(quoted);
  return quotedString;
}

@end
