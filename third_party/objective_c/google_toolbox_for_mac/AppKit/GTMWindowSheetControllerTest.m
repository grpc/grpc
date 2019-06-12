//
//  GTMWindowSheetControllerTest.m
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
#import "GTMWindowSheetController.h"

@interface GTMWindowSheetControllerTest : GTMTestCase
                                          <GTMWindowSheetControllerDelegate,
                                           NSTabViewDelegate> {
 @private
  GTMWindowSheetController *sheetController_;
  NSWindow* window_;
  BOOL didAlertClose_;
  BOOL didSheetClose_;
}
- (void)alertDidEnd:(NSAlert *)alert
         returnCode:(NSInteger)returnCode
            context:(void *)context;
- (void)openSecondSheet:(NSAlert *)alert
             returnCode:(NSInteger)returnCode
                context:(void *)context;
- (void)sheetDidEnd:(NSWindow *)sheet
         returnCode:(NSInteger)returnCode
            context:(void *)context;
@end

@implementation GTMWindowSheetControllerTest

- (void)testOpenTwoSheetsAndSwitch {
  // Set up window
  NSWindow *window =
      [[[NSWindow alloc] initWithContentRect:NSMakeRect(100, 100, 600, 600)
                                   styleMask:NSTitledWindowMask
                                     backing:NSBackingStoreBuffered
                                       defer:NO] autorelease];
  XCTAssertNotNil(window, @"Could not allocate window");
  NSTabView *tabView =
      [[[NSTabView alloc] initWithFrame:NSMakeRect(10, 10, 580, 580)]
      autorelease];
  XCTAssertNotNil(tabView, @"Could not allocate tab view");
  [[window contentView] addSubview:tabView];
  [tabView setDelegate:self];

  NSTabViewItem *item1 =
      [[[NSTabViewItem alloc] initWithIdentifier:@"one"] autorelease];
  [item1 setLabel:@"One"];
  NSTabViewItem *item2 =
      [[[NSTabViewItem alloc] initWithIdentifier:@"two"] autorelease];
  [item2 setLabel:@"Two"];
  [tabView addTabViewItem:item1];
  [tabView addTabViewItem:item2];

  sheetController_ =
      [[[GTMWindowSheetController alloc] initWithWindow:window
                                               delegate:self] autorelease];

  XCTAssertFalse([sheetController_ isSheetAttachedToView:
                  [[tabView selectedTabViewItem] view]],
                 @"Sheet should not be attached to current view");
  XCTAssertEqual([[sheetController_ viewsWithAttachedSheets] count],
                 (NSUInteger)0,
                 @"Should have no views with sheets");

  // Pop alert on first tab
  NSAlert* alert = [[NSAlert alloc] init];

  [alert setMessageText:@"Hell Has Broken Loose."];
  [alert setInformativeText:@"All hell has broken loose. You may want to run "
                            @"outside screaming and waving your arms around "
                            @"wildly."];

  NSButton *alertButton = [alert addButtonWithTitle:@"OK"];

  [sheetController_ beginSystemSheet:alert
                        modalForView:[item1 view]
                      withParameters:[NSArray arrayWithObjects:
                                      [NSNull null],
                                      self,
                                      [NSValue valueWithPointer:
                                        @selector(alertDidEnd:returnCode:context:)],
                                      [NSValue valueWithPointer:nil],
                                      nil]];
  didAlertClose_ = NO;

  XCTAssertTrue([sheetController_ isSheetAttachedToView:
                 [[tabView selectedTabViewItem] view]],
                 @"Sheet should be attached to current view");
  XCTAssertEqual([[sheetController_ viewsWithAttachedSheets] count],
                 (NSUInteger)1,
                 @"Should have one view with sheets");

  [tabView selectTabViewItem:item2];

  XCTAssertFalse([sheetController_ isSheetAttachedToView:
                  [[tabView selectedTabViewItem] view]],
                 @"Sheet should not be attached to current view");
  XCTAssertEqual([[sheetController_ viewsWithAttachedSheets] count],
                 (NSUInteger)1,
                 @"Should have one view with sheets");

  // Pop sheet on second tab
  NSPanel *sheet =
      [[[NSPanel alloc] initWithContentRect:NSMakeRect(0, 0, 300, 200)
                                  styleMask:NSTitledWindowMask
                                    backing:NSBackingStoreBuffered
                                      defer:NO] autorelease];

  [sheetController_ beginSheet:sheet
                  modalForView:[item2 view]
                 modalDelegate:self
                didEndSelector:@selector(sheetDidEnd:returnCode:context:)
                   contextInfo:nil];
  didSheetClose_ = NO;

  XCTAssertTrue([sheetController_ isSheetAttachedToView:
                 [[tabView selectedTabViewItem] view]],
                @"Sheet should be attached to current view");
  XCTAssertEqual([[sheetController_ viewsWithAttachedSheets] count],
                 (NSUInteger)2,
                 @"Should have two views with sheets");

  [tabView selectTabViewItem:item1];

  XCTAssertTrue([sheetController_ isSheetAttachedToView:
                 [[tabView selectedTabViewItem] view]],
                @"Sheet should be attached to current view");
  XCTAssertEqual([[sheetController_ viewsWithAttachedSheets] count],
                 (NSUInteger)2,
                 @"Should have two views with sheets");

  // Close alert
  [alertButton performClick:self];

  XCTAssertFalse([sheetController_ isSheetAttachedToView:
                  [[tabView selectedTabViewItem] view]],
                 @"Sheet should not be attached to current view");
  XCTAssertEqual([[sheetController_ viewsWithAttachedSheets] count],
                 (NSUInteger)1,
                 @"Should have one view with sheets");
  XCTAssertTrue(didAlertClose_, @"Alert should have closed");

  [tabView selectTabViewItem:item2];

  XCTAssertTrue([sheetController_ isSheetAttachedToView:
                 [[tabView selectedTabViewItem] view]],
                @"Sheet should be attached to current view");
  XCTAssertEqual([[sheetController_ viewsWithAttachedSheets] count],
                 (NSUInteger)1,
                 @"Should have one view with sheets");

  // Close sheet
  [[NSApplication sharedApplication] endSheet:sheet returnCode:NSOKButton];

  XCTAssertFalse([sheetController_ isSheetAttachedToView:
                  [[tabView selectedTabViewItem] view]],
                 @"Sheet should not be attached to current view");
  XCTAssertEqual([[sheetController_ viewsWithAttachedSheets] count],
                 (NSUInteger)0,
                 @"Should have no views with sheets");
  XCTAssertTrue(didSheetClose_, @"Sheet should have closed");
}

- (void)testOpenSheetAfterFirst {
  // Set up window
  window_ =
    [[[NSWindow alloc] initWithContentRect:NSMakeRect(100, 100, 600, 600)
                                 styleMask:NSTitledWindowMask
                                   backing:NSBackingStoreBuffered
                                     defer:NO] autorelease];
  XCTAssertNotNil(window_, @"Could not allocate window");

  sheetController_ =
      [[[GTMWindowSheetController alloc] initWithWindow:window_
                                               delegate:self] autorelease];

  XCTAssertFalse([sheetController_ isSheetAttachedToView:[window_ contentView]],
                 @"Sheet should not be attached to current view");
  XCTAssertEqual([[sheetController_ viewsWithAttachedSheets] count],
                 (NSUInteger)0,
                 @"Should have no views with sheets");

  // Pop alert on first tab
  NSAlert* alert = [[NSAlert alloc] init];

  [alert setMessageText:@"Hell Has Broken Loose."];
  [alert setInformativeText:@"All hell has broken loose. You may want to run "
                            @"outside screaming and waving your arms around "
                            @"wildly."];

  NSButton *alertButton = [alert addButtonWithTitle:@"OK"];

  // Allocate a second sheet to be launched by the alert
  NSPanel *sheet =
      [[[NSPanel alloc] initWithContentRect:NSMakeRect(0, 0, 300, 200)
                                  styleMask:NSTitledWindowMask
                                    backing:NSBackingStoreBuffered
                                      defer:NO] autorelease];

  [sheetController_ beginSystemSheet:alert
                        modalForView:[window_ contentView]
                      withParameters:[NSArray arrayWithObjects:
                                      [NSNull null],
                                      self,
                                      [NSValue valueWithPointer:
                                        @selector(openSecondSheet:returnCode:context:)],
                                      [NSValue valueWithPointer:sheet],
                                      nil]];
  didAlertClose_ = NO;
  didSheetClose_ = NO;

  XCTAssertTrue([sheetController_ isSheetAttachedToView:[window_ contentView]],
                @"Sheet should be attached to view");
  XCTAssertEqual([[sheetController_ viewsWithAttachedSheets] count],
                 (NSUInteger)1,
                 @"Should have one view with sheets");

  // Close alert
  [alertButton performClick:self];

  XCTAssertTrue([sheetController_ isSheetAttachedToView:[window_ contentView]],
                @"Second sheet should be attached to view");
  XCTAssertEqual([[sheetController_ viewsWithAttachedSheets] count],
                 (NSUInteger)1,
                 @"Should have one view with sheets");
  XCTAssertTrue(didAlertClose_, @"Alert should have closed");

  // Close sheet
  [[NSApplication sharedApplication] endSheet:sheet returnCode:NSOKButton];

  XCTAssertFalse([sheetController_ isSheetAttachedToView:[window_ contentView]],
                 @"Sheet should not be attached to current view");
  XCTAssertEqual([[sheetController_ viewsWithAttachedSheets] count],
                 (NSUInteger)0,
                 @"Should have no views with sheets");
  XCTAssertTrue(didSheetClose_, @"Sheet should have closed");
}

- (void)alertDidEnd:(NSAlert *)alert
         returnCode:(NSInteger)returnCode
            context:(void *)context {
  didAlertClose_ = YES;
}

- (void)openSecondSheet:(NSAlert *)alert
             returnCode:(NSInteger)returnCode
                context:(void *)context {
  didAlertClose_ = YES;

  NSPanel* sheet = context;
  // Pop a second sheet
  [sheetController_ beginSheet:sheet
                  modalForView:[window_ contentView]
                 modalDelegate:self
                didEndSelector:@selector(sheetDidEnd:returnCode:context:)
                   contextInfo:nil];
}

- (void)sheetDidEnd:(NSWindow *)sheet
         returnCode:(NSInteger)returnCode
            context:(void *)context {
  didSheetClose_ = YES;
  [sheet orderOut:self];
}

- (void)tabView:(NSTabView *)tabView
    didSelectTabViewItem:(NSTabViewItem *)tabViewItem {
  NSView* view = [tabViewItem view];
  [sheetController_ setActiveView:view];
}

- (void)gtm_systemRequestsVisibilityForView:(NSView*)view {
  XCTAssertTrue(false, @"Shouldn't be called");
}

@end
