//
//  GTMUILocalizer.h
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

// A class for localizing nibs by doing simple string replacement.
// To use this, make an instance of GTMUILocalizer in your nib. Connect the
// owner_ outlet of the your instance to the File Owner of the nib. It expects
// the owner_ outlet to be an instance or subclass of NSWindowController,
// NSViewController or NSApplication. Using the bundle of the nib it will then
// localize any items in the NSWindowController's window and subviews, or the
// NSViewController's view and subviews or the NSApplication's main menu
// and dockmenu at when awakeFromNib is called on the GTMUILocalizer instance.
// You can optionally hook up otherObjectToLocalize_ and
// yetAnotherObjectToLocalize_ and those will also be localized. Strings in the
// nib that you want localized must start with ^ (shift-6). The strings will
// be looked up in the Localizable.strings table without the caret as the
// key.
// Things that will be localized are:
//  - Titles and altTitles (for menus, buttons, windows, menuitems, tabViewItem)
//  - stringValue (for labels)
//  - tooltips
//  - accessibility help
//  - menus
//
// Due to technical limitations, accessibility description cannot be localized.
// See http://lists.apple.com/archives/Accessibility-dev/2009/Dec/msg00004.html
// and http://openradar.appspot.com/7496255 for more information.
//
// As an example if I wanted to localize a button with the word "Print" on
// it, I would put it in a window controlled by a NSWindowController that was
// the owner of the nib. I would set it's title to be "^Print". I would then
// create an instance of GTMUILocalizer and set it's owner_ to be the owner
// of the nib.
// In my Localizable.strings file in my fr.lproj directory for the bundle
// I would put "Print" = "Imprimer";
// Then when my app launched in French I would get a button labeled
// "Imprimer". Note that GTMUILocalizer is only for strings, and doesn't
// resize, move or change text alignment on any of the things it modifies.
// If you absolutely need a caret at the beginning of the string
// post-localization, you can put "Foo" = "^Foo"; in your strings file and
// it will work.
// Your nib could be located in a variety of places depending on what you want
// to do. I would recommend having your "master" nib directly in Resources.
// If for some reason you needed to do some custom localization of the
// nib you could copy the master nib into your specific locale folder, and
// then you only need to adjust the items in the nib that you need to
// customize. You can leave the strings in the "^Foo" convention and they
// will localize properly. This keeps the differences between the nibs down
// to the bare essentials.
//
// NOTE: NSToolbar localization support is limited to only working on the
// default items in the toolbar. We cannot localize items that are on of the
// customization palette but not in the default items because there is not an
// API for NSToolbar to get all possible items. You are responsible for
// localizing all non-default toolbar items by hand.
//
@interface GTMUILocalizer : NSObject {
 @protected
  IBOutlet id owner_;
  IBOutlet id otherObjectToLocalize_;
  IBOutlet id yetAnotherObjectToLocalize_;
 @private
  NSBundle *bundle_;
}
- (id)initWithBundle:(NSBundle *)bundle;

// Localize |object|. If |recursive| is true, it will attempt
// to localize objects owned/referenced by |object|.
- (void)localizeObject:(id)object recursively:(BOOL)recursive;

// A method for subclasses to override in case you have a different
// way to go about getting localized strings.
// If |string| does not start with ^ you should return nil.
// If |string| is nil, you should return nil
- (NSString *)localizedStringForString:(NSString *)string;

// Allows subclasses to override how the bundle is picked up
+ (NSBundle *)bundleForOwner:(id)owner;
@end
