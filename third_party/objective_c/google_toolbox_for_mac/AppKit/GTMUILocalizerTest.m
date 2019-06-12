//
//  GTMUILocalizerTest.m
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


#import "GTMSenTestCase.h"
#import "GTMUILocalizerTest.h"
#import "GTMUILocalizer.h"

@interface GTMUILocalizerTest : GTMTestCase
@end

@implementation GTMUILocalizerTest
// Utility method to verify that all the options for |binding| on |object| have
// been localized.
- (void)verifyBinding:(NSString *)binding forObject:(id)object {
  NSDictionary *bindingInfo
    = [object infoForBinding:binding];
  XCTAssertNotNil(bindingInfo,
                  @"Can't get binding info for %@ from %@.\nExposed bindings: %@",
                  binding, object, [object exposedBindings]);
  NSDictionary *bindingOptions = [bindingInfo objectForKey:NSOptionsKey];
  XCTAssertNotNil(bindingOptions);
  NSString *key = nil;
  for(key in bindingOptions) {
    id value = [bindingOptions objectForKey:key];
    if ([value isKindOfClass:[NSString class]]) {
      XCTAssertFalse([value hasPrefix:@"^"],
                     @"Binding option %@ not localized. Has value %@.",
                     key, value);
    }
  }
}

- (void)testWindowLocalization {
  GTMUILocalizerTestWindowController *controller
    = [[GTMUILocalizerTestWindowController alloc] init];

  // Window automatically localized on load
  XCTAssertEqualObjects(controller.window.title, @"Window");
  XCTAssertEqualObjects(controller.tabViewItem1.label, @"Localized Tab");
  XCTAssertEqualObjects(controller.tabViewItem2.label, @"^Tab2");
  XCTAssertEqualObjects(controller.toolbarItem1.label, @"Localized Toolbar Item Label");
  XCTAssertEqualObjects(controller.toolbarItem1.paletteLabel,
                        @"Localized Toolbar Item Palette Label");
  XCTAssertEqualObjects(controller.toolbarItem2.label, @"ToolbarItemLabel");
  XCTAssertEqualObjects(controller.toolbarItem2.paletteLabel, @"ToolbarItemPaletteLabel");
  XCTAssertEqualObjects(controller.button1.title, @"Localized Button");
  XCTAssertEqualObjects(controller.button2.title, @"^Button2");
  XCTAssertEqualObjects(controller.textField1.stringValue, @"Localized Label");
  XCTAssertEqualObjects(controller.textField2.stringValue, @"^Label2");
  XCTAssertEqualObjects(controller.button1.title, @"Localized Button");
  XCTAssertEqualObjects(controller.button2.title, @"^Button2");
  XCTAssertEqualObjects(controller.checkbox1.title, @"Localized Checkbox 1");
  XCTAssertEqualObjects(controller.checkbox2.title, @"^Checkbox 2");
  XCTAssertEqualObjects(controller.menuItem1.title, @"Localized Item 1");
  XCTAssertEqualObjects(controller.menuItem2.title, @"Localized Item 2");
  XCTAssertEqualObjects(controller.menuItem3.title, @"^Item 3");
  XCTAssertEqualObjects(controller.radio1.title, @"Localized Radio 1");
  XCTAssertEqualObjects(controller.radio2.title, @"Localized Radio 2");

  // Another Window Before Localization
  XCTAssertEqualObjects(controller.anotherWindow.title, @"^WindowTest");
  XCTAssertEqualObjects(controller.aBox.title, @"^Box");
  XCTAssertEqualObjects(controller.aButton1.title, @"^Button1");
  XCTAssertEqualObjects(controller.aButton2.title, @"^Button2");
  XCTAssertEqualObjects(controller.aCheckbox1.title, @"^Checkbox 1");
  XCTAssertEqualObjects(controller.aCheckbox2.title, @"^Checkbox 2");
  XCTAssertEqualObjects(controller.aRadio1.title, @"^Radio 1");
  XCTAssertEqualObjects(controller.aRadio2.title, @"^Radio 2");
  XCTAssertEqualObjects(controller.aTextField1.stringValue, @"^Label1");
  XCTAssertEqualObjects(controller.aTextField2.stringValue, @"^Label2");
  NSSegmentedControl *segmented = controller.aSegmented;
  XCTAssertEqualObjects([segmented labelForSegment:0], @"^Seg1");
  XCTAssertEqualObjects([segmented labelForSegment:1], @"^Seg2");
  XCTAssertEqualObjects([segmented labelForSegment:2], @"^Seg3");
  NSComboBox *comboBox = controller.aComboBox;
  XCTAssertEqualObjects(comboBox.stringValue, @"^Label1");
  XCTAssertEqualObjects(comboBox.placeholderString, @"^Placeholder1");
  NSArray *objects = comboBox.objectValues;
  NSArray *expectedObjects = [NSArray arrayWithObjects:
                              @"^Choice1", @"^Choice2", @"^Choice3", @"^Choice4", @"^Choice5", nil];
  XCTAssertEqualObjects(objects, expectedObjects);

  NSBundle *bundle = [NSBundle bundleForClass:[self class]];
  GTMUILocalizer *localizer = [[GTMUILocalizer alloc] initWithBundle:bundle];
  [localizer localizeObject:controller.anotherWindow recursively:YES];

  XCTAssertEqualObjects(controller.anotherWindow.title, @"Localized Window");
  XCTAssertEqualObjects(controller.aBox.title, @"Localized Box");
  XCTAssertEqualObjects(controller.aButton1.title, @"Localized Button");
  XCTAssertEqualObjects(controller.aButton2.title, @"^Button2");
  XCTAssertEqualObjects(controller.aCheckbox1.title, @"Localized Checkbox 1");
  XCTAssertEqualObjects(controller.aCheckbox2.title, @"^Checkbox 2");
  XCTAssertEqualObjects(controller.aRadio1.title, @"Localized Radio 1");
  XCTAssertEqualObjects(controller.aRadio2.title, @"Localized Radio 2");
  XCTAssertEqualObjects(controller.aTextField1.stringValue, @"Localized Label");
  XCTAssertEqualObjects(controller.aTextField2.stringValue, @"^Label2");
  XCTAssertEqualObjects([segmented labelForSegment:0], @"Localized Segment 1");
  XCTAssertEqualObjects([segmented labelForSegment:1], @"Localized Segment 2");
  XCTAssertEqualObjects([segmented labelForSegment:2], @"^Seg3");
  XCTAssertEqualObjects(comboBox.stringValue, @"Localized Label");
  XCTAssertEqualObjects(comboBox.placeholderString, @"Localized Placeholder");
  objects = comboBox.objectValues;
  expectedObjects = [NSArray arrayWithObjects:
      @"Localized Choice 1", @"Localized Choice 2", @"Localized Choice 3",
      @"^Choice4", @"^Choice5", nil];
  XCTAssertEqualObjects(objects, expectedObjects);

  NSMenu *menu = controller.otherMenu;
  XCTAssertNotNil(menu);
  [localizer localizeObject:menu recursively:YES];
  XCTAssertEqualObjects(menu.title, @"Localized Menu");
  NSMenuItem *item = [menu itemAtIndex:0];
  XCTAssertEqualObjects(item.title, @"Localized Menu Item");


  // Test binding localization.
  NSTextField *textField = controller.bindingsTextField;
  XCTAssertNotNil(textField);
  NSString *displayPatternValue1Binding
    = [NSString stringWithFormat:@"%@1", NSDisplayPatternValueBinding];
  [self verifyBinding:displayPatternValue1Binding forObject:textField];

  NSSearchField *searchField = controller.bindingsSearchField;
  XCTAssertNotNil(searchField);
  [self verifyBinding:NSPredicateBinding forObject:searchField];

  [localizer release];
  [controller release];
}

- (void)testViewLocalization {
  NSBundle *bundle = [NSBundle bundleForClass:[self class]];
  GTMUILocalizer *localizer = [[GTMUILocalizer alloc] initWithBundle:bundle];
  XCTAssertNotNil(localizer);

  GTMUILocalizerTestViewController *controller
    = [[GTMUILocalizerTestViewController alloc] init];
  NSView *view = controller.view;
  XCTAssertNotNil(view);
  XCTAssertEqualObjects(controller.viewButton.title, @"Localized Button");
  XCTAssertEqualObjects(controller.pollyTextField.stringValue, @"^Polly want a caret?");

  // We don't expect otherView to be localized.
  view = controller.otherView;
  XCTAssertNotNil(view);
  XCTAssertEqualObjects(controller.otherButton.title, @"^Button");

  [controller release];
}
@end

@implementation GTMUILocalizerTestWindowController

@synthesize anotherWindow = _anotherWindow;
@synthesize otherMenu = _otherMenu;

// Window Items
@synthesize bindingsTextField = _bindingsTextField;
@synthesize bindingsSearchField = _bindingsSearchField;
@synthesize toolbarItem1 = _toolbarItem1;
@synthesize toolbarItem2 = _toolbarItem2;
@synthesize tabViewItem1 = _tabViewItem1;
@synthesize tabViewItem2 = _tabViewItem2;
@synthesize button1 = _button1;
@synthesize button2 = _button2;
@synthesize textField1 = _textField1;
@synthesize textField2 = _textField2;
@synthesize checkbox1 = _checkbox1;
@synthesize checkbox2 = _checkbox2;
@synthesize menuItem1 = _menuItem1;
@synthesize menuItem2 = _menuItem2;
@synthesize menuItem3 = _menuItem3;
@synthesize radio1 = _radio1;
@synthesize radio2 = _radio2;

// Another Window Items
@synthesize aBox = _aBox;
@synthesize aButton1 = _aButton1;
@synthesize aButton2 = _aButton2;
@synthesize aCheckbox1 = _aCheckbox1;
@synthesize aCheckbox2 = _aCheckbox2;
@synthesize aRadio1 = _aRadio1;
@synthesize aRadio2 = _aRadio2;
@synthesize aTextField1 = _aTextField1;
@synthesize aTextField2 = _aTextField2;
@synthesize aSegmented = _aSegmented;
@synthesize aComboBox = _aComboBox;

- (id)init {
  return [self initWithWindowNibName:@"GTMUILocalizerTestWindow"];
}
@end

@implementation GTMUILocalizerTestViewController
@synthesize otherView = _otherView;
@synthesize otherButton = _otherButton;
@synthesize viewButton = _viewButton;
@synthesize pollyTextField = _pollyTextField;

- (id)init {
  NSBundle *bundle = [NSBundle bundleForClass:[self class]];
  return [self initWithNibName:@"GTMUILocalizerTestView" bundle:bundle];
}
@end
