//
//  GTMUILocalizerTest.h
//
//  Copyright 2011 Google Inc.
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

#import <UIKit/UIKit.h>

#import "GTMUILocalizer.h"

@interface GTMUILocalizerTestViewController : UIViewController {
 @private
  UILabel *label_;
  UIButton *button_;
  UISegmentedControl *segmentedControl_;
  UISearchBar *searchBar_;
}

@property(nonatomic, retain) IBOutlet UILabel *label;
@property(nonatomic, retain) IBOutlet UIButton *button;
@property(nonatomic, retain) IBOutlet UISegmentedControl *segmentedControl;
@property(nonatomic, retain) IBOutlet UISearchBar *searchBar;

@end
