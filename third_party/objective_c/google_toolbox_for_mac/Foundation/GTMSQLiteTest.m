//
//  GTMSQLiteTest.m
//
//  Copyright 2006-2008 Google Inc.
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


#import "GTMSQLite.h"
#import "GTMSenTestCase.h"

@interface GTMSQLiteTest : GTMTestCase
@end

// This variable is used by a custom upper function that we set in a
// SQLite database to indicate that the custom function was
// successfully called.  It has to be a global rather than instance
// variable because the custom upper function is not an instance function
static BOOL customUpperFunctionCalled =  NO;

@interface GTMSQLiteStatementTest : GTMTestCase
@end

// Prototype for LIKE/GLOB test helper
static NSArray* LikeGlobTestHelper(GTMSQLiteDatabase *db, NSString *sql);

@implementation GTMSQLiteTest

// Test cases for change counting
- (void)testTransactionAPI {
  int err;
  GTMSQLiteDatabase *db =
    [[[GTMSQLiteDatabase alloc] initInMemoryWithCFAdditions:YES
                                                       utf8:YES
                                                  errorCode:&err]
      autorelease];

  err = [db executeSQL:@"CREATE TABLE foo (bar TEXT COLLATE NOCASE_NONLITERAL);"];
  XCTAssertEqual(err, SQLITE_OK, @"Failed to create table");

  int changeCount = [db lastChangeCount];
  XCTAssertEqual(changeCount, 0,
                 @"Change count was not 0 after creating database/table!");

  err = [db executeSQL:@"insert into foo (bar) values ('blah!');"];
  XCTAssertEqual(err, SQLITE_OK, @"Failed to execute SQL");

  changeCount = [db lastChangeCount];
  XCTAssertEqual(changeCount, 1, @"Change count was not 1!");

  // Test last row id!
  unsigned long long lastRowId;
  lastRowId = [db lastInsertRowID];
  XCTAssertEqual(lastRowId, (unsigned long long)1L,
                 @"First row in database was not 1?");

  // Test setting busy and retrieving it!
  int busyTimeout = 10000;
  err = [db setBusyTimeoutMS:busyTimeout];
  XCTAssertEqual(err, SQLITE_OK, @"Error setting busy timeout");

  int retrievedBusyTimeout;
  retrievedBusyTimeout = [db busyTimeoutMS];
  XCTAssertEqual(retrievedBusyTimeout, busyTimeout,
                 @"Retrieved busy time out was not equal to what we set it"
                 @" to!");

  BOOL xactOpSucceeded;

  xactOpSucceeded = [db beginDeferredTransaction];
  XCTAssertTrue(xactOpSucceeded, @"beginDeferredTransaction failed!");

  err = [db executeSQL:@"insert into foo (bar) values ('blah!');"];
  XCTAssertEqual(err, SQLITE_OK, @"Failed to execute SQL");
  changeCount = [db lastChangeCount];
  XCTAssertEqual(changeCount, 1,
                 @"Change count didn't stay the same"
                 @"when inserting during transaction");

  xactOpSucceeded = [db rollback];
  XCTAssertTrue(xactOpSucceeded, @"could not rollback!");

  changeCount = [db lastChangeCount];
  XCTAssertEqual(changeCount, 1, @"Change count isn't 1 after rollback :-(");

  xactOpSucceeded = [db beginDeferredTransaction];
  XCTAssertTrue(xactOpSucceeded, @"beginDeferredTransaction failed!");

  for (unsigned int i = 0; i < 100; i++) {
    err = [db executeSQL:@"insert into foo (bar) values ('blah!');"];
    XCTAssertEqual(err, SQLITE_OK, @"Failed to execute SQL");
  }

  xactOpSucceeded = [db commit];
  XCTAssertTrue(xactOpSucceeded, @"could not commit!");

  changeCount = [db totalChangeCount];
  XCTAssertEqual(changeCount, 102, @"Change count isn't 102 after commit :-(");
}

- (void)testSQLiteWithoutCFAdditions {
  int err;
  GTMSQLiteDatabase *dbNoCFAdditions =
    [[[GTMSQLiteDatabase alloc] initInMemoryWithCFAdditions:NO
                                                       utf8:YES
                                                  errorCode:&err]
      autorelease];

  XCTAssertNotNil(dbNoCFAdditions, @"Failed to create DB");
  XCTAssertEqual(err, SQLITE_OK, @"Failed to create DB");

  err = [dbNoCFAdditions executeSQL:nil];
  XCTAssertEqual(err, SQLITE_MISUSE, @"Nil SQL did not return error");

  err = [dbNoCFAdditions executeSQL:@"SELECT UPPER('Fred');"];
  XCTAssertEqual(err, SQLITE_OK, @"Nil SQL did not return error");
}

- (void)testSynchronousAPI {
  int err;
  GTMSQLiteDatabase *db =
    [[[GTMSQLiteDatabase alloc] initInMemoryWithCFAdditions:YES
                                                       utf8:YES
                                                  errorCode:&err]
      autorelease];
  [db synchronousMode:YES];
  [db synchronousMode:NO];
}

- (void)testEmptyStringsCollation {
  int err;
  GTMSQLiteDatabase *db8 =
    [[[GTMSQLiteDatabase alloc] initInMemoryWithCFAdditions:YES
                                                       utf8:YES
                                                  errorCode:&err]
      autorelease];

  XCTAssertNotNil(db8, @"Failed to create DB");
  XCTAssertEqual(err, SQLITE_OK, @"Failed to create DB");

  GTMSQLiteDatabase *db16 =
    [[[GTMSQLiteDatabase alloc] initInMemoryWithCFAdditions:YES
                                                       utf8:NO
                                                  errorCode:&err]
      autorelease];

  XCTAssertNotNil(db16, @"Failed to create DB");
  XCTAssertEqual(err, SQLITE_OK, @"Failed to create DB");

  NSArray *databases = [NSArray arrayWithObjects:db8, db16, nil];
  GTMSQLiteDatabase *db;
  for (db in databases) {
    err = [db executeSQL:
                @"CREATE TABLE foo (bar TEXT COLLATE NOCASE_NONLITERAL,"
                @"                  barrev text collate reverse);"];

    XCTAssertEqual(err, SQLITE_OK,
                   @"Failed to create table for collation test");
    // Create blank rows to test matching inside collation functions
    err = [db executeSQL:@"insert into foo (bar, barrev) values ('','');"];
    XCTAssertEqual(err, SQLITE_OK, @"Failed to execute SQL");

    // Insert one row we want to match
    err = [db executeSQL:
                @"INSERT INTO foo (bar, barrev) VALUES "
                @"('teststring','teststring');"];
    XCTAssertEqual(err, SQLITE_OK, @"Failed to execute SQL");

    NSString *matchString = @"foobar";
    GTMSQLiteStatement *statement =
        [GTMSQLiteStatement statementWithSQL:[NSString stringWithFormat:
        @"SELECT bar FROM foo WHERE bar == '%@';", matchString]
                               inDatabase:db
                                errorCode:&err];
    XCTAssertNotNil(statement, @"Failed to create statement");
    XCTAssertEqual(err, SQLITE_OK, @"Failed to create statement");
    [statement stepRow];
    [statement finalizeStatement];

    statement =
        [GTMSQLiteStatement statementWithSQL:[NSString stringWithFormat:
        @"SELECT bar FROM foo WHERE barrev == '%@' order by barrev;", matchString]
                               inDatabase:db
                                errorCode:&err];
    XCTAssertNotNil(statement, @"Failed to create statement");
    XCTAssertEqual(err, SQLITE_OK, @"Failed to create statement");
    [statement stepRow];

    [statement finalizeStatement];

    statement =
        [GTMSQLiteStatement statementWithSQL:[NSString stringWithFormat:
        @"SELECT bar FROM foo WHERE bar == '';"]
                               inDatabase:db
                                errorCode:&err];
    XCTAssertNotNil(statement, @"Failed to create statement");
    XCTAssertEqual(err, SQLITE_OK, @"Failed to create statement");
    [statement stepRow];
    [statement finalizeStatement];

    statement =
        [GTMSQLiteStatement statementWithSQL:[NSString stringWithFormat:
        @"SELECT bar FROM foo WHERE barrev == '' order by barrev;"]
                               inDatabase:db
                                errorCode:&err];
    XCTAssertNotNil(statement, @"Failed to create statement");
    XCTAssertEqual(err, SQLITE_OK, @"Failed to create statement");
    [statement stepRow];
    [statement finalizeStatement];
  }
}

- (void)testUTF16Database {
  int err;
  GTMSQLiteDatabase *db =
    [[[GTMSQLiteDatabase alloc] initInMemoryWithCFAdditions:YES
                                                       utf8:NO
                                                  errorCode:&err]
      autorelease];

  XCTAssertNotNil(db, @"Failed to create DB");
  XCTAssertEqual(err, SQLITE_OK, @"Failed to create DB");

  err = [db executeSQL:@"CREATE TABLE foo (bar TEXT COLLATE NOCASE_NONLITERAL);"];
  XCTAssertEqual(err, SQLITE_OK, @"Failed to create table for collation test");

  // Insert one row we want to match
  err = [db executeSQL:[NSString stringWithFormat:
                                   @"INSERT INTO foo (bar) VALUES ('%@');",
    [NSString stringWithCString:"Frédéric" encoding:NSUTF8StringEncoding]]];
  XCTAssertEqual(err, SQLITE_OK, @"Failed to execute SQL");

  // Create blank rows to test matching inside collation functions
  err = [db executeSQL:@"insert into foo (bar) values ('');"];
  XCTAssertEqual(err, SQLITE_OK, @"Failed to execute SQL");

  err = [db executeSQL:@"insert into foo (bar) values ('');"];
  XCTAssertEqual(err, SQLITE_OK, @"Failed to execute SQL");

  // Loop over a few things all of which should match
  NSArray *testArray = [NSArray arrayWithObjects:
                         [NSString stringWithCString:"Frédéric"
                                            encoding:NSUTF8StringEncoding],
                         [NSString stringWithCString:"frédéric"
                                            encoding:NSUTF8StringEncoding],
                         [NSString stringWithCString:"FRÉDÉRIC"
                                            encoding:NSUTF8StringEncoding],
                         nil];
  NSString *testString = nil;
  for (testString in testArray) {
    GTMSQLiteStatement *statement =
      [GTMSQLiteStatement statementWithSQL:[NSString stringWithFormat:
        @"SELECT bar FROM foo WHERE bar == '%@';", testString]
                               inDatabase:db
                                errorCode:&err];
    XCTAssertNotNil(statement, @"Failed to create statement");
    XCTAssertEqual(err, SQLITE_OK, @"Failed to create statement");
    int count = 0;
    while ([statement stepRow] == SQLITE_ROW) {
      count++;
    }
    XCTAssertEqual(count, 1, @"Wrong number of collated rows for \"%@\"",
                   testString);
    [statement finalizeStatement];
  }

  GTMSQLiteStatement *statement =
    [GTMSQLiteStatement statementWithSQL:@"select * from foo;"
                        inDatabase:db
                        errorCode:&err];

  XCTAssertNotNil(statement, @"Failed to create statement");
  XCTAssertEqual(err, SQLITE_OK, @"Failed to create statement");

  while ([statement stepRow] == SQLITE_ROW) ;
  [statement finalizeStatement];

}

- (void)testUpperLower {

  // Test our custom UPPER/LOWER implementation, need a database and statement
  // to do it.
  int err;
  GTMSQLiteDatabase *db =
    [[[GTMSQLiteDatabase alloc] initInMemoryWithCFAdditions:YES
                                                       utf8:YES
                                                  errorCode:&err]
      autorelease];
  XCTAssertNotNil(db, @"Failed to create DB");
  XCTAssertEqual(err, SQLITE_OK, @"Failed to create DB");
  GTMSQLiteStatement *statement = nil;

  // Test simple ASCII
  statement = [GTMSQLiteStatement statementWithSQL:@"SELECT LOWER('Fred');"
                                       inDatabase:db
                                        errorCode:&err];
  XCTAssertNotNil(statement, @"Failed to create statement");
  XCTAssertEqual(err, SQLITE_OK, @"Failed to create statement");
  err = [statement stepRow];
  XCTAssertEqual(err, SQLITE_ROW, @"Failed to step row");
  XCTAssertEqualObjects([statement resultStringAtPosition:0],
                        @"fred",
                        @"LOWER failed for ASCII string");
  [statement finalizeStatement];

  statement = [GTMSQLiteStatement statementWithSQL:@"SELECT UPPER('Fred');"
                                       inDatabase:db
                                        errorCode:&err];
  XCTAssertNotNil(statement, @"Failed to create statement");
  XCTAssertEqual(err, SQLITE_OK, @"Failed to create statement");
  err = [statement stepRow];
  XCTAssertEqual(err, SQLITE_ROW, @"Failed to step row");
  XCTAssertEqualObjects([statement resultStringAtPosition:0],
                        @"FRED",
                        @"UPPER failed for ASCII string");

  [statement finalizeStatement];
  // Test UTF-8, have to do some dancing to make the compiler take
  // UTF8 literals
  NSString *utfNormalString =
    [NSString stringWithCString:"Frédéric"
                       encoding:NSUTF8StringEncoding];
  NSString *utfLowerString =
    [NSString stringWithCString:"frédéric"
                       encoding:NSUTF8StringEncoding];
  NSString *utfUpperString =
    [NSString stringWithCString:"FRÉDÉRIC" encoding:NSUTF8StringEncoding];

  statement =
    [GTMSQLiteStatement statementWithSQL:
              [NSString stringWithFormat:@"SELECT LOWER('%@');", utfNormalString]
                              inDatabase:db
                               errorCode:&err];
  XCTAssertNotNil(statement, @"Failed to create statement");
  XCTAssertEqual(err, SQLITE_OK, @"Failed to create statement");
  err = [statement stepRow];
  XCTAssertEqual(err, SQLITE_ROW, @"Failed to step row");
  XCTAssertEqualObjects([statement resultStringAtPosition:0],
                        utfLowerString,
                        @"UPPER failed for UTF8 string");
  [statement finalizeStatement];

  statement =
    [GTMSQLiteStatement statementWithSQL:
              [NSString stringWithFormat:@"SELECT UPPER('%@');", utfNormalString]
                              inDatabase:db
                               errorCode:&err];
  XCTAssertNotNil(statement, @"Failed to create statement");
  XCTAssertEqual(err, SQLITE_OK, @"Failed to create statement");
  err = [statement stepRow];
  XCTAssertEqual(err, SQLITE_ROW, @"Failed to step row");
  XCTAssertEqualObjects([statement resultStringAtPosition:0],
                        utfUpperString,
                        @"UPPER failed for UTF8 string");
  err = [statement stepRow];
  XCTAssertEqual(err, SQLITE_DONE, @"Should be done");
  [statement finalizeStatement];
}

- (void)testUpperLower16 {

  // Test our custom UPPER/LOWER implementation, need a database and
  // statement to do it.
  int err;
  GTMSQLiteDatabase *db =
    [[[GTMSQLiteDatabase alloc] initInMemoryWithCFAdditions:YES
                                                       utf8:NO
                                                  errorCode:&err]
      autorelease];
  XCTAssertNotNil(db, @"Failed to create DB");
  XCTAssertEqual(err, SQLITE_OK, @"Failed to create DB");
  GTMSQLiteStatement *statement = nil;

  // Test simple ASCII
  statement = [GTMSQLiteStatement statementWithSQL:@"SELECT LOWER('Fred');"
                                       inDatabase:db
                                        errorCode:&err];
  XCTAssertNotNil(statement, @"Failed to create statement");
  XCTAssertEqual(err, SQLITE_OK, @"Failed to create statement");
  err = [statement stepRow];
  XCTAssertEqual(err, SQLITE_ROW, @"Failed to step row");
  XCTAssertEqualObjects([statement resultStringAtPosition:0],
                        @"fred",
                        @"LOWER failed for ASCII string");
  [statement finalizeStatement];

  statement = [GTMSQLiteStatement statementWithSQL:@"SELECT UPPER('Fred');"
                                       inDatabase:db
                                        errorCode:&err];
  XCTAssertNotNil(statement, @"Failed to create statement");
  XCTAssertEqual(err, SQLITE_OK, @"Failed to create statement");
  err = [statement stepRow];
  XCTAssertEqual(err, SQLITE_ROW, @"Failed to step row");
  XCTAssertEqualObjects([statement resultStringAtPosition:0],
                        @"FRED",
                        @"UPPER failed for ASCII string");
  [statement finalizeStatement];
}

typedef struct {
  BOOL upperCase;
  int  textRep;
} UpperLowerUserArgs;

static void TestUpperLower16Impl(sqlite3_context *context,
                                 int argc, sqlite3_value **argv);

- (void)testUTF16DatabasesAreReallyUTF16 {
  int err;
  GTMSQLiteDatabase *db =
    [[[GTMSQLiteDatabase alloc] initInMemoryWithCFAdditions:YES
                                                       utf8:NO
                                                  errorCode:&err]
      autorelease];

  const struct {
    const char           *sqlName;
    UpperLowerUserArgs   userArgs;
    void                 *function;
  } customUpperLower[] = {
    { "upper", { YES, SQLITE_UTF16 }, &TestUpperLower16Impl },
    { "upper", { YES, SQLITE_UTF16BE }, &TestUpperLower16Impl },
    { "upper", { YES, SQLITE_UTF16LE }, &TestUpperLower16Impl }
  };


  sqlite3 *sqldb = [db sqlite3DB];
  int rc;
  for (size_t i = 0;
       i < (sizeof(customUpperLower) / sizeof(customUpperLower[0]));
       i++) {
    rc = sqlite3_create_function(sqldb,
                                 customUpperLower[i].sqlName,
                                 1,
                                 customUpperLower[i].userArgs.textRep,
                                 (void *)&customUpperLower[i].userArgs,
                                 customUpperLower[i].function,
                                 NULL,
                                 NULL);
    XCTAssertEqual(rc, SQLITE_OK,
                   @"Failed to register upper function"
                   @"with SQLite db");
  }

  customUpperFunctionCalled = NO;
  GTMSQLiteStatement *statement = [GTMSQLiteStatement statementWithSQL:@"SELECT UPPER('Fred');"
                                                            inDatabase:db
                                                             errorCode:&err];
  XCTAssertNotNil(statement, @"Failed to create statement");
  XCTAssertEqual(err, SQLITE_OK, @"Failed to create statement");
  err = [statement stepRow];
  XCTAssertEqual(err, SQLITE_ROW, @"Failed to step row");
  XCTAssertTrue(customUpperFunctionCalled,
                @"Custom upper function was not called!");
  [statement finalizeStatement];
}

- (void)testLikeComparisonOptions {
  int err;

  GTMSQLiteDatabase *db8 =
    [[[GTMSQLiteDatabase alloc] initInMemoryWithCFAdditions:YES
                                                       utf8:YES
                                                  errorCode:&err] autorelease];

  GTMSQLiteDatabase *db16 =
    [[[GTMSQLiteDatabase alloc] initInMemoryWithCFAdditions:YES
                                                       utf8:NO
                                                  errorCode:&err] autorelease];

  NSArray *databases = [NSArray arrayWithObjects:db8, db16, nil];
  GTMSQLiteDatabase *db;
  for (db in databases) {
    CFOptionFlags c = 0, oldFlags;

    oldFlags = [db likeComparisonOptions];

    // We'll do a case sensitivity test by making comparison options
    // case insensitive
    [db setLikeComparisonOptions:c];

    XCTAssertTrue([db likeComparisonOptions] == 0,
                  @"LIKE Comparison options setter/getter does not work!");

    NSString *createString = nil;
    createString = @"CREATE TABLE foo (bar NODIACRITIC_WIDTHINSENSITIVE TEXT);";

    err = [db executeSQL:createString];
    XCTAssertEqual(err, SQLITE_OK,
                   @"Failed to create table for like comparison options test");

    err = [db executeSQL:@"insert into foo values('test like test');"];
    XCTAssertEqual(err, SQLITE_OK,
                   @"Failed to create row for like comparison options test");

    GTMSQLiteStatement *statement =
      [GTMSQLiteStatement statementWithSQL:@"select * from foo where bar like '%LIKE%'"
                                inDatabase:db
                                 errorCode:&err];

    XCTAssertNotNil(statement, @"failed to create statement");
    XCTAssertEqual(err, SQLITE_OK, @"failed to create statement");
    err = [statement stepRow];
    XCTAssertEqual(err, SQLITE_DONE, @"failed to retrieve row!");

    // Now change it back to case insensitive and rerun the same query
    c |= kCFCompareCaseInsensitive;
    [db setLikeComparisonOptions:c];
    err = [statement reset];
    XCTAssertEqual(err, SQLITE_OK, @"failed to reset select statement");

    err = [statement stepRow];
    XCTAssertEqual(err, SQLITE_ROW, @"failed to retrieve row!");

    c |= (kCFCompareDiacriticInsensitive | kCFCompareWidthInsensitive);
    [db setLikeComparisonOptions:c];
    // Make a new statement
    [statement finalizeStatement];
    statement =
      [GTMSQLiteStatement statementWithSQL:@"select * from foo where bar like '%LIKE%'"
                                inDatabase:db
                                 errorCode:&err];

    XCTAssertNotNil(statement, @"failed to create statement");
    XCTAssertEqual(err, SQLITE_OK, @"failed to create statement");

    err = [statement stepRow];
    XCTAssertEqual(err, SQLITE_ROW, @"failed to retrieve row!");

    // Now reset comparison options
    [db setLikeComparisonOptions:oldFlags];

    [statement finalizeStatement];
  }
}

- (void)testGlobComparisonOptions {
  int err;
  GTMSQLiteDatabase *db = [[[GTMSQLiteDatabase alloc]
                             initInMemoryWithCFAdditions:YES
                                                    utf8:YES
                                               errorCode:&err] autorelease];

  CFOptionFlags c = 0, oldFlags;

  oldFlags = [db globComparisonOptions];

  [db setGlobComparisonOptions:c];

  XCTAssertTrue([db globComparisonOptions] == 0,
                @"GLOB Comparison options setter/getter does not work!");

  err = [db executeSQL:@"CREATE TABLE foo (bar TEXT);"];
  XCTAssertEqual(err, SQLITE_OK,
                 @"Failed to create table for glob comparison options test");

  err = [db executeSQL:@"insert into foo values('test like test');"];
  XCTAssertEqual(err, SQLITE_OK,
                 @"Failed to create row for glob comparison options test");

  GTMSQLiteStatement *statement =
    [GTMSQLiteStatement statementWithSQL:@"select * from foo where bar GLOB 'TEST*'"
                              inDatabase:db
                               errorCode:&err];

  XCTAssertNotNil(statement, @"failed to create statement");
  XCTAssertEqual(err, SQLITE_OK, @"failed to create statement");
  err = [statement stepRow];
  XCTAssertEqual(err, SQLITE_DONE, @"failed to retrieve row!");

  // Now change it back to case insensitive and rerun the same query
  c |= kCFCompareCaseInsensitive;
  [db setGlobComparisonOptions:c];
  err = [statement reset];
  XCTAssertEqual(err, SQLITE_OK, @"failed to reset select statement");

  err = [statement stepRow];
  XCTAssertEqual(err, SQLITE_ROW, @"failed to retrieve row!");

  [statement finalizeStatement];

  // Now reset comparison options
  [db setGlobComparisonOptions:oldFlags];
}

- (void)testCFStringReverseCollation {
  int err;
  GTMSQLiteDatabase *db =
    [[[GTMSQLiteDatabase alloc] initInMemoryWithCFAdditions:YES
                                                       utf8:YES
                                                  errorCode:&err] autorelease];

  err = [db executeSQL:@"CREATE table foo_reverse (bar TEXT COLLATE REVERSE);"];
  XCTAssertEqual(err, SQLITE_OK,
                 @"Failed to create table for reverse collation test");

  err = [db executeSQL:@"insert into foo_reverse values('a2');"];
  XCTAssertEqual(err, SQLITE_OK, @"Failed to execute SQL");

  err = [db executeSQL:@"insert into foo_reverse values('b1');"];
  XCTAssertEqual(err, SQLITE_OK, @"Failed to execute SQL");

  GTMSQLiteStatement *statement =
    [GTMSQLiteStatement statementWithSQL:@"SELECT bar from foo_reverse order by bar"
                              inDatabase:db
                               errorCode:&err];

  XCTAssertNotNil(statement, @"failed to create statement");
  XCTAssertEqual(err, SQLITE_OK, @"Failed to create statement");
  err = [statement stepRow];
  XCTAssertEqual(err, SQLITE_ROW, @"failed to advance row");
  NSString *oneRow = [statement resultStringAtPosition:0];

  XCTAssertEqualStrings(oneRow, @"b1", @"b did not come first!");
  err = [statement stepRow];
  XCTAssertEqual(err, SQLITE_ROW, @"failed to advance row!");

  XCTAssertEqual(err, [db lastErrorCode],
                 @"lastErrorCode API did not match what last API returned!");

  oneRow = [statement resultStringAtPosition:0];
  XCTAssertEqualStrings(oneRow, @"a2", @"a did not come second!");

  [statement finalizeStatement];
}

- (void)testCFStringNumericCollation {
  int err;
  GTMSQLiteDatabase *db = [[[GTMSQLiteDatabase alloc]
                             initInMemoryWithCFAdditions:YES
                                                    utf8:YES
                                               errorCode:&err] autorelease];

  err = [db executeSQL:
              @"CREATE table numeric_test_table "
              @"(numeric_sort TEXT COLLATE NUMERIC, lexographic_sort TEXT);"];
  XCTAssertEqual(err, SQLITE_OK,
                 @"Failed to create table for numeric collation test");

  err = [db executeSQL:@"insert into numeric_test_table values('4','17');"];
  XCTAssertEqual(err, SQLITE_OK, @"Failed to execute SQL");

  err = [db executeSQL:@"insert into numeric_test_table values('17','4');"];
  XCTAssertEqual(err, SQLITE_OK, @"Failed to execute SQL");

  GTMSQLiteStatement *statement =
    [GTMSQLiteStatement statementWithSQL:@"SELECT numeric_sort from numeric_test_table order by numeric_sort"
                              inDatabase:db
                               errorCode:&err];

  XCTAssertNotNil(statement, @"failed to create statement");
  XCTAssertEqual(err, SQLITE_OK, @"Failed to create statement");
  err = [statement stepRow];
  XCTAssertEqual(err, SQLITE_ROW, @"failed to advance row");
  NSString *oneRow = [statement resultStringAtPosition:0];

  XCTAssertEqualStrings(oneRow, @"4", @"4 did not come first!");
  err = [statement stepRow];
  XCTAssertEqual(err, SQLITE_ROW, @"failed to advance row!");

  oneRow = [statement resultStringAtPosition:0];
  XCTAssertEqualStrings(oneRow, @"17", @"17 did not come second!");

  [statement finalizeStatement];

  statement =
    [GTMSQLiteStatement statementWithSQL:
                          @"SELECT lexographic_sort from numeric_test_table "
                          @"order by lexographic_sort"
                              inDatabase:db
                               errorCode:&err];

  XCTAssertNotNil(statement, @"failed to create statement for lexographic sort");
  XCTAssertEqual(err, SQLITE_OK, @"Failed to create statement");
  err = [statement stepRow];
  XCTAssertEqual(err, SQLITE_ROW, @"failed to advance row");
  oneRow = [statement resultStringAtPosition:0];

  XCTAssertEqualStrings(oneRow, @"17", @"17 did not come first!");
  err = [statement stepRow];
  XCTAssertEqual(err, SQLITE_ROW, @"failed to advance row!");

  oneRow = [statement resultStringAtPosition:0];
  XCTAssertEqualStrings(oneRow, @"4", @"4 did not come second!");

  [statement finalizeStatement];
}

- (void)testCFStringCollation {

  // Test just one case of the collations, they all exercise largely the
  // same code
  int err;
  GTMSQLiteDatabase *db =
    [[GTMSQLiteDatabase alloc] initInMemoryWithCFAdditions:YES
                                                      utf8:YES
                                                 errorCode:&err];
  XCTAssertNotNil(db, @"Failed to create DB");
  XCTAssertEqual(err, SQLITE_OK, @"Failed to create DB");

  err = [db executeSQL:
              @"CREATE TABLE foo (bar TEXT COLLATE NOCASE_NONLITERAL_LOCALIZED);"];
  XCTAssertEqual(err, SQLITE_OK, @"Failed to create table for collation test");

  // Insert one row we want to match
  err = [db executeSQL:[NSString stringWithFormat:
                                   @"INSERT INTO foo (bar) VALUES ('%@');",
    [NSString stringWithCString:"Frédéric" encoding:NSUTF8StringEncoding]]];
  XCTAssertEqual(err, SQLITE_OK, @"Failed to execute SQL");

  // Loop over a few things all of which should match
  NSArray *testArray = [NSArray arrayWithObjects:
                         [NSString stringWithCString:"Frédéric"
                                            encoding:NSUTF8StringEncoding],
                         [NSString stringWithCString:"frédéric"
                                            encoding:NSUTF8StringEncoding],
                         [NSString stringWithCString:"FRÉDÉRIC"
                                            encoding:NSUTF8StringEncoding],
                         nil];

  NSString *testString = nil;
  for (testString in testArray) {
    GTMSQLiteStatement *statement =
      [GTMSQLiteStatement statementWithSQL:[NSString stringWithFormat:
        @"SELECT bar FROM foo WHERE bar == '%@';", testString]
                               inDatabase:db
                                errorCode:&err];
    XCTAssertNotNil(statement, @"Failed to create statement");
    XCTAssertEqual(err, SQLITE_OK, @"Failed to create statement");
    int count = 0;
    while ([statement stepRow] == SQLITE_ROW) {
      count++;
    }
    XCTAssertEqual(count, 1, @"Wrong number of collated rows for \"%@\"",
                   testString);
    [statement finalizeStatement];
  }

  // Force a release to test the statement cleanup
  [db release];

}

- (void)testDiacriticAndWidthInsensitiveCollations {
  int err;
  GTMSQLiteDatabase *db =
    [[[GTMSQLiteDatabase alloc] initInMemoryWithCFAdditions:YES
                                                       utf8:YES
                                                  errorCode:&err] autorelease];
  XCTAssertNotNil(db, @"Failed to create DB");
  XCTAssertEqual(err, SQLITE_OK, @"Failed to create DB");

  NSString *tableSQL =
    @"CREATE TABLE FOOBAR (collated TEXT COLLATE NODIACRITIC_WIDTHINSENSITIVE, "
    @"                     noncollated TEXT);";

  err = [db executeSQL:tableSQL];
  XCTAssertEqual(err, SQLITE_OK, @"error creating table");

  NSString *testStringValue = [NSString stringWithCString:"Frédéric"
                                                 encoding:NSUTF8StringEncoding];
  // Insert one row we want to match
  err = [db executeSQL:[NSString stringWithFormat:
                                   @"INSERT INTO FOOBAR (collated, noncollated) "
                                   @"VALUES ('%@','%@');",
                                 testStringValue, testStringValue]];

  GTMSQLiteStatement *statement =
    [GTMSQLiteStatement statementWithSQL:
              [NSString stringWithFormat:@"SELECT noncollated FROM foobar"
                                         @" WHERE noncollated == 'Frederic';"]
                              inDatabase:db
                               errorCode:&err];
  XCTAssertNotNil(statement, @"Failed to create statement");
  XCTAssertEqual(err, SQLITE_OK, @"Failed to create statement");
  // Make sure the comparison query didn't return a row because
  // we're doing a comparison on the row without the collation
  XCTAssertEqual([statement stepRow], SQLITE_DONE,
                 @"Comparison with diacritics did not succeed");

  [statement finalizeStatement];

  statement =
    [GTMSQLiteStatement statementWithSQL:
              [NSString stringWithFormat:@"SELECT collated FROM foobar"
                                         @" WHERE collated == 'Frederic';"]
                              inDatabase:db
                               errorCode:&err];
  XCTAssertNotNil(statement, @"Failed to create statement");
  XCTAssertEqual(err, SQLITE_OK, @"Failed to create statement");
  XCTAssertEqual([statement stepRow], SQLITE_ROW,
                 @"Comparison ignoring diacritics did not succeed");
  [statement finalizeStatement];
}

- (void)testCFStringLikeGlob {

  // Test cases drawn from SQLite test case source
  int err;
  GTMSQLiteDatabase *db8 =
    [[[GTMSQLiteDatabase alloc] initInMemoryWithCFAdditions:YES
                                                       utf8:YES
                                                  errorCode:&err]
      autorelease];

  XCTAssertNotNil(db8, @"Failed to create database");
  XCTAssertEqual(err, SQLITE_OK, @"Failed to create database");

  GTMSQLiteDatabase *db16 =
    [[[GTMSQLiteDatabase alloc] initInMemoryWithCFAdditions:YES
                                                       utf8:NO
                                                  errorCode:&err]
      autorelease];

  XCTAssertNotNil(db16, @"Failed to create database");
  XCTAssertEqual(err, SQLITE_OK, @"Failed to create database");

  NSArray *databases = [NSArray arrayWithObjects:db8, db16, nil];
  GTMSQLiteDatabase *db;
  for (db in databases) {
    err = [db executeSQL:@"CREATE TABLE t1 (x TEXT);"];
    XCTAssertEqual(err, SQLITE_OK,
                   @"Failed to create table for LIKE/GLOB test");

    // Insert data set
    err = [db executeSQL:@"INSERT INTO t1 VALUES ('a');"];
    XCTAssertEqual(err, SQLITE_OK, @"Failed to execute sql");
    err = [db executeSQL:@"INSERT INTO t1 VALUES ('ab');"];
    XCTAssertEqual(err, SQLITE_OK, @"Failed to execute sql");
    err = [db executeSQL:@"INSERT INTO t1 VALUES ('abc');"];
    XCTAssertEqual(err, SQLITE_OK, @"Failed to execute sql");
    err = [db executeSQL:@"INSERT INTO t1 VALUES ('abcd');"];
    XCTAssertEqual(err, SQLITE_OK, @"Failed to execute sql");
    err = [db executeSQL:@"INSERT INTO t1 VALUES ('acd');"];
    XCTAssertEqual(err, SQLITE_OK, @"Failed to execute sql");
    err = [db executeSQL:@"INSERT INTO t1 VALUES ('abd');"];
    XCTAssertEqual(err, SQLITE_OK, @"Failed to execute sql");
    err = [db executeSQL:@"INSERT INTO t1 VALUES ('bc');"];
    XCTAssertEqual(err, SQLITE_OK, @"Failed to execute sql");
    err = [db executeSQL:@"INSERT INTO t1 VALUES ('bcd');"];
    XCTAssertEqual(err, SQLITE_OK, @"Failed to execute sql");
    err = [db executeSQL:@"INSERT INTO t1 VALUES ('xyz');"];
    XCTAssertEqual(err, SQLITE_OK, @"Failed to execute sql");
    err = [db executeSQL:@"INSERT INTO t1 VALUES ('ABC');"];
    XCTAssertEqual(err, SQLITE_OK, @"Failed to execute sql");
    err = [db executeSQL:@"INSERT INTO t1 VALUES ('CDE');"];
    XCTAssertEqual(err, SQLITE_OK, @"Failed to execute sql");
    err = [db executeSQL:@"INSERT INTO t1 VALUES ('ABC abc xyz');"];
    XCTAssertEqual(err, SQLITE_OK, @"Failed to execute sql");

    // Section 1, case tests
    XCTAssertEqualObjects(
      LikeGlobTestHelper(db,
                         @"SELECT x FROM t1 WHERE x LIKE 'abc' ORDER BY 1;"),
                         ([NSArray arrayWithObjects:@"ABC", @"abc", nil]),
                         @"Fail on LIKE test 1.1");
    XCTAssertEqualObjects(
      LikeGlobTestHelper(db,
                         @"SELECT x FROM t1 WHERE x GLOB 'abc' ORDER BY 1;"),
                         ([NSArray arrayWithObjects:@"abc", nil]),
                         @"Fail on LIKE test 1.2");
    XCTAssertEqualObjects(
      LikeGlobTestHelper(db,
                         @"SELECT x FROM t1 WHERE x LIKE 'ABC' ORDER BY 1;"),
                         ([NSArray arrayWithObjects:@"ABC", @"abc", nil]),
                         @"Fail on LIKE test 1.3");
    XCTAssertEqualObjects(
      LikeGlobTestHelper(db,
                         @"SELECT x FROM t1 WHERE x LIKE 'abc%' ORDER BY 1;"),
                         ([NSArray arrayWithObjects:@"ABC", @"ABC abc xyz", @"abc", @"abcd", nil]),
                         @"Fail on LIKE test 3.1");
    [db setLikeComparisonOptions:(kCFCompareNonliteral)];
    err = [db executeSQL:@"CREATE INDEX i1 ON t1(x);"];
    XCTAssertEqual(err, SQLITE_OK, @"Failed to execute sql");
    XCTAssertEqualObjects(
      LikeGlobTestHelper(db,
                         @"SELECT x FROM t1 WHERE x LIKE 'abc%' ORDER BY 1;"),
                         ([NSArray arrayWithObjects:@"abc", @"abcd", nil]),
                         @"Fail on LIKE test 3.3");
    XCTAssertEqualObjects(
      LikeGlobTestHelper(db,
                         @"SELECT x FROM t1 WHERE x LIKE 'a_c' ORDER BY 1;"),
                         ([NSArray arrayWithObjects:@"abc", nil]),
                         @"Fail on LIKE test 3.5");
    XCTAssertEqualObjects(
      LikeGlobTestHelper(db,
                         @"SELECT x FROM t1 WHERE x LIKE 'ab%d' ORDER BY 1;"),
                         ([NSArray arrayWithObjects:@"abcd", @"abd", nil]),
                         @"Fail on LIKE test 3.7");
    XCTAssertEqualObjects(
      LikeGlobTestHelper(db,
                         @"SELECT x FROM t1 WHERE x LIKE 'a_c%' ORDER BY 1;"),
                         ([NSArray arrayWithObjects:@"abc", @"abcd", nil]),
                         @"Fail on LIKE test 3.9");
    XCTAssertEqualObjects(
      LikeGlobTestHelper(db,
                         @"SELECT x FROM t1 WHERE x LIKE '%bcd' ORDER BY 1;"),
                         ([NSArray arrayWithObjects:@"abcd", @"bcd", nil]),
                         @"Fail on LIKE test 3.11");
    [db setLikeComparisonOptions:(kCFCompareNonliteral | kCFCompareCaseInsensitive)];
    XCTAssertEqualObjects(
      LikeGlobTestHelper(db,
                         @"SELECT x FROM t1 WHERE x LIKE 'abc%' ORDER BY 1;"),
                         ([NSArray arrayWithObjects:@"ABC", @"ABC abc xyz", @"abc", @"abcd", nil]),
                         @"Fail on LIKE test 3.13");
    [db setLikeComparisonOptions:(kCFCompareNonliteral)];
    err = [db executeSQL:@"DROP INDEX i1;"];
    XCTAssertEqual(err, SQLITE_OK, @"Failed to execute sql");
    XCTAssertEqualObjects(
      LikeGlobTestHelper(db,
                         @"SELECT x FROM t1 WHERE x LIKE 'abc%' ORDER BY 1;"),
                         ([NSArray arrayWithObjects:@"abc", @"abcd", nil]),
                         @"Fail on LIKE test 3.15");
    XCTAssertEqualObjects(
      LikeGlobTestHelper(db,
                         @"SELECT x FROM t1 WHERE x GLOB 'abc*' ORDER BY 1;"),
                         ([NSArray arrayWithObjects:@"abc", @"abcd", nil]),
                         @"Fail on LIKE test 3.17");
    err = [db executeSQL:@"CREATE INDEX i1 ON t1(x);"];
    XCTAssertEqual(err, SQLITE_OK, @"Failed to execute sql");
    XCTAssertEqualObjects(
      LikeGlobTestHelper(db,
                         @"SELECT x FROM t1 WHERE x GLOB 'abc*' ORDER BY 1;"),
                         ([NSArray arrayWithObjects:@"abc", @"abcd", nil]),
                         @"Fail on LIKE test 3.19");
    [db setLikeComparisonOptions:(kCFCompareNonliteral)];
    XCTAssertEqualObjects(
      LikeGlobTestHelper(db,
                         @"SELECT x FROM t1 WHERE x GLOB 'abc*' ORDER BY 1;"),
                         ([NSArray arrayWithObjects:@"abc", @"abcd", nil]),
                         @"Fail on LIKE test 3.21");
    [db setLikeComparisonOptions:(kCFCompareNonliteral |
                                  kCFCompareCaseInsensitive)];
    XCTAssertEqualObjects(
      LikeGlobTestHelper(db,
                         @"SELECT x FROM t1 WHERE x GLOB 'a[bc]d' ORDER BY 1;"),
                         ([NSArray arrayWithObjects:@"abd", @"acd", nil]),
                         @"Fail on LIKE test 3.23");

    XCTAssertEqualObjects(
      LikeGlobTestHelper(db,
                         @"SELECT x from t1 where x GLOB 'a[^xyz]d' ORDER BY 1;"),
                         ([NSArray arrayWithObjects:@"abd", @"acd", nil]),
                         @"Fail on glob inverted character set test 3.24");

    XCTAssertEqualObjects(
      LikeGlobTestHelper(db,
                         @"SELECT x from t1 where x GLOB 'a[^' ORDER BY 1;"),
      ([NSArray array]),
      @"Fail on glob inverted character set test 3.25");

    XCTAssertEqualObjects(
      LikeGlobTestHelper(db,
                         @"SELECT x from t1 where x GLOB 'a['"),
      ([NSArray array]),
      @"Unclosed glob character set did not return empty result set 3.26");

    XCTAssertEqualObjects(
      LikeGlobTestHelper(db,
                         @"SELECT x from t1 where x GLOB 'a[^]'"),
      ([NSArray array]),
      @"Unclosed glob inverted character set did not return empty "
      @"result set 3.27");

    XCTAssertEqualObjects(
      LikeGlobTestHelper(db,
                         @"SELECT x from t1 where x GLOB 'a[^]c]d'"),
      ([NSArray arrayWithObjects:@"abd", nil]),
      @"Glob character set with inverted set not matching ] did not "
      @"return right rows 3.28");

    XCTAssertEqualObjects(
      LikeGlobTestHelper(db,
                         @"SELECT x from t1 where x GLOB 'a[bcdefg'"),
      ([NSArray array]),
      @"Unclosed glob character set did not return empty result set 3.29");

    // Section 4
    [db setLikeComparisonOptions:(kCFCompareNonliteral)];
    XCTAssertEqualObjects(
      LikeGlobTestHelper(db,
                         @"SELECT x FROM t1 WHERE x LIKE 'abc%' ORDER BY 1;"),
      ([NSArray arrayWithObjects:@"abc", @"abcd", nil]),
      @"Fail on LIKE test 4.1");
    XCTAssertEqualObjects(
      LikeGlobTestHelper(db,
                         @"SELECT x FROM t1 WHERE +x LIKE 'abc%' ORDER BY 1;"),
      ([NSArray arrayWithObjects:@"abc", @"abcd", nil]),
      @"Fail on LIKE test 4.2");
    XCTAssertEqualObjects(
      LikeGlobTestHelper(db,
                         @"SELECT x FROM t1 WHERE x LIKE ('ab' || 'c%') ORDER BY 1;"),
      ([NSArray arrayWithObjects:@"abc", @"abcd", nil]),
      @"Fail on LIKE test 4.3");

    XCTAssertEqualObjects(
      LikeGlobTestHelper(db,
                         @"SELECT x from t1 where x LIKE 'a[xyz]\%' ESCAPE ''"),
      ([NSArray array]),
      @"0-Character escape clause did not return empty set 4.4");

    XCTAssertEqualObjects(
      LikeGlobTestHelper(db,
                         @"SELECT x from t1 where x LIKE "
                         @"'a[xyz]\%' ESCAPE NULL"),
      ([NSArray array]),
      @"Null escape did not return empty set 4.5");

    XCTAssertEqualObjects(
      LikeGlobTestHelper(db,
                         @"SELECT x from t1 where x LIKE 'a[xyz]\\%' "
                         @"ESCAPE '\\'"),
      ([NSArray array]),
      @"Literal percent match using ESCAPE clause did not return empty result "
      @"set 4.6");


    // Section 5
    [db setLikeComparisonOptions:(kCFCompareNonliteral | kCFCompareCaseInsensitive)];
    XCTAssertEqualObjects(
      LikeGlobTestHelper(db,
                         @"SELECT x FROM t1 WHERE x LIKE 'abc%' ORDER BY 1;"),
      ([NSArray arrayWithObjects:@"ABC", @"ABC abc xyz", @"abc", @"abcd", nil]),
      @"Fail on LIKE test 5.1");

    err = [db executeSQL:@"CREATE TABLE t2(x COLLATE NOCASE);"];
    XCTAssertEqual(err, SQLITE_OK, @"Failed to execute sql");

    err = [db executeSQL:@"INSERT INTO t2 SELECT * FROM t1;"];
    XCTAssertEqual(err, SQLITE_OK, @"Failed to execute sql");

    err = [db executeSQL:@"CREATE INDEX i2 ON t2(x COLLATE NOCASE);"];

    XCTAssertEqual(err, SQLITE_OK, @"Failed to execute sql");
    XCTAssertEqualObjects(
      LikeGlobTestHelper(db,
                         @"SELECT x FROM t2 WHERE x LIKE 'abc%' ORDER BY 1;"),
      ([NSArray arrayWithObjects:@"abc", @"ABC", @"ABC abc xyz", @"abcd", nil]),
      @"Fail on LIKE test 5.3");

    [db setLikeComparisonOptions:(kCFCompareNonliteral)];

    XCTAssertEqualObjects(
      LikeGlobTestHelper(db,
                         @"SELECT x FROM t2 WHERE x LIKE 'abc%' ORDER BY 1;"),
      ([NSArray arrayWithObjects:@"abc", @"abcd", nil]),
      @"Fail on LIKE test 5.5");

    [db setLikeComparisonOptions:(kCFCompareNonliteral | kCFCompareCaseInsensitive)];

    XCTAssertEqualObjects(
      LikeGlobTestHelper(db,
                         @"SELECT x FROM t2 WHERE x GLOB 'abc*' ORDER BY 1;"),
      ([NSArray arrayWithObjects:@"abc", @"abcd", nil]),
      @"Fail on LIKE test 5.5");

    // Non standard tests not from the SQLite source
    XCTAssertEqualObjects(
      LikeGlobTestHelper(db,
                         @"SELECT x FROM t1 WHERE x GLOB 'a[b-d]d' ORDER BY 1;"),
      ([NSArray arrayWithObjects:@"abd", @"acd", nil]),
      @"Fail on GLOB with character range");
  }
}

- (void)testDescription {
  int err;
  GTMSQLiteDatabase *db8 =
  [[[GTMSQLiteDatabase alloc] initInMemoryWithCFAdditions:YES
                                                     utf8:YES
                                                errorCode:&err]
   autorelease];

  XCTAssertNotNil(db8, @"Failed to create database");
  XCTAssertEqual(err, SQLITE_OK, @"Failed to create database");
  XCTAssertNotNil([db8 description]);
}

// // From GTMSQLite.m
// CFStringEncoding SqliteTextEncodingToCFStringEncoding(int enc);

// - (void)testEncodingMappingIsCorrect {
//   XCTAssertTrue(SqliteTextEncodingToCFStringEncoding(SQLITE_UTF8) ==
//                 kCFStringEncodingUTF8,
//                 @"helper method didn't return right encoding for "
//                 @"kCFStringEncodingUTF8");

//   XCTAssertTrue(SqliteTextEncodingToCFStringEncoding(SQLITE_UTF16BE)
//                 == kCFStringEncodingUTF16BE,
//                 @"helper method didn't return right encoding for "
//                 @"kCFStringEncodingUTF16BE");

//   XCTAssertTrue(SqliteTextEncodingToCFStringEncoding(SQLITE_UTF16LE)
//                 == kCFStringEncodingUTF16LE,
//                 @"helper method didn't return right encoding for "
//                 @"kCFStringEncodingUTF16LE");

//   XCTAssertTrue(SqliteTextEncodingToCFStringEncoding(9999)
//                 == kCFStringEncodingUTF8, @"helper method didn't "
//                 @"return default encoding for invalid input");
// }

@end


//  Helper function for LIKE/GLOB testing
static NSArray* LikeGlobTestHelper(GTMSQLiteDatabase *db, NSString *sql) {

  int err;
  NSMutableArray *resultArray = [NSMutableArray array];
  GTMSQLiteStatement *statement = [GTMSQLiteStatement statementWithSQL:sql
                                                          inDatabase:db
                                                           errorCode:&err];
  if (!statement || err != SQLITE_OK) return nil;
  while ([statement stepRow] == SQLITE_ROW) {
    id result = [statement resultFoundationObjectAtPosition:0];
    if (result) [resultArray addObject:result];
  }
  if (err != SQLITE_DONE && err != SQLITE_OK) resultArray = nil;
  [statement finalizeStatement];

  return resultArray;
}

// =============================================================================

@implementation GTMSQLiteStatementTest

#pragma mark Parameters/binding tests

- (void)testInitAPI {
  int err;
  GTMSQLiteStatement *statement = [GTMSQLiteStatement statementWithSQL:nil
                                                            inDatabase:nil
                                                             errorCode:&err];
  XCTAssertNil(statement, @"Create statement succeeded with nil SQL string");
  XCTAssertEqual(err, SQLITE_MISUSE, @"Err was not SQLITE_MISUSE on nil "
                 @"SQL string");

  GTMSQLiteDatabase *db =
    [[[GTMSQLiteDatabase alloc] initInMemoryWithCFAdditions:YES
                                                       utf8:YES
                                                  errorCode:&err]
      autorelease];

  statement = [GTMSQLiteStatement statementWithSQL:@"select * from blah"
                                       inDatabase:db
                                        errorCode:&err];

  XCTAssertNil(statement, @"Select statement succeeded with invalid table");
  XCTAssertNotEqual(err, SQLITE_OK,
                    @"Err was not SQLITE_MISUSE on invalid table");

  [statement finalizeStatement];
}

- (void)testParameterCountAPI {
  int err;
  GTMSQLiteDatabase *db =
    [[[GTMSQLiteDatabase alloc] initInMemoryWithCFAdditions:YES
                                                       utf8:YES
                                                  errorCode:&err]
      autorelease];

  NSString *tableCreateSQL =
    @"CREATE TABLE foo (tc TEXT,"
    @"ic integer,"
    @"rc real,"
    @"bc blob);";

  err = [db executeSQL:tableCreateSQL];

  XCTAssertEqual(err, SQLITE_OK,
                 @"Failed to create table for collation test");
  NSString *insert =
    @"insert into foo (tc, ic, rc, bc) values (:tc, :ic, :rc, :bc);";

  GTMSQLiteStatement *statement = [GTMSQLiteStatement statementWithSQL:insert
                                                            inDatabase:db
                                                             errorCode:&err];
  XCTAssertNotNil(statement, @"Failed to create statement");
  XCTAssertEqual(err, SQLITE_OK, @"Failed to create statement");
  XCTAssertEqual([statement parameterCount], 4,
                 @"Bound parameter count was not 4");

  [statement sqlite3Statement];
  [statement finalizeStatement];
}

- (void)testPositionOfNamedParameterAPI {
  int err;

  GTMSQLiteDatabase *dbWithCF =
    [[[GTMSQLiteDatabase alloc] initInMemoryWithCFAdditions:YES
                                                       utf8:YES
                                                  errorCode:&err]
      autorelease];

  GTMSQLiteDatabase *dbWithoutCF =
    [[[GTMSQLiteDatabase alloc] initInMemoryWithCFAdditions:NO
                                                       utf8:YES
                                                  errorCode:&err]
      autorelease];

  NSArray *databases = [NSArray arrayWithObjects:dbWithCF, dbWithoutCF, nil];
  GTMSQLiteDatabase *db;
  for (db in databases) {
    NSString *tableCreateSQL =
      @"CREATE TABLE foo (tc TEXT,"
      @"ic integer,"
      @"rc real,"
      @"bc blob);";
    err = [db executeSQL:tableCreateSQL];

    XCTAssertEqual(err, SQLITE_OK,
                   @"Failed to create table for collation test");
    NSString *insert =
      @"insert into foo (tc, ic, rc, bc) "
      @"values (:tc, :ic, :rc, :bc);";

    GTMSQLiteStatement *statement = [GTMSQLiteStatement statementWithSQL:insert
                                                              inDatabase:db
                                                               errorCode:&err];
    XCTAssertNotNil(statement, @"Failed to create statement");
    XCTAssertEqual(err, SQLITE_OK, @"Failed to create statement");

    NSArray *parameterNames = [NSArray arrayWithObjects:@":tc",
                                                        @":ic",
                                                        @":rc",
                                                        @":bc", nil];

    for (unsigned int i = 1; i <= [parameterNames count]; i++) {
      NSString *paramName = [parameterNames objectAtIndex:i-1];
      // Cast to signed int to avoid type errors from XCTAssertEqual
      XCTAssertEqual((int)i,
                     [statement positionOfParameterNamed:paramName],
                     @"positionOfParameterNamed API did not return correct "
                     @"results");
      XCTAssertEqualStrings(paramName,
                            [statement nameOfParameterAtPosition:i],
                            @"nameOfParameterAtPosition API did not return "
                            @"correct name");
    }
    [statement finalizeStatement];
  }
}

- (void)testBindingBlob {
  int err;
  const int BLOB_COLUMN = 0;
  GTMSQLiteDatabase *dbWithCF =
    [[[GTMSQLiteDatabase alloc] initInMemoryWithCFAdditions:YES
                                                       utf8:YES
                                                  errorCode:&err]
      autorelease];

  GTMSQLiteDatabase *dbWithoutCF =
    [[[GTMSQLiteDatabase alloc] initInMemoryWithCFAdditions:NO
                                                       utf8:YES
                                                  errorCode:&err]
      autorelease];

  NSArray *databases = [NSArray arrayWithObjects:dbWithCF, dbWithoutCF, nil];
  GTMSQLiteDatabase *db;
  for (db in databases) {
    // Test strategy is to create a table with 3 columns
    // Insert some values, and use the result collection APIs
    // to make sure we get the same values back
    err = [db executeSQL:
                @"CREATE TABLE blobby (bc blob);"];

    XCTAssertEqual(err, SQLITE_OK,
                   @"Failed to create table for BLOB binding test");
    NSString *insert = @"insert into blobby (bc) values (:bc);";
    GTMSQLiteStatement *statement = [GTMSQLiteStatement statementWithSQL:insert
                                                              inDatabase:db
                                                               errorCode:&err];
    XCTAssertNotNil(statement, @"Failed to create insert statement");
    XCTAssertEqual(err, SQLITE_OK, @"Failed to create insert statement");

    char bytes[] = "DEADBEEF";
    NSUInteger bytesLen = strlen(bytes);
    NSData *originalBytes = [NSData dataWithBytes:bytes length:bytesLen];

    err = [statement bindBlobAtPosition:1 data:originalBytes];

    XCTAssertEqual(err, SQLITE_OK, @"error binding BLOB at position 1");

    err = [statement stepRow];
    XCTAssertEqual(err, SQLITE_DONE, @"failed to insert BLOB for BLOB test");

    [statement finalizeStatement];

    NSString *selectSQL = @"select * from blobby;";
    statement = [GTMSQLiteStatement statementWithSQL:selectSQL
                                          inDatabase:db
                                           errorCode:&err];
    XCTAssertNotNil(statement, @"Failed to create select statement");
    XCTAssertEqual(err, SQLITE_OK, @"Failed to create select statement");

    err = [statement stepRow];
    // Check that we got at least one row back
    XCTAssertEqual(err, SQLITE_ROW, @"did not retrieve a row from db :-(");
    XCTAssertEqual([statement resultColumnCount], 1,
                   @"result had more columns than the table had?");

    XCTAssertEqualStrings([statement resultColumnNameAtPosition:BLOB_COLUMN],
                          @"bc",
                          @"column name dictionary was not correct");

    XCTAssertEqual([statement rowDataCount],
                   1,
                   @"More than one column returned?");

    XCTAssertEqual([statement resultColumnTypeAtPosition:BLOB_COLUMN],
                   SQLITE_BLOB,
                   @"Query for column 1 of test table was not BLOB!");

    NSData *returnedbytes = [statement resultBlobDataAtPosition:BLOB_COLUMN];
    XCTAssertTrue([originalBytes isEqualToData:returnedbytes],
                  @"Queried data was not equal :-(");
    [statement finalizeStatement];
  }
}

- (void)testBindingNull {
  int err;
  GTMSQLiteDatabase *db =
    [[[GTMSQLiteDatabase alloc] initInMemoryWithCFAdditions:YES
                                                       utf8:YES
                                                  errorCode:&err]
      autorelease];

  err = [db executeSQL:
              @"CREATE TABLE foo (tc TEXT);"];

  XCTAssertEqual(err, SQLITE_OK,
                 @"Failed to create table for NULL binding test");
  NSString *insert = @"insert into foo (tc) values (:tc);";

  GTMSQLiteStatement *statement = [GTMSQLiteStatement statementWithSQL:insert
                                                            inDatabase:db
                                                             errorCode:&err];
  XCTAssertNotNil(statement, @"Failed to create insert statement");
  XCTAssertEqual(err, SQLITE_OK, @"Failed to create insert statement");

  err = [statement bindSQLNullAtPosition:1];

  XCTAssertEqual(err, SQLITE_OK, @"error binding NULL at position 1");

  err = [statement stepRow];
  XCTAssertEqual(err, SQLITE_DONE, @"failed to insert NULL for Null Binding test");

  [statement finalizeStatement];

  NSString *selectSQL = @"select 1 from foo where tc is NULL;";
  statement = [GTMSQLiteStatement statementWithSQL:selectSQL
                                        inDatabase:db
                                         errorCode:&err];
  XCTAssertNotNil(statement, @"Failed to create select statement");
  XCTAssertEqual(err, SQLITE_OK, @"Failed to create select statement");

  err = [statement stepRow];
  // Check that we got at least one row back
  XCTAssertEqual(err, SQLITE_ROW, @"did not retrieve a row from db :-(");
  [statement finalizeStatement];
}

- (void)testBindingDoubles {
  int err;
  GTMSQLiteDatabase *db =
    [[[GTMSQLiteDatabase alloc] initInMemoryWithCFAdditions:YES
                                                       utf8:YES
                                                  errorCode:&err]
      autorelease];

  // Test strategy is to create a table with 2 real columns.
  // For the first one, we'll use bindDoubleAtPosition
  // For the second one, we'll use bindNumberAsDoubleAtPosition
  // Then, for verification, we'll use a query that returns
  // all rows where the columns are equal
  double testVal = 42.42;
  NSNumber *doubleValue = [NSNumber numberWithDouble:testVal];

  err = [db executeSQL:
              @"CREATE TABLE realTable (rc1 REAL, rc2 REAL);"];

  XCTAssertEqual(err, SQLITE_OK,
                 @"Failed to create table for double binding test");
  NSString *insert = @"insert into realTable (rc1, rc2) values (:rc1, :rc2);";

  GTMSQLiteStatement *statement = [GTMSQLiteStatement statementWithSQL:insert
                                                            inDatabase:db
                                                             errorCode:&err];
  XCTAssertNotNil(statement, @"Failed to create insert statement");
  XCTAssertEqual(err, SQLITE_OK, @"Failed to create insert statement");

  err = [statement bindDoubleAtPosition:1 value:testVal];
  XCTAssertEqual(err, SQLITE_OK, @"error binding double at position 1");

  err = [statement bindNumberAsDoubleAtPosition:2 number:doubleValue];
  XCTAssertEqual(err, SQLITE_OK,
                 @"error binding number as double at "
                 @"position 2");

  err = [statement stepRow];
  XCTAssertEqual(err, SQLITE_DONE,
                 @"failed to insert doubles for double "
                 @"binding test");

  [statement finalizeStatement];

  NSString *selectSQL = @"select rc1, rc2 from realTable where rc1 = rc2;";
  statement = [GTMSQLiteStatement statementWithSQL:selectSQL
                                        inDatabase:db
                                         errorCode:&err];
  XCTAssertNotNil(statement, @"Failed to create select statement");
  XCTAssertEqual(err, SQLITE_OK, @"Failed to create select statement");

  err = [statement stepRow];
  // Check that we got at least one row back
  XCTAssertEqual(err, SQLITE_ROW, @"did not retrieve a row from db :-(");
  double retrievedValue = [statement resultDoubleAtPosition:0];
  XCTAssertEqualWithAccuracy(retrievedValue, testVal, 0.01,
                             @"Retrieved double did not equal "
                             @"original");

  NSNumber *retrievedNumber = [statement resultNumberAtPosition:1];
  XCTAssertEqualObjects(retrievedNumber, doubleValue,
                        @"Retrieved NSNumber object did not equal");

  [statement finalizeStatement];
}

- (void) testResultCollectionAPI {
  int err;
  GTMSQLiteDatabase *dbWithCF =
    [[[GTMSQLiteDatabase alloc] initInMemoryWithCFAdditions:YES
                                                       utf8:YES
                                                  errorCode:&err]
      autorelease];

  GTMSQLiteDatabase *dbWithoutCF =
    [[[GTMSQLiteDatabase alloc] initInMemoryWithCFAdditions:NO
                                                       utf8:YES
                                                  errorCode:&err]
      autorelease];

  NSArray *databases = [NSArray arrayWithObjects:dbWithCF, dbWithoutCF, nil];
  GTMSQLiteDatabase *db;
  for (db in databases) {
    // Test strategy is to create a table with 3 columns
    // Insert some values, and use the result collection APIs
    // to make sure we get the same values back
    err = [db executeSQL:
                @"CREATE TABLE test (a integer, b text, c blob, d text);"];

    XCTAssertEqual(err, SQLITE_OK,
                   @"Failed to create table for result collection test");

    NSString *insert =
      @"insert into test (a, b, c, d) "
      @"values (42, 'text text', :bc, null);";

    GTMSQLiteStatement *statement = [GTMSQLiteStatement statementWithSQL:insert
                                                              inDatabase:db
                                                               errorCode:&err];
    XCTAssertNotNil(statement, @"Failed to create insert statement");
    XCTAssertEqual(err, SQLITE_OK, @"Failed to create insert statement");


    char blobChars[] = "DEADBEEF";
    NSUInteger blobLength = strlen(blobChars);
    NSData *blobData = [NSData dataWithBytes:blobChars length:blobLength];

    err = [statement bindBlobAtPosition:1 data:blobData];
    XCTAssertEqual(err, SQLITE_OK, @"error binding BLOB at position 1");

    err = [statement stepRow];
    XCTAssertEqual(err, SQLITE_DONE,
                   @"failed to insert doubles for double "
                   @"binding test");

    NSString *selectSQL = @"select * from test;";

    [statement finalizeStatement];

    statement = [GTMSQLiteStatement statementWithSQL:selectSQL
                                          inDatabase:db
                                           errorCode:&err];
    XCTAssertNotNil(statement, @"Failed to create select statement");
    XCTAssertEqual(err, SQLITE_OK, @"Failed to create select statement");

    err = [statement stepRow];
    // Check that we got at least one row back
    XCTAssertEqual(err, SQLITE_ROW, @"did not retrieve a row from db :-(");
    XCTAssertNotNil([statement resultRowArray],
                    @"Failed to retrieve result array");
    XCTAssertNotNil([statement resultRowDictionary],
                    @"Failed to retrieve result dictionary");
    [statement finalizeStatement];
  }
}

- (void) testBindingIntegers {
  int err;
  GTMSQLiteDatabase *db =
    [[[GTMSQLiteDatabase alloc] initInMemoryWithCFAdditions:YES
                                                       utf8:YES
                                                  errorCode:&err]
      autorelease];

  // Test strategy is to create a table with 2 real columns.
  // For the first one, we'll use bindIntegerAtPosition
  // For the second one, we'll use bindNumberAsIntegerAtPosition
  // Then, for verification, we'll use a query that returns
  // all rows where the columns are equal
  int testVal = 42;
  NSNumber *intValue = [NSNumber numberWithInt:testVal];

  err = [db executeSQL:
              @"CREATE TABLE integerTable (ic1 integer, ic2 integer);"];

  XCTAssertEqual(err, SQLITE_OK,
                 @"Failed to create table for integer binding test");
  NSString *insert =
    @"insert into integerTable (ic1, ic2) values (:ic1, :ic2);";

  GTMSQLiteStatement *statement = [GTMSQLiteStatement statementWithSQL:insert
                                                            inDatabase:db
                                                             errorCode:&err];
  XCTAssertNotNil(statement, @"Failed to create insert statement");
  XCTAssertEqual(err, SQLITE_OK, @"Failed to create insert statement");

  err = [statement bindInt32AtPosition:1 value:testVal];
  XCTAssertEqual(err, SQLITE_OK, @"error binding integer at position 1");

  err = [statement bindNumberAsInt32AtPosition:2 number:intValue];
  XCTAssertEqual(err, SQLITE_OK,
                 @"error binding number as integer at "
                 @"position 2");

  err = [statement stepRow];
  XCTAssertEqual(err, SQLITE_DONE,
                 @"failed to insert integers for integer "
                 @"binding test");

  [statement finalizeStatement];

  NSString *selectSQL = @"select ic1, ic2 from integerTable where ic1 = ic2;";
  statement = [GTMSQLiteStatement statementWithSQL:selectSQL
                                        inDatabase:db
                                         errorCode:&err];
  XCTAssertNotNil(statement, @"Failed to create select statement");
  XCTAssertEqual(err, SQLITE_OK, @"Failed to create select statement");

  err = [statement stepRow];
  // Check that we got at least one row back
  XCTAssertEqual(err, SQLITE_ROW, @"did not retrieve a row from db :-(");
  int retrievedValue = [statement resultInt32AtPosition:0];
  XCTAssertEqual(retrievedValue, testVal,
                 @"Retrieved integer did not equal "
                 @"original");

  NSNumber *retrievedNumber = [statement resultNumberAtPosition:1];
  XCTAssertEqualObjects(retrievedNumber, intValue,
                        @"Retrieved NSNumber object did not equal");

  [statement finalizeStatement];
}

- (void) testBindingLongLongs {
  int err;
  GTMSQLiteDatabase *db =
    [[[GTMSQLiteDatabase alloc] initInMemoryWithCFAdditions:YES
                                                       utf8:YES
                                                  errorCode:&err]
      autorelease];

  // Test strategy is to create a table with 2 long long columns.
  // For the first one, we'll use bindLongLongAtPosition
  // For the second one, we'll use bindNumberAsLongLongAtPosition
  // Then, for verification, we'll use a query that returns
  // all rows where the columns are equal
  long long testVal = LLONG_MAX;
  NSNumber *longlongValue = [NSNumber numberWithLongLong:testVal];

  err = [db executeSQL:
      @"CREATE TABLE longlongTable (llc1 integer, llc2 integer);"];

  XCTAssertEqual(err, SQLITE_OK,
                 @"Failed to create table for long long binding test");
  NSString *insert =
    @"insert into longlongTable (llc1, llc2) "
    @"values (:llc1, :llc2);";

  GTMSQLiteStatement *statement = [GTMSQLiteStatement statementWithSQL:insert
                                                            inDatabase:db
                                                             errorCode:&err];
  XCTAssertNotNil(statement, @"Failed to create insert statement");
  XCTAssertEqual(err, SQLITE_OK, @"Failed to create insert statement");

  err = [statement bindLongLongAtPosition:1 value:testVal];
  XCTAssertEqual(err, SQLITE_OK, @"error binding long long at position 1");

  err = [statement bindNumberAsLongLongAtPosition:2 number:longlongValue];
  XCTAssertEqual(err, SQLITE_OK,
                 @"error binding number as long long at "
                 @"position 2");

  err = [statement stepRow];
  XCTAssertEqual(err, SQLITE_DONE,
                 @"failed to insert long longs for long long "
                 @"binding test");

  [statement finalizeStatement];

  NSString *selectSQL = @"select llc1, llc2 from longlongTable where llc1 = llc2;";

  statement = [GTMSQLiteStatement statementWithSQL:selectSQL
                                        inDatabase:db
                                         errorCode:&err];
  XCTAssertNotNil(statement, @"Failed to create select statement");
  XCTAssertEqual(err, SQLITE_OK, @"Failed to create select statement");

  err = [statement stepRow];
  // Check that we got at least one row back
  XCTAssertEqual(err, SQLITE_ROW, @"did not retrieve a row from db :-(");
  long long retrievedValue = [statement resultLongLongAtPosition:0];
  XCTAssertEqual(retrievedValue, testVal,
                 @"Retrieved long long did not equal "
                 @"original");

  NSNumber *retrievedNumber = [statement resultNumberAtPosition:1];
  XCTAssertEqualObjects(retrievedNumber, longlongValue,
                        @"Retrieved NSNumber object did not equal");

  [statement finalizeStatement];
}

- (void) testBindingString {
  int err;
  GTMSQLiteDatabase *db =
    [[[GTMSQLiteDatabase alloc] initInMemoryWithCFAdditions:YES
                                                       utf8:YES
                                                  errorCode:&err]
      autorelease];

  // Test strategy is to create a table with 1 string column
  // Then, for verification, we'll use a query that returns
  // all rows where the strings are equal
  err = [db executeSQL:
              @"CREATE TABLE stringTable (sc1 text);"];

  XCTAssertEqual(err, SQLITE_OK,
                 @"Failed to create table for string binding test");

  NSString *insert =
    @"insert into stringTable (sc1) "
    @"values (:sc1);";

  GTMSQLiteStatement *statement = [GTMSQLiteStatement statementWithSQL:insert
                                                            inDatabase:db
                                                             errorCode:&err];
  XCTAssertNotNil(statement, @"Failed to create insert statement");
  XCTAssertEqual(err, SQLITE_OK, @"Failed to create insert statement");

  NSString *testVal = @"this is a test string";
  err = [statement bindStringAtPosition:1 string:testVal];
  XCTAssertEqual(err, SQLITE_OK, @"error binding string at position 1");

  err = [statement stepRow];
  XCTAssertEqual(err, SQLITE_DONE,
                 @"failed to insert string for string binding test");

  [statement finalizeStatement];

  NSString *selectSQL =
    [NSString stringWithFormat:@"select 1 from stringtable where sc1 = '%@';",
              testVal];

  statement = [GTMSQLiteStatement statementWithSQL:selectSQL
                                        inDatabase:db
                                         errorCode:&err];
  XCTAssertNotNil(statement, @"Failed to create select statement");
  XCTAssertEqual(err, SQLITE_OK, @"Failed to create select statement");

  err = [statement stepRow];
  // Check that we got at least one row back
  XCTAssertEqual(err, SQLITE_ROW, @"did not retrieve a row from db :-(");
  err = [statement stepRow];
  XCTAssertEqual(err, SQLITE_DONE, @"retrieved more than 1 row from db :-(");
  [statement finalizeStatement];
}

- (void)testThatNotFinalizingStatementsThrowsAssertion {
  NSAutoreleasePool *localPool = [[NSAutoreleasePool alloc] init];

  int err;
  GTMSQLiteDatabase *db =
    [[[GTMSQLiteDatabase alloc] initInMemoryWithCFAdditions:YES
                                                       utf8:YES
                                                  errorCode:&err]
      autorelease];

  XCTAssertNotNil(db, @"Failed to create database");

  sqlite3 *sqlite3DB = [db sqlite3DB];

  NSString *selectSQL = @"select 1";
  GTMSQLiteStatement *statement;
  statement = [GTMSQLiteStatement statementWithSQL:selectSQL
                                        inDatabase:db
                                         errorCode:&err];
  XCTAssertNotNil(statement, @"Failed to create select statement");
  XCTAssertEqual(err, SQLITE_OK, @"Failed to create select statement");

  sqlite3_stmt *sqlite3Statment = [statement sqlite3Statement];

  err = [statement stepRow];
  XCTAssertEqual(err, SQLITE_ROW,
                 @"failed to step row for finalize test");
  [localPool drain];

  // Clean up leaks. Since we hadn't finalized the statement above we
  // were unable to clean up the sqlite databases. Since the pool is drained
  // all of our objective-c objects are gone, so we have to call the
  // sqlite3 api directly.
  XCTAssertEqual(sqlite3_finalize(sqlite3Statment), SQLITE_OK);
  XCTAssertEqual(sqlite3_close(sqlite3DB), SQLITE_OK);
}

- (void)testCompleteSQLString {
  NSString *str = @"CREATE TABLE longlongTable (llc1 integer, llc2 integer);";
  BOOL isComplete = [GTMSQLiteStatement isCompleteStatement:str];
  XCTAssertTrue(isComplete);
  isComplete = [GTMSQLiteStatement isCompleteStatement:@""];
  XCTAssertFalse(isComplete);
  isComplete = [GTMSQLiteStatement isCompleteStatement:@"CR"];
  XCTAssertFalse(isComplete);
}

- (void)testQuotingSQLString {
  NSString *str = @"This is wild! It's fun!";
  NSString *str2 = [GTMSQLiteStatement quoteAndEscapeString:str];
  XCTAssertEqualObjects(str2, @"'This is wild! It''s fun!'");
  str2 = [GTMSQLiteStatement quoteAndEscapeString:@""];
  XCTAssertEqualObjects(str2, @"''");
}

- (void)testVersion {
  XCTAssertGreaterThan([GTMSQLiteDatabase sqliteVersionNumber], 0);
  XCTAssertNotNil([GTMSQLiteDatabase sqliteVersionString]);
}

@end

static void TestUpperLower16Impl(sqlite3_context *context,
                                 int argc, sqlite3_value **argv) {

  customUpperFunctionCalled = YES;
}
