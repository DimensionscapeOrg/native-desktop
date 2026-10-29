#include <native_desktop/Desktop.h>
#import <Cocoa/Cocoa.h>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
// Inside the namespace, its names are found before any of AppKit's
namespace native_desktop {

// The menu bar that hosts such as SDL install, with the actions AppKit gives Cocoa applications
static void Add (NSMenu* menu, NSString* title, SEL action, NSString* key, NSEventModifierFlags modifiers = NSEventModifierFlagCommand) {
 NSMenuItem* item = [menu addItemWithTitle:title action:action keyEquivalent:key];
 if ([key length]) [item setKeyEquivalentModifierMask:modifiers];
}
static NSMenu* CreateDefaultMenuBar () {
 NSMenu* bar = [[NSMenu alloc] initWithTitle:@""];
 NSMenu* application = [[NSMenu alloc] initWithTitle:@""];
 Add (application, @"About Q&A Tests", @selector(orderFrontStandardAboutPanel:), @"");
 [application addItem:[NSMenuItem separatorItem]];
 Add (application, @"Settings...", nil, @",");
 [application addItem:[NSMenuItem separatorItem]];
 NSMenu* services = [[NSMenu alloc] initWithTitle:@""];
 [[application addItemWithTitle:@"Services" action:nil keyEquivalent:@""] setSubmenu:services];
 [NSApp setServicesMenu:services];
 [application addItem:[NSMenuItem separatorItem]];
 Add (application, @"Hide Tests", @selector(hide:), @"h");
 Add (application, @"Hide Others", @selector(hideOtherApplications:), @"h", NSEventModifierFlagOption | NSEventModifierFlagCommand);
 Add (application, @"Show All", @selector(unhideAllApplications:), @"");
 [application addItem:[NSMenuItem separatorItem]];
 Add (application, @"Quit Tests", @selector(terminate:), @"q");
 [[bar addItemWithTitle:@"" action:nil keyEquivalent:@""] setSubmenu:application];
 // Keys that read back through each mapping, in a menu that AppKit adds nothing to
 NSMenu* keys = [[NSMenu alloc] initWithTitle:@"Keys"];
 Add (keys, @"Redo", @selector(redo:), @"Z");
 Add (keys, @"Delete", @selector(delete:), [NSString stringWithFormat:@"%C", (unichar)NSDeleteFunctionKey], NSEventModifierFlagControl | NSEventModifierFlagCommand);
 Add (keys, @"Refresh", @selector(refresh:), [NSString stringWithFormat:@"%C", (unichar)NSF5FunctionKey],
  NSEventModifierFlagControl | NSEventModifierFlagOption | NSEventModifierFlagCommand);
 [[bar addItemWithTitle:@"Keys" action:nil keyEquivalent:@""] setSubmenu:keys];
 NSMenu* window = [[NSMenu alloc] initWithTitle:@"Window"];
 Add (window, @"Close", @selector(performClose:), @"w");
 Add (window, @"Minimize", @selector(performMiniaturize:), @"m");
 Add (window, @"Zoom", @selector(performZoom:), @"");
 Add (window, @"Toggle Full Screen", @selector(toggleFullScreen:), @"f", NSEventModifierFlagControl | NSEventModifierFlagCommand);
 [[bar addItemWithTitle:@"Window" action:nil keyEquivalent:@""] setSubmenu:window];
 [NSApp setWindowsMenu:window];
 return bar;
}
static const MenuItem* Find (const std::vector<MenuItem>& items, const std::string& action) {
 for (size_t i = 0; i < items.size (); ++i) {
  if (items[i].action == action) return &items[i];
  const MenuItem* found = Find (items[i].submenu, action);
  if (found) return found;
 }
 return NULL;
}
static std::vector<MenuItem> Read () {
 std::vector<unsigned char> data; std::vector<MenuItem> items;
 assert (Menu::GetApplicationMenu (&data));
 assert (Menu::Parse (data.data (), (int)data.size (), &items));
 return items;
}
// Hiding and showing the application complete as AppKit events are delivered
static bool WaitForHidden (bool hidden) {
 for (int i = 0; i < 40 && [NSApp isHidden] != hidden; ++i) {
  NSDate* until = [NSDate dateWithTimeIntervalSinceNow:0.05];
  NSEvent* event;
  while ((event = [NSApp nextEventMatchingMask:NSEventMaskAny untilDate:until inMode:NSDefaultRunLoopMode dequeue:YES]) != nil) [NSApp sendEvent:event];
 }
 return [NSApp isHidden] == hidden;
}
static int Run () {
 if (!Menu::IsOwnerThread ()) return 1;
 [NSApplication sharedApplication];
 [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
 [NSApp finishLaunching];
 std::vector<unsigned char> data;
 if (![NSApp mainMenu]) assert (!Menu::GetApplicationMenu (&data));
 NSMenu* defaultBar = CreateDefaultMenuBar ();
 [NSApp setMainMenu:defaultBar];
 NSMenu* defaultServices = [NSApp servicesMenu];
 NSMenu* defaultWindows = [NSApp windowsMenu];

 // The default menu bar reads back with its standard items, keys and escaped titles.
 std::vector<MenuItem> items = Read ();
 assert (items.size () == 3 && items[0].submenu.size () == 11);
 assert (items[0].submenu[0].label == "About Q&&A Tests" && Menu::GetLabel (items[0].submenu[0].label, 0) == "About Q&A Tests");
 assert (items[0].submenu[0].action == "orderFrontStandardAboutPanel:");
 assert (items[0].submenu[1].flags & MENU_ITEM_SEPARATOR);
 assert (items[0].submenu[2].label == "Settings..." && items[0].submenu[2].action.empty () && items[0].submenu[2].keyCode == ',');
 const MenuItem* quit = Find (items, "terminate:");
 assert (quit && quit->label == "Quit Tests" && quit->keyCode == 'q' && quit->keyModifiers == MOD_GUI);
 const MenuItem* hideOthers = Find (items, "hideOtherApplications:");
 assert (hideOthers && hideOthers->keyCode == 'h' && hideOthers->keyModifiers == (MOD_GUI | MOD_ALT));
 // An uppercase key equivalent implies Shift; special and function keys map to their codes
 const MenuItem* redo = Find (items, "redo:");
 assert (redo && redo->keyCode == 'z' && redo->keyModifiers == (MOD_GUI | MOD_SHIFT));
 const MenuItem* remove = Find (items, "delete:");
 assert (remove && remove->keyCode == KEY_DELETE && remove->keyModifiers == (MOD_GUI | MOD_CTRL));
 const MenuItem* refresh = Find (items, "refresh:");
 assert (refresh && refresh->keyCode == KEY_F1 + 4 && refresh->keyModifiers == (MOD_GUI | MOD_ALT | MOD_CTRL));
 // AppKit manages the full screen item, and its key equivalent differs between versions of macOS
 assert (Find (items, "toggleFullScreen:"));
 const MenuItem* services = Find (items, "services");
 assert (services && (services->flags & MENU_ITEM_SUBMENU) && services->submenu.empty ());
 // AppKit may add items of its own to the Window menu
 assert (items[1].label == "Keys" && items[1].action.empty () && items[1].submenu.size () == 3);
 assert (items[2].label == "Window" && items[2].action == "windows" && items[2].submenu.size () >= 4);
 assert (items[2].submenu[0].action == "performClose:" && Find (items[2].submenu, "performMiniaturize:"));

 // A copy with a menu of the application's own keeps the standard items when installed.
 MenuItem open = MenuItem (); open.id = 0xE001; open.flags = MENU_ITEM_ENABLED; open.label = "Open";
 MenuItem file = MenuItem (); file.id = 0xE000; file.flags = MENU_ITEM_ENABLED | MENU_ITEM_SUBMENU; file.label = "File";
 file.submenu.push_back (open);
 items.insert (items.begin () + 1, file);
 Menu::Write (items, &data);
 assert (Menu::SetApplicationMenu (data.data (), (int)data.size ()));
 NSMenu* bar = [NSApp mainMenu];
 assert (bar != defaultBar && [bar numberOfItems] == 4);
 NSMenu* application = [[bar itemAtIndex:0] submenu];
 // The Services menu that macOS fills moves to the new menu bar, and the Window menu is replaced
 assert ([[application itemAtIndex:4] submenu] == defaultServices && [NSApp servicesMenu] == defaultServices);
 assert ([NSApp windowsMenu] == [[bar itemAtIndex:3] submenu] && [NSApp windowsMenu] != defaultWindows);
 std::vector<MenuItem> installed = Read ();
 assert (installed.size () == 4 && installed[1].label == "File" && installed[1].action.empty () && installed[1].submenu[0].action.empty ());
 assert (Find (installed, "terminate:") && Find (installed, "hide:") && Find (installed, "services") && installed[3].action == "windows");
 // Keys survive being installed and read back
 assert (Find (installed, "redo:")->keyCode == 'z' && Find (installed, "redo:")->keyModifiers == (MOD_GUI | MOD_SHIFT));
 assert (Find (installed, "refresh:")->keyCode == KEY_F1 + 4 && Find (installed, "refresh:")->keyModifiers == (MOD_GUI | MOD_ALT | MOD_CTRL));

 // Choosing Hide tells the host, then hides the application as on the default menu bar.
 const MenuItem* hide = Find (items, "hide:");
 Event event;
 while (Menu::PollEvent (&event)) {}
 [application performActionForItemAtIndex:[application indexOfItemWithTitle:@"Hide Tests"]];
 assert (Menu::PollEvent (&event) && event.kind == MENU_EVENT_SELECTION && event.target == 0 && event.code == hide->id);
 assert (WaitForHidden (true));
 [NSApp unhide:nil];
 assert (WaitForHidden (false));

 // The application's own items are selected as before.
 NSMenu* fileMenu = [[bar itemAtIndex:1] submenu];
 [fileMenu performActionForItemAtIndex:0];
 assert (Menu::PollEvent (&event) && event.kind == MENU_EVENT_SELECTION && event.code == 0xE001 && ![NSApp isHidden]);

 // Restoring the default menu bar restores the menus that macOS fills.
 assert (Menu::SetApplicationMenu (NULL, 0));
 assert ([NSApp mainMenu] == defaultBar && [NSApp servicesMenu] == defaultServices && [NSApp windowsMenu] == defaultWindows);
 assert ([[[[defaultBar itemAtIndex:0] submenu] itemAtIndex:4] submenu] == defaultServices && ![[application itemAtIndex:4] hasSubmenu]);
 std::cout << "macOS menu bar reading, standard items, selection and restoring passed\n";
 return 0;
}
}
int main () {
 @autoreleasepool {
  return native_desktop::Run ();
 }
}
