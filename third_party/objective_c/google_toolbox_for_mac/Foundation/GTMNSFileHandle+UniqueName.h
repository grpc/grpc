//
//  GTMNSFileHandle+UniqueName.h
//
//  Copyright 2010 Google Inc.
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
#import "GTMDefines.h"

@interface NSFileHandle (GTMFileHandleUniqueNameAdditions)

// Creates a read/write temporary file in NSTemporaryDirectory with mode 0600.
// The template should be similar to the template passed to mkstemp.
// If there is an extension on the nameTemplate it will remain. An example
// template is "MyAppXXXXXX.txt".
// If |path| is not nil, it will contain the derived path for the file.
// The file descriptor wrapped by the NSFileHandle will be closed on dealloc.
+ (id)gtm_fileHandleForTemporaryFileBasedOn:(NSString *)nameTemplate
                                  finalPath:(NSString **)path;

// Return an opened read/write file handle with mode 0600 based on a template.
// The template should be similar to the template passed to mkstemp.
// If there is an extension on the pathTemplate it will remain. An example
// template is "/Applications/MyAppXXXXXX.txt".
// If |path| is not nil, it will contain the derived path for the file.
// The file descriptor wrapped by the NSFileHandle will be closed on dealloc.
+ (id)gtm_fileHandleWithUniqueNameBasedOn:(NSString *)pathTemplate
                                finalPath:(NSString **)path;

// Same as fileHandleWithUniqueNameBasedOn:finalName: but splits up the
// template from the directory.
+ (id)gtm_fileHandleWithUniqueNameBasedOn:(NSString *)nameTemplate
                              inDirectory:(NSString *)directory
                                finalPath:(NSString **)path;


// Same as fileHandleWithUniqueNameBasedOn:inDirectory:finalName: but finds
// the directory using the |directory| and |mask| arguments.
+ (id)gtm_fileHandleWithUniqueNameBasedOn:(NSString *)nameTemplate
                              inDirectory:(NSSearchPathDirectory)directory
                               domainMask:(NSSearchPathDomainMask)mask
                                finalPath:(NSString **)path;
@end

@interface NSFileManager (GTMFileManagerUniqueNameAdditions)

// Creates a new directory in NSTemporaryDirectory with mode 0700.
// The template should be similar to the template passed to mkdtemp.
- (NSString *)gtm_createTemporaryDirectoryBasedOn:(NSString *)nameTemplate;

// Return the path to a directory with mode 0700 based on a template.
// The template should be similar to the template passed to mkdtemp.
- (NSString *)gtm_createDirectoryWithUniqueNameBasedOn:(NSString *)nameTemplate;

// Same as createDirectoryWithUniqueNameBasedOn: but splits up the
// template from the directory.
- (NSString *)gtm_createDirectoryWithUniqueNameBasedOn:(NSString *)pathTemplate
                                           inDirectory:(NSString *)directory;

// Same as createDirectoryWithUniqueNameBasedOn:inDirectory: but finds
// the directory using the |directory| and |mask| arguments.
- (NSString *)gtm_createDirectoryWithUniqueNameBasedOn:(NSString *)pathTemplate
                                           inDirectory:(NSSearchPathDirectory)directory
                                            domainMask:(NSSearchPathDomainMask)mask;
@end
