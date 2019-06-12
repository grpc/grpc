//
//  GTMSQLite.h
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
//
//  This class is a convenience wrapper for SQLite storage with
//  release/retain semantics. In its most basic form, that is all this
//  class offers. You have the option of activating "CFAdditions" on
//  init which patches or overrides the following SQLite functionality:
//
//  - Strings you pass through the API layer will always be converted
//  to precomposed UTF-8 with compatibility mapping
//  (kCFStringNormalizationFormKC).  This is done in an attempt to
//  make SQLite correctly handle string equality for composed
//  character sequences. This change applies only to
//  NSStrings/CFStrings passed through the GTMSQLiteDatabase or
//  GTMSQLiteStatement. Direct access to the database using the
//  underlying sqlite3_* handles is not affected.
//
//  - The SQL UPPER/LOWER functions are replaced with CFString-based
//  implementations which (unlike SQLite's native implementation)
//  handle case conversion outside the ASCII range. These
//  implementations seem to be 20-30% slower than the SQLite
//  implementations but may be worth it for accuracy.
//
//  - The SQLite "NOCASE" collation is replaced with a CFString-based
//  collation that is case insensitive but still uses literal
//  comparison (composition-sensitive).
//
//  - Additional collation sequences can be created by using these keywords
//  separated by underscores. Each option corresponds to a CFStringCompareFlags
//  option.
//    NOCASE                  (kCFCompareCaseInsensitive)
//    NONLITERAL              (kCFCompareNonliteral)
//    LOCALIZED               (kCFCompareLocalized)
//    NUMERIC                 (kCFCompareNumerically)

//  These additional options are available when linking with the 10.5 SDK:
//    NODIACRITIC             (kCFCompareDiacriticInsensitive)
//    WIDTHINSENSITIVE        (kCFCompareWidthInsensitive)
//
//  Ordering of the above options can be changed by adding "REVERSE".
//
//  Thus, for a case-insensitive, width-insensitive, composition-insensitive
//  comparison that ignores diacritical marks and sorts in reverse use:
//
//    NOCASE_NONLITERAL_NODIACRITIC_WIDTHINSENSITIVE_REVERSE
//
//  - SQL LIKE and GLOB commands are implemented with CFString/CFCharacterSet
//  comparisons. As with the other CF additions, this gives us better handling
//  of case and composed character sequences. However, whereever reasonable,
//  SQLite semantics have been retained. Specific notes:
//
//    * LIKE is case insensitive and uses non-literal comparison
//      (kCFCompareNonliteral) by default. It is possible to modify this
//      behavior using the accessor methods. You must use those methods
//      instead of the SQLite  "PRAGMA case_sensitive_like" in order for them
//      to interact properly with our CFString implementations.
//
//    * ESCAPE clauses to LIKE are honored, but the escape character must
//      be expressable as a single UniChar (UTF16). The escaped characters in
//      LIKE only escape the following UniChar, not a composed character
//      sequence. This is not viewed as a limitation since the use of ESCAPE
//      is typically only for characters with meaning to SQL LIKE ('%', '_')
//      all of which can be expressed as a single UniChar.
//
//    * GLOB is by default case sensitive but non-literal. Again, accessor
//      methods are available to change this behavior.
//
//    * Single character pattern matches ('_' for LIKE, '?' for GLOB) will
//      always consume a full composed character sequence.
//
//    * As with the standard SQLite implementation, character set comparisons
//      are only available for GLOB.
//
//    * Character set comparisons are always literal and case sensitive and do
//      not take into account composed character sequences. Essentially
//      character sets should always be expressed as a set of single UniChars
//      or ranges between single UniChars.
//

#import <Foundation/Foundation.h>
#import <sqlite3.h>

/// Wrapper for SQLite with release/retain semantics and CFString convenience features
@interface GTMSQLiteDatabase : NSObject {
 @protected
  sqlite3 *db_;     // strong
  NSString *path_;   // strong
  int timeoutMS_;
  BOOL hasCFAdditions_;
  CFOptionFlags likeOptions_;
  CFOptionFlags globOptions_;
  NSMutableArray *userArgDataPool_;  // strong
}

//  Get the numeric version number of the SQLite library (compiled in value
//  for SQLITE_VERSION_NUMBER).
//
//  Returns:
//    Integer version number
//
+ (int)sqliteVersionNumber;

//  Get the string version number of the SQLite library.
//
//  Returns:
//    Autoreleased NSString version string
//
+ (NSString *)sqliteVersionString;

//  Create and open a database instance on a file-based database.
//
//  Args:
//    path: Path to the database. If it does not exist an empty database
//          will be created.
//    withCFAdditions: If true, the SQLite database will include CFString
//                     based string functions and collation sequences. See
//                     the class header for information on these differences
//                     and performance impact.
//    err:  Result code from SQLite. If nil is returned by this function
//          check the result code for the error. If NULL no result code is
//          reported.
//
- (id)initWithPath:(NSString *)path
   withCFAdditions:(BOOL)additions
              utf8:(BOOL)useUTF8
         errorCode:(int *)err;

//  Create and open a memory-based database. Memory-based databases
//  cannot be shared amongst threads, and each instance is unique. See
//  SQLite documentation for details.
//
//  For argument details see [... initWithPath:withCFAdditions:errorCode:]
//
- (id)initInMemoryWithCFAdditions:(BOOL)additions
                             utf8:(BOOL)useUTF8
                        errorCode:(int *)err;

//  Get the underlying SQLite database handle. In general you should
//  never do this, if you do use this be careful with how you compose
//  and decompse strings you pass to the database.
//
//  Returns:
//    sqlite3 pointer
//
- (sqlite3 *)sqlite3DB;

//  Enable/Disable the database synchronous mode. Disabling
//  synchronous mode results in much faster insert throughput at the
//  cost of safety. See the SQlite documentation for details.
//
//  Args:
//    enable: Boolean flag to determine mode.
//
- (void)synchronousMode:(BOOL)enable;

//  Check if this database instance has our CFString functions and collation
//  sequences (see top of file for details).
//
//  Returns:
//    YES if the GTMSQLiteDatabase instance has our CF additions
//
- (BOOL)hasCFAdditions;

//  Set comparison options for the "LIKE" operator for databases with
//  our CF addtions active.
//
//  Args:
//    options: CFStringCompareFlags value. Note that a limited list
//             of options are supported. For example one cannot
//             use kCFCompareBackwards as an option.
//
- (void)setLikeComparisonOptions:(CFOptionFlags)options;

//  Get current comparison options for the "LIKE" operator in a database
//  with our CF additions active.
//
//  Returns:
//    Current comparison options or zero if CF additions are inactive.
//
- (CFOptionFlags)likeComparisonOptions;

//  Set comparison options for the "GLOB" operator for databases with
//  our CF addtions active.
//
//  Args:
//    options: CFStringCompareFlags value. Note that a limited list
//             of options are supported. For example one cannot
//             use kCFCompareBackwards as an option.
//
- (void)setGlobComparisonOptions:(CFOptionFlags)options;

//  Get current comparison options for the "GLOB" operator in a database
//  with our CF additions active.
//
//  Returns:
//    Current comparison options or zero if CF additions are inactive.
//
- (CFOptionFlags)globComparisonOptions;

//  Obtain the last error code from the database
//
//  Returns:
//    SQLite error code, if no error is pending returns SQLITE_OK
//
- (int)lastErrorCode;

//  Obtain an error string for the last error from the database
//
//  Returns:
//    Autoreleased NSString error message
//
- (NSString *)lastErrorString;

//  Obtain a count of rows added, mmodified or deleted by the most recent
//  statement. See sqlite3_changes() for details and limitations.
//
//  Returns:
//    Row count
//
- (int)lastChangeCount;

//  Obtain a count of rows added, mmodified or deleted since the database
//  was opened. See sqlite3_total_changes() for details and limitations.
//
//  Returns:
//    Row count
//
- (int)totalChangeCount;

//  Obtain the last insert row ID
//
//  Returns:
//    64-bit row ID
//
- (unsigned long long)lastInsertRowID;

//  Interrupt any currently running database operations as soon as possible.
//  Running operations will receive a SQLITE_INTERRUPT and will need to
//  handle it correctly (this is the callers problem to deal with).
//
- (void)interrupt;

//  Set the timeout value in milliseconds. This is a database global affecting
//  all running and future statements.
//
//  Args:
//    timeoutMS: Integer count in ms SQLite will wait for the database to
//               unlock before giving up and returning SQLITE_BUSY. A value
//               of 0 or less means the database always returns immediately.
//
//  Returns:
//    SQLite result code, SQLITE_OK on no error
//
- (int)setBusyTimeoutMS:(int)timeoutMS;

//  Get the current busy timeout in milliseconds.
//
//  Returns:
//    Current database busy timeout value in ms, 0 or less means no timeout.
//
- (int)busyTimeoutMS;

//  Execute a string containing one or more SQL statements. No returned data
//  is available, use GTMSQLiteStatement for that usage.
//
//  Args:
//    sql: Raw SQL statement to prepare. It is the caller's responsibility
//         to properly escape the SQL.
//
//  Returns:
//    SQLite result code, SQLITE_OK on no error
//
- (int)executeSQL:(NSString *)sql;

//  Convenience method to start a deferred transaction (most common case).
//
//  Returns:
//    YES if the transaction started successfully
//
- (BOOL)beginDeferredTransaction;

//  Convenience method to roll back a transaction.
//
//  Returns:
//    YES if the transaction rolled back successfully
//
- (BOOL)rollback;

//  Convenience method to commit a transaction.
//
//  Returns:
//    YES if the transaction committed successfully
//
- (BOOL)commit;

@end

//  Wrapper class for SQLite statements with retain/release semantics.
//  Attempts to behave like an NSEnumerator, however you should bind
//  your values before beginning enumeration and unlike NSEnumerator,
//  a reset is supported.
//
//  The GTMSQLiteDatabase class has options to modify some SQL
//  functions and force particular string representations. This class
//  honors the database preferences for those options. See the
//  GTMSQLiteDatabase header for details.

///  Wrapper class for SQLite statements with retain/release
///  semantics.
@interface GTMSQLiteStatement : NSObject {

@protected
  sqlite3_stmt *statement_;
  BOOL hasCFAdditions_;
}

#pragma mark Creation, Access and Finalization

//  Create an autoreleased prepared statement, see initWithSQL: for arguments.
//
//  NOTE: Even though this object is autoreleased you MUST call
//  [finalizeStatement] on this when your done. See the init for explanation.
//
//  Returns:
//    Autoreleased GTMSQLiteStatement
//
+ (id)statementWithSQL:(NSString *)sql
            inDatabase:(GTMSQLiteDatabase *)gtmdb
             errorCode:(int *)err;

//  Designated initializer, create a prepared statement. Positional and named
//  parameters are supported, see the SQLite documentation.
//
//  NOTE: Although this object will clean up its statement when deallocated,
//  you are REQUIRED to "finalize" the statement when you are
//  through with it. Failing to do this will prevent the database from allowing
//  new transactions or queries. In other words, leaving an instance on the
//  autorelease pool unfinalized may interfere with other database usage if any
//  caller sharing the database uses transactions.
//
//  Args:
//    sql: Raw SQL statement to prepare. It is the caller's responsibility
//         to properly escape the SQL and make sure that the SQL contains
//         only _one_ statement. Additional statements are silently ignored.
//    db: The GTMSQLiteDatabase (not retained)
//    err:  Result code from SQLite. If nil is returned by this function
//          check the result code for the error. If NULL no result code is
//          reported.
//
- (id)initWithSQL:(NSString *)sql
       inDatabase:(GTMSQLiteDatabase *)gtmdb
        errorCode:(int *)err;

//  Get the underlying SQLite statement handle. In general you should never
//  do this, if you do use this be careful with how you compose and
//  decompse strings you pass to the database.
//
//  Returns:
//    sqlite3_stmt pointer
//
- (sqlite3_stmt *)sqlite3Statement;

//  Finalize the statement, allowing other transactions to start on the database
//  This method MUST be called when you are done with a statement.  Failure to
//  do so means that the database will not be torn down properly when it's
//  retain count drops to 0 or GC collects it.
//
//  Returns:
//    SQLite result code, SQLITE_OK on no error
//
- (int)finalizeStatement;

#pragma mark Parameters and Binding

//  Get the number of parameters that can be bound in the prepared statement.
//
//  Returns:
//    Integer count of parameters or -1 on error
//
- (int)parameterCount;

//  Get the position of a parameter with a given name.
//
//  Args:
//    paramName: String name of the parameter, including any leading punctuation
//               (see SQLite docs)
//
//  Returns:
//    1-based parameter position index or -1 on error
//
- (int)positionOfParameterNamed:(NSString *)paramName;

//  Get the name of a parameter at a particular index.
//
//  Args:
//    position: Parameter position (1-based index)
//
//  Returns:
//    Autoreleased string name of the parameter, including any leading
//    punctuation (see SQLite docs) or nil on error.
//
- (NSString *)nameOfParameterAtPosition:(int)position;

//  Bind a NULL at a given position
//
//  Args:
//    position: Parameter position (1-based index)
//
//  Returns:
//    SQLite result code, SQLITE_OK on no error
//
- (int)bindSQLNullAtPosition:(int)position;

//  Bind a blob parameter at a given position index to a raw pointer and
//  length. The data will be copied by SQLite
//
//  Args:
//    position: Parameter position (1-based index)
//    bytes: Raw pointer to the data to copy/bind
//    length: Number of bytes in the blob
//
//  Returns:
//    SQLite result code, SQLITE_OK on no error
//
- (int)bindBlobAtPosition:(int)position bytes:(void *)bytes length:(int)length;

//  Bind an NSData as a blob at a given position. The data will be copied
//  by SQLite.
//
//  Args:
//    position: Parameter position (1-based index)
//    data: NSData to convert to blob
//
//  Returns:
//    SQLite result code, SQLITE_OK on no error
//
- (int)bindBlobAtPosition:(int)position data:(NSData *)data;

//  Bind a double at the given position (for floats convert to double).
//
//  Args:
//    position: Parameter position (1-based index)
//    value: Double to bind
//
//  Returns:
//    SQLite result code, SQLITE_OK on no error
//
- (int)bindDoubleAtPosition:(int)position value:(double)value;

//  Bind an NSNumber as a double value at the given position.
//
//  Args:
//    position: Parameter position (1-based index)
//    number: NSNumber to bind
//
//  Returns:
//    SQLite result code, SQLITE_OK on no error
//
- (int)bindNumberAsDoubleAtPosition:(int)position number:(NSNumber *)number;

//  Bind a 32-bit integer at the given position.
//
//  Args:
//    position: Parameter position (1-based index)
//    value: Integer to bind
//
//  Returns:
//    SQLite result code, SQLITE_OK on no error
//
- (int)bindInt32AtPosition:(int)position value:(int)value;

//  Bind an NSNumber as a 32-bit integer value at the given position.
//
//  Args:
//    position: Parameter position (1-based index)
//    number: NSNumber to bind
//
//  Returns:
//    SQLite result code, SQLITE_OK on no error
//
- (int)bindNumberAsInt32AtPosition:(int)position number:(NSNumber *)number;

//  Bind a 64-bit integer at the given position.
//
//  Args:
//    position: Parameter position (1-based index)
//    value: Int64 value to bind
//
//  Returns:
//    SQLite result code, SQLITE_OK on no error
//
- (int)bindLongLongAtPosition:(int)position value:(long long)value;

//  Bind an NSNumber as a 64-bit integer value at the given position.
//
//  Args:
//    position: Parameter position (1-based index)
//    number: NSNumber to bind
//
//  Returns:
//    SQLite result code, SQLITE_OK on no error
//
- (int)bindNumberAsLongLongAtPosition:(int)position number:(NSNumber *)number;

//  Bind a string at the given position.
//
//  Args:
//    position: Parameter position (1-based index)
//    string: String to bind (string will be converted to UTF8 and copied).
//            NOTE: For bindings it is not necessary for you to SQL escape
//                  your strings.
//
//  Returns:
//    SQLite result code, SQLITE_OK on no error
//
- (int)bindStringAtPosition:(int)position string:(NSString *)string;

#pragma mark Results

//  Get the number of result columns per row this statement will generate.
//
//  Returns:
//    Column count, 0 if no columns will be returned ("UPDATE.." etc.),
//    -1 on error.
//
- (int)resultColumnCount;

//  Get the name of result colument at a given index.
//
//  Args:
//    position: Column position (0-based index)
//
//  Returns:
//    Autoreleased NSString column name or nil if no column exists at that
//    position or error.
//
- (NSString *)resultColumnNameAtPosition:(int)position;

//  Get the number of data values in the current row of this statement.
//  Generally this will be the same as resultColumnCount:, except when row
//  iteration is done (see SQLite docs for sqlite3_data_count()).
//
//  Returns:
//    Data count or 0 if no data will be returned, -1 on error.
//
- (int)rowDataCount;

//  Get the SQLite type constant for a column in a row. Note that because
//  SQLite does not enforce column type restrictions the type of a particular
//  column in a row may not match the declared type of the column.
//
//  Args:
//    position: Column position (0-based index)
//
//  Returns:
//    SQLite data type constant (i.e. SQLITE_INTEGER, SQLITE_FLOAT, etc.) or
//    -1 on error.
//
- (int)resultColumnTypeAtPosition:(int)position;

//  Get the data for a result row blob column as an NSData
//
//  Args:
//    position: Column position (0-based index)
//
//  Returns:
//    Autoreleased NSData, nil on error
//
- (NSData *)resultBlobDataAtPosition:(int)position;

//  Get the data for a result row blob column as a double
//
//  Args:
//    position: Column position (0-based index)
//
//  Returns:
//    Double value
//
- (double)resultDoubleAtPosition:(int)position;

//  Get the data for a result row blob column as an integer
//
//  Args:
//    position: Column position (0-based index)
//
//  Returns:
//    Integer value
//
- (int)resultInt32AtPosition:(int)position;

//  Get the data for a result row blob column as a long long
//
//  Args:
//    position: Column position (0-based index)
//
//  Returns:
//    Long long value
//
- (long long)resultLongLongAtPosition:(int)position;

//  Get the data for a result row blob column as an NSNumber
//
//  Args:
//    position: Column position (0-based index)
//
//  Returns:
//    Autoreleased NSNumber value or nil on error
//
- (NSNumber *)resultNumberAtPosition:(int)position;

//  Get the data for a result row blob column as an NSString
//
//  Args:
//    position: Column position (0-based index)
//
//  Returns:
//    Autoreleased NSString value or nil on error
//
- (NSString *)resultStringAtPosition:(int)position;

//  Get a Foundation object (NSData, NSNumber, NSString, NSNull) for the column,
//  autodetecting the most appropriate representation.
//
//  Args:
//    position: Column position (0-based index)
//
//  Returns:
//    Autoreleased Foundation type, nil on error
//
- (id)resultFoundationObjectAtPosition:(int)position;

//  Get an array of Foundation objects for the row in query column order.
//
//  Returns:
//    Autoreleased array of Foundation types or nil if there is no
//    data in the row or error
//
- (NSArray *)resultRowArray;

//  Get a dictionary of Foundation objects for the row keyed by column name.
//
//  Returns:
//    Autoreleased dictionary of Foundation types or nil if there is no
//    data in the row or error.
//
- (NSDictionary *)resultRowDictionary;

#pragma mark Rows

//  Step the statement forward one row, potentially spinning forever till
//  the row can be located (if database is SQLITE_BUSY).
//
//  Returns:
//    SQLite result code, SQLITE_ROW if a row was found or SQLITE_DONE if
//    no further rows match the statement.
//
- (int)stepRow;

//  Step the statement forward one row, waiting at most the currrent database
//  busy timeout (see [GTMSQLiteDatabase setBusyTimeoutMS]).
//
//  Returns:
//    SQLite result code, SQLITE_ROW if a row was found or SQLITE_DONE if
//    no further rows match the statement. If SQLITE_BUSY is returned the
//    database did not unlock during the timeout.
//
- (int)stepRowWithTimeout;

//  Reset the statement starting again at the first row
//
//  Returns:
//    SQLite result code, SQLITE_OK on no error
//
- (int)reset;

//  Check if the SQLite parser recognizes the receiver as one or more valid
//  SQLite statements.
//
//  Returns:
//    YES if the string is a complete and valid SQLite statement
//
+ (BOOL)isCompleteStatement:(NSString *)string;

//  Quote and escape the receiver for SQL.
//  Example: "This is wild! It's fun!"
//  Becomes: "'This is wild! It''s fun!'"
//
//  Returns:
//    Autoreleased NSString
+ (NSString *)quoteAndEscapeString:(NSString *)string;

@end
