//
//  GTMIBArray.h
//
//  Copyright 2009 Google Inc.
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
#import <AppKit/NSNibDeclarations.h>

// This class allows you to create dynamically sized arrays of objects in your
// nib.  This saves you from adding a random "large" number of outlets on an
// object to accommodate a variable number of connections. If you need <= 5
// objects you only need to create one of these in your nib. If you need > 5
// objects you can connect any of the outlets in a given GTMIBArray to another
// instance of GTMIBArray and we will recurse through it to create the final
// array.
@interface GTMIBArray : NSArray {
 @protected
  IBOutlet id object1_;
  IBOutlet id object2_;
  IBOutlet id object3_;
  IBOutlet id object4_;
  IBOutlet id object5_;
  NSArray *realArray_;
}

@end
