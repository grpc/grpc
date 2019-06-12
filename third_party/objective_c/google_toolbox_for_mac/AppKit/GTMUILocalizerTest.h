//
//  GTMUILocalizerTest.h
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

#import <Cocoa/Cocoa.h>

@interface GTMUILocalizerTestWindowController : NSWindowController {
  IBOutlet NSWindow *_anotherWindow;
  IBOutlet NSMenu *_otherMenu;

  // Window Items
  IBOutlet NSTextField *_bindingsTextField;
  IBOutlet NSSearchField *_bindingsSearchField;
  IBOutlet NSToolbarItem *_toolbarItem1;
  IBOutlet NSToolbarItem *_toolbarItem2;
  IBOutlet NSTabViewItem *_tabViewItem1;
  IBOutlet NSTabViewItem *_tabViewItem2;
  IBOutlet NSButton *_button1;
  IBOutlet NSButton *_button2;
  IBOutlet NSTextField *_textField1;
  IBOutlet NSTextField *_textField2;
  IBOutlet NSButton *_checkbox1;
  IBOutlet NSButton *_checkbox2;
  IBOutlet NSMenuItem *_menuItem1;
  IBOutlet NSMenuItem *_menuItem2;
  IBOutlet NSMenuItem *_menuItem3;
  IBOutlet NSButtonCell *_radio1;
  IBOutlet NSButtonCell *_radio2;

  // AnotherWindow Items
  IBOutlet NSBox *_aBox;
  IBOutlet NSButton *_aButton1;
  IBOutlet NSButton *_aButton2;
  IBOutlet NSButton *_aCheckbox1;
  IBOutlet NSButton *_aCheckbox2;
  IBOutlet NSButtonCell *_aRadio1;
  IBOutlet NSButtonCell *_aRadio2;
  IBOutlet NSTextField *_aTextField1;
  IBOutlet NSTextField *_aTextField2;
  IBOutlet NSSegmentedControl *_aSegmented;
  IBOutlet NSComboBox *_aComboBox;
}

@property (nonatomic, retain) NSWindow *anotherWindow;
@property (nonatomic, retain) NSMenu *otherMenu;

// Window Items
@property (nonatomic, retain) NSTextField *bindingsTextField;
@property (nonatomic, retain) NSSearchField *bindingsSearchField;
@property (nonatomic, retain) NSToolbarItem *toolbarItem1;
@property (nonatomic, retain) NSToolbarItem *toolbarItem2;
@property (nonatomic, retain) NSTabViewItem *tabViewItem1;
@property (nonatomic, retain) NSTabViewItem *tabViewItem2;
@property (nonatomic, retain) NSButton *button1;
@property (nonatomic, retain) NSButton *button2;
@property (nonatomic, retain) NSTextField *textField1;
@property (nonatomic, retain) NSTextField *textField2;
@property (nonatomic, retain) NSButton *checkbox1;
@property (nonatomic, retain) NSButton *checkbox2;
@property (nonatomic, retain) NSMenuItem *menuItem1;
@property (nonatomic, retain) NSMenuItem *menuItem2;
@property (nonatomic, retain) NSMenuItem *menuItem3;
@property (nonatomic, retain) NSButtonCell *radio1;
@property (nonatomic, retain) NSButtonCell *radio2;

// AnotherWindow Items
@property (nonatomic, retain) NSBox *aBox;
@property (nonatomic, retain) NSButton *aButton1;
@property (nonatomic, retain) NSButton *aButton2;
@property (nonatomic, retain) NSButton *aCheckbox1;
@property (nonatomic, retain) NSButton *aCheckbox2;
@property (nonatomic, retain) NSButtonCell *aRadio1;
@property (nonatomic, retain) NSButtonCell *aRadio2;
@property (nonatomic, retain) NSTextField *aTextField1;
@property (nonatomic, retain) NSTextField *aTextField2;
@property (nonatomic, retain) NSSegmentedControl *aSegmented;
@property (nonatomic, retain) NSComboBox *aComboBox;

@end

@interface GTMUILocalizerTestViewController : NSViewController {
  IBOutlet NSView *_otherView;
  IBOutlet NSButton *_otherButton;
  IBOutlet NSButton *_viewButton;
  IBOutlet NSTextField *_pollyTextField;
}

@property (nonatomic, retain) NSView *otherView;
@property (nonatomic, retain) NSButton *otherButton;
@property (nonatomic, retain) NSButton *viewButton;
@property (nonatomic, retain) NSTextField *pollyTextField;
@end
