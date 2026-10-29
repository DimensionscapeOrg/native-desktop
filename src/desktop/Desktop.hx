package desktop;

/**
	Native menus and trays are owned by one UI thread. Call poll from that thread's
	existing loop. This library neither starts a worker nor owns the application's loop.
**/
#if cpp
@:buildXml('<include name="${haxelib:native-desktop}/project/Build.xml"/>')
@:cppFileCode('#include <native_desktop/Desktop.h>\n#include <cstring>\nstatic bool AdvanceHaxeModal(void*) { return ::desktop::Desktop_obj::advanceModal(); }')
class Desktop {
	static var owner = sys.thread.Thread.current();
	static var applicationCommands:Array<MenuItem> = [];
	static var dockCommands:Array<MenuItem> = [];
	/** Optional host progress during native menu tracking; false dismisses the menu. */
	public static var onModalTick(default, set):() -> Bool;
	static function set_onModalTick(value:() -> Bool):() -> Bool {
		checkThread(); onModalTick = value;
		if (value == null) untyped __cpp__("native_desktop::Menu::SetModalCallback(NULL, NULL)");
		else untyped __cpp__("native_desktop::Menu::SetModalCallback(AdvanceHaxeModal, NULL)");
		return value;
	}
	static var modalError:Dynamic;
	static var modalErrorSet = false;
	@:noCompletion
	public static function advanceModal():Bool {
		if (modalErrorSet) return false;
		try { return onModalTick == null || onModalTick(); }
		catch (error:Dynamic) { modalError = error; modalErrorSet = true; return false; }
	}
	public static var menuSupport(get, never):Int;
	static function get_menuSupport():Int {
		checkThread(); return untyped __cpp__("native_desktop::Menu::GetSupport()");
	}
	public static function setApplicationMenu(menu:Menu):Bool {
		checkThread(); var next:Array<MenuItem> = [];
		var bytes = menu != null ? menu.encode(next) : haxe.io.Bytes.alloc(0);
		var data = bytes.getData(); var length = bytes.length;
		var success:Bool = untyped __cpp__("native_desktop::Menu::SetApplicationMenu((const unsigned char*){0}->GetBase(), {1})", data, length);
		if (success) applicationCommands = next;
		return success;
	}
	/**
		A copy of the installed application menu bar, or null where there is none. On
		macOS it holds standard items from the start, such as About, Hide and Quit, which
		keep doing what they do when the copy is changed and installed.
	**/
	public static function getApplicationMenu():Menu {
		checkThread();
		untyped __cpp__("std::vector<unsigned char> desktopMenu");
		if (!untyped __cpp__("native_desktop::Menu::GetApplicationMenu(&desktopMenu)")) return null;
		var length:Int = untyped __cpp__("(int)desktopMenu.size()");
		var bytes = haxe.io.Bytes.alloc(length);
		if (length > 0) untyped __cpp__("memcpy({0}->GetBase(), desktopMenu.data(), {1})", bytes.getData(), length);
		return Menu.decode(bytes);
	}
	public static function setDockMenu(menu:Menu):Bool {
		checkThread(); var next:Array<MenuItem> = [];
		var bytes = menu != null ? menu.encode(next) : haxe.io.Bytes.alloc(0);
		var data = bytes.getData(); var length = bytes.length;
		var success:Bool = untyped __cpp__("native_desktop::DockIcon::SetMenu((const unsigned char*){0}->GetBase(), {1})", data, length);
		if (success) dockCommands = next;
		return success;
	}
	@:allow(desktop)
	static function checkThread():Void {
		if (sys.thread.Thread.current() != owner) throw "Native desktop operations must run on their owning UI thread";
	}
	public static function poll():Void {
		checkThread();
		untyped __cpp__("native_desktop::TrayIcon::Update()");
		if (modalErrorSet) { var error = modalError; modalError = null; modalErrorSet = false; throw error; }
		// Copy each event out of native storage before invoking a callback. Callbacks may
		// replace menus, close icons or reenter host code without invalidating this event.
		untyped __cpp__("native_desktop::Event desktopEvent");
		var count = 0;
		while (count++ < 256 && untyped __cpp__("native_desktop::Menu::PollEvent(&desktopEvent)")) {
			var kind:Int = untyped __cpp__("desktopEvent.kind");
			var target:Int = untyped __cpp__("(int)desktopEvent.target");
			var code:Int = untyped __cpp__("desktopEvent.code");
			var value:Int = untyped __cpp__("desktopEvent.value");
			if (kind == 2) TrayIcon.dispatch(target, code, value);
			else if (kind == 1) {
				if (target == 0) Menu.select(applicationCommands, code); else NativeWindow.dispatch(target, code);
			} else if (kind == 3) Menu.select(dockCommands, code);
		}
	}
}
#else
class Desktop {
	public static var onModalTick:() -> Bool;
	public static var menuSupport(default, null):Int = 0;
	public static function setApplicationMenu(menu:Menu):Bool return false;
	public static function getApplicationMenu():Menu return null;
	public static function setDockMenu(menu:Menu):Bool return false;
	public static function poll():Void {}
}
#end
