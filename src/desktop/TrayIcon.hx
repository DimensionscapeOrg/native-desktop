package desktop;

/** An owned native tray icon. Close explicitly on the owning UI thread. */
#if cpp
@:buildXml('<include name="${haxelib:native-desktop}/project/Build.xml"/>')
@:cppFileCode('#include <native_desktop/Desktop.h>')
class TrayIcon {
	static var icons = new Map<Int, TrayIcon>();
	var handle:cpp.RawPointer<cpp.Void>;
	var id:Int;
	var commands:Array<MenuItem> = [];
	var currentMenu:Menu;
	var platformMenu:Bool = false;
	public var onClick:() -> Void;
	public var onRightClick:() -> Void;
	public var visible(get, never):Bool;
	public static var isSupported(get, never):Bool;
	static function get_isSupported():Bool {
		Desktop.checkThread(); return untyped __cpp__("native_desktop::TrayIcon::IsSupported()");
	}
	function get_visible():Bool {
		Desktop.checkThread(); return handle != null && untyped __cpp__("((native_desktop::TrayIcon*){0})->IsVisible()", handle);
	}
	public function new(?iconPath:String, tooltip:String = "") {
		Desktop.checkThread();
		handle = untyped __cpp__("(void*)(new native_desktop::TrayIcon())");
		id = untyped __cpp__("((native_desktop::TrayIcon*){0})->GetID()", handle);
		if (!visible) { close(); return; }
		icons.set(id, this);
		if (iconPath != null) setIcon(iconPath);
		setTooltip(tooltip);
	}
	public function setIcon(path:String):Bool {
		Desktop.checkThread();
		if (path != null && sys.FileSystem.exists(path) && !sys.FileSystem.isDirectory(path)) path = sys.FileSystem.absolutePath(path);
		return handle != null && path != null && untyped __cpp__("((native_desktop::TrayIcon*){0})->SetIconFile({1}.utf8_str())", handle, path);
	}
	public function setTooltip(text:String):Void {
		Desktop.checkThread(); if (text == null) text = "";
		if (handle != null) untyped __cpp__("((native_desktop::TrayIcon*){0})->SetTooltip({1}.utf8_str())", handle, text);
	}
	public function setMenu(menu:Menu):Void {
		Desktop.checkThread();
		if (handle == null) return;
		var next:Array<MenuItem> = [];
		var bytes = menu != null ? menu.encode(next) : haxe.io.Bytes.alloc(0);
		var data = bytes.getData(); var length = bytes.length;
		platformMenu = untyped __cpp__("((native_desktop::TrayIcon*){0})->SetMenu((const unsigned char*){1}->GetBase(), {2})", handle, data, length);
		commands = next; currentMenu = menu;
	}
	public function close():Void {
		Desktop.checkThread();
		if (handle != null) {
			var old = handle; handle = null; icons.remove(id); commands = []; currentMenu = null;
			untyped __cpp__("delete (native_desktop::TrayIcon*){0}", old);
		}
	}
	@:allow(desktop.Desktop)
	static function dispatch(id:Int, type:Int, item:Int):Void {
		var icon = icons.get(id); if (icon == null || icon.handle == null) return;
		switch type {
			case 0: if (icon.onClick != null) icon.onClick();
			case 1: Menu.select(icon.commands, item);
			case 2:
				if (icon.onRightClick != null) icon.onRightClick();
				if (icon.handle != null && icon.currentMenu != null && !icon.platformMenu) {
					var selected:Array<MenuItem> = []; var bytes = icon.currentMenu.encode(selected);
					// The popup and dispatch must use the same immutable command snapshot.
					var data = bytes.getData(); var length = bytes.length;
					var result:Int = untyped __cpp__("((native_desktop::TrayIcon*){0})->PopupMenu((const unsigned char*){1}->GetBase(), {2})", icon.handle, data, length);
					if (icon.handle != null) Menu.select(selected, result);
				}
		}
	}
}
#else
class TrayIcon {
	public static var isSupported(default, null):Bool = false;
	public var visible(default, null):Bool = false;
	public var onClick:() -> Void;
	public var onRightClick:() -> Void;
	public function new(?iconPath:String, tooltip:String = "") {}
	public function setIcon(path:String):Bool return false;
	public function setTooltip(text:String):Void {}
	public function setMenu(menu:Menu):Void {}
	public function close():Void {}
}
#end
