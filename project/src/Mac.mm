#include <native_desktop/Desktop.h>

#ifdef NATIVE_DESKTOP_MAC

// Native menus, tray icons and the Dock icon for macOS

#import <Cocoa/Cocoa.h>
#import <objc/runtime.h>
#include <native_desktop/Desktop.h>



#include <string.h>
#include <pthread.h>

#if __has_feature(objc_arc)
#define DESKTOP_AUTORELEASE(object) (object)
#define DESKTOP_RETAIN(object) (object)
#define DESKTOP_RELEASE(object)
#define DESKTOP_BRIDGE(type, pointer) ((__bridge type)(pointer))
#define DESKTOP_BRIDGE_RETAIN(object) ((void*)CFBridgingRetain (object))
#define DESKTOP_BRIDGE_RELEASE(pointer) CFRelease ((CFTypeRef)(pointer))
#else
#define DESKTOP_AUTORELEASE(object) [(object) autorelease]
#define DESKTOP_RETAIN(object) [(object) retain]
#define DESKTOP_RELEASE(object) [(object) release]
#define DESKTOP_BRIDGE(type, pointer) ((type)(pointer))
#define DESKTOP_BRIDGE_RETAIN(object) ((void*)[(object) retain])
#define DESKTOP_BRIDGE_RELEASE(pointer) [(NSObject*)(pointer) release]
#endif


@interface NativeDesktopMenuTarget : NSObject <NSMenuDelegate, NSApplicationDelegate> {

	@public
	NSInteger highlightedTag;
	NSInteger popupSelection;
	NSUInteger trackingDepth;
	NSTimer* idleTimer;

}

- (void)clickTrayIcon:(id)sender;
- (void)selectApplicationMenuItem:(NSMenuItem*)sender;
- (void)selectDockMenuItem:(NSMenuItem*)sender;
- (void)selectPopupMenuItem:(NSMenuItem*)sender;
- (void)selectSystemMenuItem:(NSMenuItem*)sender;
- (void)selectTrayIconMenuItem:(NSMenuItem*)sender;

@end


@implementation NativeDesktopMenuTarget

- (void)menuWillOpen:(NSMenu*)menu {
	if (trackingDepth++ == 0 && native_desktop::Menu::HasModalCallback ()) {
		idleTimer = DESKTOP_RETAIN ([NSTimer timerWithTimeInterval:0.02 repeats:YES block:^(NSTimer* timer) {
			if (!native_desktop::Menu::AdvanceModal ()) [menu cancelTracking];
		}]);
		[[NSRunLoop currentRunLoop] addTimer:idleTimer forMode:NSEventTrackingRunLoopMode];
	}
}
- (void)menuDidClose:(NSMenu*)menu {
	if (trackingDepth > 0 && --trackingDepth == 0 && idleTimer) {
		[idleTimer invalidate]; DESKTOP_RELEASE (idleTimer); idleTimer = nil;
	}
}

- (void)clickTrayIcon:(id)sender {

	NSEvent* event = [NSApp currentEvent];
	bool rightClick = event && ([event type] == NSEventTypeRightMouseUp || ([event modifierFlags] & NSEventModifierFlagControl));

	native_desktop::Menu::PushTrayIconEvent ((int)[sender tag], rightClick ? native_desktop::TRAY_ICON_RIGHT_CLICK : native_desktop::TRAY_ICON_CLICK, 0);

}

- (void)selectApplicationMenuItem:(NSMenuItem*)sender {

	// Queue the selection for the owning host.
	native_desktop::Menu::PushSelection (0, (int)[sender tag]);

}

- (void)selectDockMenuItem:(NSMenuItem*)sender {

	native_desktop::Menu::PushDockMenuSelection ((int)[sender tag]);

}

- (void)selectPopupMenuItem:(NSMenuItem*)sender {

	popupSelection = [sender tag];

}

- (void)selectSystemMenuItem:(NSMenuItem*)sender {

	// A standard item, such as Quit. The host hears of it, then AppKit does what it does.
	native_desktop::Menu::PushSelection (0, (int)[sender tag]);

	id action = [sender representedObject];
	if ([action isKindOfClass:[NSString class]]) [NSApp sendAction:NSSelectorFromString (action) to:nil from:sender];

}

- (void)selectTrayIconMenuItem:(NSMenuItem*)sender {

	// Tray icon menu items carry the tray icon ID in the high 32 bits of their tag
	NSInteger tag = [sender tag];
	native_desktop::Menu::PushTrayIconEvent ((int)(tag >> 32), native_desktop::TRAY_ICON_MENU_SELECT, (int)(tag & 0xFFFFFFFF));

}

- (void)menu:(NSMenu*)menu willHighlightItem:(NSMenuItem*)item {

	highlightedTag = (item && [item action] == @selector(selectPopupMenuItem:)) ? [item tag] : 0;

}

@end


namespace native_desktop {
	static bool EnsureApplication () {
		if (!pthread_main_np ()) return false;
		@autoreleasepool {
			if (!NSApp) {
				[NSApplication sharedApplication];
				[NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
				[NSApp finishLaunching];
			}
			return NSApp != nil;
		}
	}


	// Cocoa has a single application menu bar. Preserve and restore any menu
	// installed by the host before this library takes ownership.

	static NSMenu* defaultApplicationMenu = nil;
	static NSMenuItem* defaultServicesItem = nil;
	static NSMenu* defaultWindowsMenu = nil;
	static bool defaultApplicationMenuSaved = false;

	// The menus that macOS fills, named as the action of the item that holds them
	static NSString* const SERVICES_MENU = @"services";
	static NSString* const WINDOWS_MENU = @"windows";
	static NativeDesktopMenuTarget* menuTarget = nil;


	static NativeDesktopMenuTarget* GetMenuTarget () {

		if (!menuTarget) menuTarget = [[NativeDesktopMenuTarget alloc] init];
		return menuTarget;

	}


	static NSWindow* GetNSWindow (Window* window) { return window ? (NSWindow*)window->handle : nil; }

	static NSString* GetString (const std::string& text) {

		NSString* result = [[NSString alloc] initWithBytes:text.data () length:text.size () encoding:NSUTF8StringEncoding];
		return result ? DESKTOP_AUTORELEASE (result) : @"";

	}


	static NSString* GetKeyEquivalent (int keyCode) {

		unichar character = 0;

		// Use lowercase characters, since an uppercase key equivalent implies Shift
		if (keyCode >= 0x20 && keyCode < 0x7F) {

			character = (unichar)keyCode;

		} else if (keyCode >= KEY_F1 && keyCode <= KEY_F12) {

			character = (unichar)(NSF1FunctionKey + (keyCode - KEY_F1));

		} else if (keyCode >= KEY_F13 && keyCode <= KEY_F24) {

			character = (unichar)(NSF13FunctionKey + (keyCode - KEY_F13));

		} else {

			switch (keyCode) {

				case KEY_BACKSPACE: character = NSBackspaceCharacter; break;
				case KEY_TAB: character = NSTabCharacter; break;
				case KEY_RETURN: character = NSCarriageReturnCharacter; break;
				case KEY_ESCAPE: character = 0x1B; break;
				case KEY_DELETE: character = NSDeleteFunctionKey; break;
				case KEY_HOME: character = NSHomeFunctionKey; break;
				case KEY_END: character = NSEndFunctionKey; break;
				case KEY_PAGEUP: character = NSPageUpFunctionKey; break;
				case KEY_PAGEDOWN: character = NSPageDownFunctionKey; break;
				case KEY_INSERT: character = NSInsertFunctionKey; break;
				case KEY_LEFT: character = NSLeftArrowFunctionKey; break;
				case KEY_RIGHT: character = NSRightArrowFunctionKey; break;
				case KEY_UP: character = NSUpArrowFunctionKey; break;
				case KEY_DOWN: character = NSDownArrowFunctionKey; break;
				default: break;

			}

		}

		return character ? [NSString stringWithCharacters:&character length:1] : @"";

	}


	static NSUInteger GetModifierMask (int keyModifiers) {

		NSUInteger mask = 0;

		if (keyModifiers & MOD_GUI) mask |= NSEventModifierFlagCommand;
		if (keyModifiers & MOD_SHIFT) mask |= NSEventModifierFlagShift;
		if (keyModifiers & MOD_ALT) mask |= NSEventModifierFlagOption;
		if (keyModifiers & MOD_CTRL) mask |= NSEventModifierFlagControl;

		return mask;

	}


	static int GetKeyCode (NSString* keyEquivalent, NSUInteger* modifierMask) {

		if ([keyEquivalent length] != 1) return 0;

		unichar character = [keyEquivalent characterAtIndex:0];

		// An uppercase key equivalent implies Shift
		if (character >= 'A' && character <= 'Z') {

			*modifierMask |= NSEventModifierFlagShift;
			return character - 'A' + 'a';

		}

		if (character >= 0x20 && character < 0x7F) return character;
		if (character >= NSF1FunctionKey && character <= NSF12FunctionKey) return KEY_F1 + (character - NSF1FunctionKey);
		if (character >= NSF13FunctionKey && character <= NSF24FunctionKey) return KEY_F13 + (character - NSF13FunctionKey);

		switch (character) {

			case NSBackspaceCharacter: case 0x7F: return KEY_BACKSPACE;
			case NSTabCharacter: return KEY_TAB;
			case NSCarriageReturnCharacter: return KEY_RETURN;
			case 0x1B: return KEY_ESCAPE;
			case NSDeleteFunctionKey: return KEY_DELETE;
			case NSHomeFunctionKey: return KEY_HOME;
			case NSEndFunctionKey: return KEY_END;
			case NSPageUpFunctionKey: return KEY_PAGEUP;
			case NSPageDownFunctionKey: return KEY_PAGEDOWN;
			case NSInsertFunctionKey: return KEY_INSERT;
			case NSLeftArrowFunctionKey: return KEY_LEFT;
			case NSRightArrowFunctionKey: return KEY_RIGHT;
			case NSUpArrowFunctionKey: return KEY_UP;
			case NSDownArrowFunctionKey: return KEY_DOWN;
			default: return 0;

		}

	}


	static int GetKeyModifiers (NSUInteger modifierMask) {

		int keyModifiers = 0;

		if (modifierMask & NSEventModifierFlagCommand) keyModifiers |= MOD_GUI;
		if (modifierMask & NSEventModifierFlagShift) keyModifiers |= MOD_SHIFT;
		if (modifierMask & NSEventModifierFlagOption) keyModifiers |= MOD_ALT;
		if (modifierMask & NSEventModifierFlagControl) keyModifiers |= MOD_CTRL;

		return keyModifiers;

	}


	static NSImage* CreateImage (Image* imageBuffer) {

		if (!imageBuffer || !imageBuffer->Valid ()) {

			return nil;

		}

		int width = imageBuffer->width;
		int height = imageBuffer->height;
		int stride = imageBuffer->stride;


		NSBitmapImageRep* bitmap = DESKTOP_AUTORELEASE ([[NSBitmapImageRep alloc] initWithBitmapDataPlanes:NULL pixelsWide:width pixelsHigh:height bitsPerSample:8 samplesPerPixel:4 hasAlpha:YES isPlanar:NO colorSpaceName:NSDeviceRGBColorSpace bitmapFormat:NSBitmapFormatAlphaNonpremultiplied bytesPerRow:width * 4 bitsPerPixel:32]);

		if (!bitmap) return nil;

		const unsigned char* source = imageBuffer->data;
		unsigned char* dest = [bitmap bitmapData];

		for (int y = 0; y < height; y++) {

			memcpy (dest + y * width * 4, source + y * stride, width * 4);

		}

		NSImage* image = DESKTOP_AUTORELEASE ([[NSImage alloc] initWithSize:NSMakeSize (width, height)]);
		[image addRepresentation:bitmap];
		return image;

	}


	static NSMenuItem* FindItemWithSubmenu (NSMenu* menu, NSMenu* submenu) {

		for (NSMenuItem* item in [menu itemArray]) {

			if (![item hasSubmenu]) continue;
			if ([item submenu] == submenu) return item;

			NSMenuItem* found = FindItemWithSubmenu ([item submenu], submenu);
			if (found) return found;

		}

		return nil;

	}


	// AppKit only takes the Services menu the first time it is set, so the menu that
	// it fills moves to the item of each menu bar that holds it
	static bool MoveServicesMenu (NSMenuItem* destination) {

		NSMenu* services = [NSApp servicesMenu];
		if (!services || !destination) return false;

		NSMenu* parent = [services supermenu];
		NSInteger index = parent ? [parent indexOfItemWithSubmenu:services] : -1;
		if (index >= 0) [[parent itemAtIndex:index] setSubmenu:nil];

		[destination setSubmenu:services];
		return true;

	}


	static NSMenu* CreateMenu (NSString* title) {

		NSMenu* menu = DESKTOP_AUTORELEASE ([[NSMenu alloc] initWithTitle:title]);

		// Use the supplied enabled state rather than the responder chain.
		[menu setAutoenablesItems:NO];
		[menu setDelegate:GetMenuTarget ()];

		return menu;

	}


	static void AppendMenuItems (NSMenu* menu, const std::vector<MenuItem>& items, SEL action, NSInteger tagBase) {

		for (size_t i = 0; i < items.size (); i++) {

			const MenuItem& item = items[i];
			bool hasSubmenu = (item.flags & MENU_ITEM_SUBMENU) != 0;

			if ((item.flags & MENU_ITEM_SEPARATOR) && !hasSubmenu) {

				[menu addItem:[NSMenuItem separatorItem]];
				continue;

			}

			// macOS menus do not use mnemonics
			NSString* title = GetString (Menu::GetLabel (item.label, 0));
			NSString* keyEquivalent = (!hasSubmenu && item.keyCode != 0) ? GetKeyEquivalent (item.keyCode) : @"";

			NSMenuItem* menuItem = DESKTOP_AUTORELEASE ([[NSMenuItem alloc] initWithTitle:title action:(hasSubmenu ? NULL : action) keyEquivalent:keyEquivalent]);

			if ([keyEquivalent length] > 0) {

				[menuItem setKeyEquivalentModifierMask:GetModifierMask (item.keyModifiers)];

			}

			[menuItem setTag:(tagBase | item.id)];
			[menuItem setTarget:GetMenuTarget ()];
			[menuItem setEnabled:(item.flags & MENU_ITEM_ENABLED) != 0];
			[menuItem setState:(item.flags & MENU_ITEM_CHECKED) ? NSControlStateValueOn : NSControlStateValueOff];

			// Standard items of the menu bar keep doing what they do on the default one
			NSString* systemAction = (!item.action.empty () && action == @selector(selectApplicationMenuItem:)) ? GetString (item.action) : nil;

			if (systemAction) {

				[menuItem setRepresentedObject:systemAction];
				if (!hasSubmenu) [menuItem setAction:@selector(selectSystemMenuItem:)];

			}

			// The Services menu that macOS already fills moves here, other submenus are built
			bool movedServices = hasSubmenu && [systemAction isEqualToString:SERVICES_MENU] && MoveServicesMenu (menuItem);

			if (hasSubmenu && !movedServices) {

				NSMenu* submenu = CreateMenu (title);
				AppendMenuItems (submenu, item.submenu, action, tagBase);
				[menuItem setSubmenu:submenu];

				// The first Services menu is registered; macOS fills both of these
				if ([systemAction isEqualToString:SERVICES_MENU]) [NSApp setServicesMenu:submenu];
				else if ([systemAction isEqualToString:WINDOWS_MENU]) [NSApp setWindowsMenu:submenu];

			}

			[menu addItem:menuItem];

		}

	}


	int Menu::GetSupport () {
		if (!Menu::IsOwnerThread ()) return 0;

		return MENU_SUPPORT_POPUP | MENU_SUPPORT_APPLICATION;

	}


	int Menu::Popup (Window* window, const unsigned char* data, int length, int x, int y, bool atCursor) {
		if (!Menu::IsOwnerThread ()) return 0;

		@autoreleasepool {

			NSWindow* nsWindow = GetNSWindow (window);
			std::vector<MenuItem> items;

			if (!nsWindow || !Parse (data, length, &items)) return 0;

			NativeDesktopMenuTarget* target = GetMenuTarget ();
			NSMenu* menu = CreateMenu (@"");
			AppendMenuItems (menu, items, @selector(selectPopupMenuItem:), 0);

			NSView* view = [nsWindow contentView];
			NSPoint location;

			if (atCursor || !view) {

				// Keep the owning view so cursor-positioned menus inherit its appearance.
				location = [NSEvent mouseLocation];
				if (view) location = [view convertPoint:[nsWindow convertPointFromScreen:location] fromView:nil];

			} else {

				// Host coordinates start at the top-left of the content view.
				location = NSMakePoint (x, [view isFlipped] ? y : NSHeight ([view bounds]) - y);

			}

			target->popupSelection = 0;
			target->highlightedTag = 0;

			BOOL selected = [menu popUpMenuPositioningItem:nil atLocation:location inView:view];

			// The action is normally sent before tracking ends, the highlighted item is a fallback
			int result = (int)target->popupSelection;
			if (result == 0 && selected) result = (int)target->highlightedTag;

			return result;

		}

	}


	void Menu::Remove (Window* window) {
		if (!Menu::IsOwnerThread ()) return;
	}


	bool Menu::SetApplicationMenu (const unsigned char* data, int length) {
		if (!Menu::IsOwnerThread ()) return false;

		@autoreleasepool {

			if (!EnsureApplication ()) return false;

			if (!defaultApplicationMenuSaved) {

				defaultApplicationMenu = DESKTOP_RETAIN ([NSApp mainMenu]);
				defaultServicesItem = DESKTOP_RETAIN (FindItemWithSubmenu ([NSApp mainMenu], [NSApp servicesMenu]));
				defaultWindowsMenu = DESKTOP_RETAIN ([NSApp windowsMenu]);
				defaultApplicationMenuSaved = true;

			}

			if (!data || length <= 0) {

				// The menus that macOS fills go back with the menu bar that holds them
				[NSApp setMainMenu:defaultApplicationMenu];
				return true;

			}

			std::vector<MenuItem> items;

			if (!Parse (data, length, &items)) return false;

			NSMenu* menu = CreateMenu (@"MainMenu");
			AppendMenuItems (menu, items, @selector(selectApplicationMenuItem:), 0);

			[NSApp setMainMenu:menu];
			return true;

		}

	}


	static std::string GetLabelText (NSString* title) {

		// Titles do not have mnemonics, so ampersands are escaped
		const char* text = title ? [title UTF8String] : NULL;
		std::string label;

		for (; text && *text; text++) {

			label += *text;
			if (*text == '&') label += '&';

		}

		return label;

	}


	static bool ReadMenuItems (NSMenu* menu, std::vector<MenuItem>* items, int* nextID, int depth) {

		if (depth > 32) return false;

		for (NSMenuItem* menuItem in [menu itemArray]) {

			// The Window menu also lists the open windows, which macOS adds and removes
			if ([menuItem action] == @selector(makeKeyAndOrderFront:)) continue;
			if (*nextID > 0xEFFF) return false;

			MenuItem item = MenuItem ();
			item.id = (*nextID)++;

			NSMenu* submenu = [menuItem hasSubmenu] ? [menuItem submenu] : nil;
			id represented = [menuItem representedObject];
			NSString* systemAction = nil;

			// Items that a host's own target handles are the host's, and have no action here
			if ([menuItem action] == @selector(selectSystemMenuItem:) && [represented isKindOfClass:[NSString class]]) {

				systemAction = represented;

			} else if (submenu && submenu == [NSApp servicesMenu]) {

				systemAction = SERVICES_MENU;

			} else if (submenu && submenu == [NSApp windowsMenu]) {

				systemAction = WINDOWS_MENU;

			} else if (!submenu && [menuItem action] && ([menuItem target] == nil || [menuItem target] == NSApp)) {

				systemAction = NSStringFromSelector ([menuItem action]);

			}

			NSUInteger modifierMask = [menuItem keyEquivalentModifierMask];
			item.keyCode = submenu ? 0 : GetKeyCode ([menuItem keyEquivalent], &modifierMask);
			item.keyModifiers = item.keyCode ? GetKeyModifiers (modifierMask) : 0;

			item.flags = ([menuItem isEnabled] ? MENU_ITEM_ENABLED : 0)
				| ([menuItem state] == NSControlStateValueOn ? MENU_ITEM_CHECKED : 0)
				| ([menuItem isSeparatorItem] ? MENU_ITEM_SEPARATOR : 0)
				| (submenu ? MENU_ITEM_SUBMENU : 0);

			if (![menuItem isSeparatorItem]) item.label = GetLabelText ([menuItem title]);
			if (systemAction) item.action = [systemAction UTF8String];

			// macOS fills the Services menu itself
			if (submenu && ![systemAction isEqualToString:SERVICES_MENU] && !ReadMenuItems (submenu, &item.submenu, nextID, depth + 1)) {

				return false;

			}

			items->push_back (item);

		}

		return true;

	}


	bool Menu::GetApplicationMenu (std::vector<unsigned char>* data) {
		if (!Menu::IsOwnerThread () || !pthread_main_np () || !data || !NSApp) return false;

		@autoreleasepool {

			NSMenu* menu = [NSApp mainMenu];
			std::vector<MenuItem> items;
			int nextID = 1;

			if (!menu || !ReadMenuItems (menu, &items, &nextID, 0)) return false;

			Write (items, data);
			return true;

		}

	}


	bool Menu::SetWindowMenu (Window* window, const unsigned char* data, int length) {
		if (!Menu::IsOwnerThread ()) return false;

		// macOS windows do not have menu bars, use the application menu instead
		return false;

	}


	// Tray icons use NSStatusItem. When an icon has a menu, macOS shows it when the icon is
	// clicked, otherwise clicks are reported through the button action.

	static int nextTrayIconID = 0;


	static NSStatusItem* GetStatusItem (TrayIcon* trayIcon) {

		return trayIcon->platform ? DESKTOP_BRIDGE (NSStatusItem*, trayIcon->platform) : nil;

	}


	static NSImage* GetTrayImage (NSImage* image) {

		if (!image) return nil;

		CGFloat size = [[NSStatusBar systemStatusBar] thickness] - 4.0;
		if (size < 16.0) size = 16.0;

		NSImage* copy = DESKTOP_AUTORELEASE ([image copy]);
		NSSize imageSize = [copy size];

		if (imageSize.width > 0 && imageSize.height > 0) {

			[copy setSize:NSMakeSize (size * imageSize.width / imageSize.height, size)];

		}

		return copy;

	}


	bool TrayIcon::IsSupported () {
		if (!Menu::IsOwnerThread ()) return false;
		return EnsureApplication ();

	}


	TrayIcon::TrayIcon () {
		id = 0; platform = NULL;
		if (!Menu::IsOwnerThread ()) return;

		id = ++nextTrayIconID;
		platform = NULL;
		if (!EnsureApplication ()) return;

		@autoreleasepool {

			NSStatusItem* statusItem = [[NSStatusBar systemStatusBar] statusItemWithLength:NSVariableStatusItemLength];

			if (!statusItem) return;

			NSStatusBarButton* button = [statusItem button];
			[button setImage:GetTrayImage ([NSApp applicationIconImage])];
			[button setTarget:GetMenuTarget ()];
			[button setAction:@selector(clickTrayIcon:)];
			[button setTag:id];
			[button sendActionOn:(NSEventMaskLeftMouseUp | NSEventMaskRightMouseUp)];

			// The status bar does not keep the item alive
			platform = DESKTOP_BRIDGE_RETAIN (statusItem);

		}

	}


	TrayIcon::~TrayIcon () {

		Close ();

	}


	void TrayIcon::Close () {
		if (!Menu::IsOwnerThread ()) return;

		if (!platform) return;

		@autoreleasepool {

			[[NSStatusBar systemStatusBar] removeStatusItem:GetStatusItem (this)];

		}

		DESKTOP_BRIDGE_RELEASE (platform);
		platform = NULL;

	}


	int TrayIcon::PopupMenu (const unsigned char* data, int length) {
		if (!Menu::IsOwnerThread ()) return 0;

		if (!platform) return 0;

		@autoreleasepool {

			std::vector<MenuItem> items;

			if (!Menu::Parse (data, length, &items)) return 0;

			NativeDesktopMenuTarget* target = GetMenuTarget ();
			NSMenu* menu = CreateMenu (@"");
			AppendMenuItems (menu, items, @selector(selectPopupMenuItem:), 0);

			NSStatusBarButton* button = [GetStatusItem (this) button];
			NSPoint location = NSMakePoint (0, [button isFlipped] ? NSHeight ([button bounds]) + 4.0 : -4.0);

			target->popupSelection = 0;
			target->highlightedTag = 0;

			BOOL selected = [menu popUpMenuPositioningItem:nil atLocation:location inView:button];

			int result = (int)target->popupSelection;
			if (result == 0 && selected) result = (int)target->highlightedTag;

			return result;

		}

	}


	void TrayIcon::SetIcon (Image* imageBuffer) {
		if (!Menu::IsOwnerThread ()) return;

		if (!platform) return;

		@autoreleasepool {

			NSImage* image = CreateImage (imageBuffer);
			[[GetStatusItem (this) button] setImage:GetTrayImage (image ? image : [NSApp applicationIconImage])];

		}

	}


	bool TrayIcon::SetMenu (const unsigned char* data, int length) {
		if (!Menu::IsOwnerThread ()) return false;

		if (!platform) return false;

		@autoreleasepool {

			NSStatusItem* statusItem = GetStatusItem (this);
			std::vector<MenuItem> items;

			if (!data || length <= 0 || !Menu::Parse (data, length, &items)) {

				[statusItem setMenu:nil];
				return false;

			}

			NSMenu* menu = CreateMenu (@"");
			AppendMenuItems (menu, items, @selector(selectTrayIconMenuItem:), (NSInteger)id << 32);

			[statusItem setMenu:menu];
			return true;

		}

	}


	void TrayIcon::SetTooltip (const char* tooltip) {
		if (!Menu::IsOwnerThread ()) return;

		if (!platform) return;

		@autoreleasepool {

			[[GetStatusItem (this) button] setToolTip:GetString (tooltip ? std::string (tooltip) : std::string ())];

		}

	}


	void TrayIcon::Update () {
		if (!Menu::IsOwnerThread ()) return;
		@autoreleasepool {
			for (int i = 0; i < 64; ++i) {
				NSEvent* event = [NSApp nextEventMatchingMask:NSEventMaskAny untilDate:[NSDate distantPast] inMode:NSDefaultRunLoopMode dequeue:YES];
				if (!event) break;
				[NSApp sendEvent:event];
			}
		}
	}


	// Install a Dock menu handler only when the host delegate does not provide one.

	static NSMenu* dockMenu = nil;
	static bool dockMenuHandlerInstalled = false;


	static NSMenu* GetApplicationDockMenu (id self, SEL command, NSApplication* sender) {

		return dockMenu;

	}


	static bool InstallDockMenuHandler () {

		if (dockMenuHandlerInstalled) return true;

		id delegate = [NSApp delegate];
		if (!delegate) { delegate = GetMenuTarget (); [NSApp setDelegate:delegate]; }
		SEL selector = @selector(applicationDockMenu:);

		// Another delegate that already provides a Dock menu is left alone
		if (!delegate || [delegate respondsToSelector:selector]) return false;

		if (!class_addMethod ([delegate class], selector, (IMP)GetApplicationDockMenu, "@@:@")) return false;

		// NSApplication caches which methods its delegate implements, so set it again
		[NSApp setDelegate:nil];
		[NSApp setDelegate:delegate];

		dockMenuHandlerInstalled = true;
		return true;

	}


	void DockIcon::Bounce (bool critical) {
		if (!Menu::IsOwnerThread ()) return;

		if (NSApp) [NSApp requestUserAttention:(critical ? NSCriticalRequest : NSInformationalRequest)];

	}


	bool DockIcon::IsSupported () {
		if (!Menu::IsOwnerThread ()) return false;

		return NSApp != nil;

	}


	void DockIcon::SetIcon (Image* imageBuffer) {
		if (!Menu::IsOwnerThread ()) return;

		if (!NSApp) return;

		@autoreleasepool {

			// A nil image restores the icon from the application bundle
			[NSApp setApplicationIconImage:CreateImage (imageBuffer)];

		}

	}


	bool DockIcon::SetMenu (const unsigned char* data, int length) {
		if (!Menu::IsOwnerThread ()) return false;

		if (!NSApp) return false;

		@autoreleasepool {

			NSMenu* menu = nil;

			if (data && length > 0) {

				std::vector<MenuItem> items;

				if (!Menu::Parse (data, length, &items) || !InstallDockMenuHandler ()) return false;

				menu = CreateMenu (@"");
				AppendMenuItems (menu, items, @selector(selectDockMenuItem:), 0);

			}

			NSMenu* previous = dockMenu;
			dockMenu = DESKTOP_RETAIN (menu);
			DESKTOP_RELEASE (previous);

			return menu != nil;

		}

	}


}


#endif

#ifdef NATIVE_DESKTOP_MAC
namespace native_desktop {
bool TrayIcon::SetIconFile (const char* path) {
		if (!Menu::IsOwnerThread ()) return false;
	if (!platform || !path) return false;
	@autoreleasepool {
		NSImage* image = DESKTOP_AUTORELEASE ([[NSImage alloc] initWithContentsOfFile:GetString (path)]);
		if (!image) return false;
		[[GetStatusItem (this) button] setImage:GetTrayImage (image)]; return true;
	}
}
}
#endif
