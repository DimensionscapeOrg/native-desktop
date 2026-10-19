package desktop;

/**
	A borrowed HWND, NSWindow*, or X11 Window (with its Display*). This adapter owns
	only its menu state. Call close before the host destroys the native window.
	x/y are the window's screen position, used for X11 popup coordinates.
**/
#if cpp
@:buildXml('<include name="${haxelib:native-desktop}/project/Build.xml"/>')
@:cppFileCode('#include <native_desktop/Desktop.h>')
class NativeWindow {
	static var windows = new Map<Int, NativeWindow>();
	static var nextID = 0;
	var native:cpp.RawPointer<cpp.Void>;
	var id:Int;
	var commands:Array<MenuItem> = [];
	public function new(handle:cpp.Pointer<cpp.Void>, ?display:cpp.Pointer<cpp.Void>, x:Int = 0, y:Int = 0) {
		Desktop.checkThread();
		if (handle == null) throw "A native window handle is required";
		var rawHandle = handle.raw;
		var rawDisplay:cpp.RawPointer<cpp.Void> = display == null ? null : display.raw;
		id = ++nextID;
		native = untyped __cpp__("(void*)(new native_desktop::Window())");
		untyped __cpp__("((native_desktop::Window*){0})->handle = {1}", native, rawHandle);
		untyped __cpp__("((native_desktop::Window*){0})->display = {1}", native, rawDisplay);
		untyped __cpp__("((native_desktop::Window*){0})->id = {1}", native, id);
		setPosition(x, y); windows.set(id, this);
	}
	public function setPosition(x:Int, y:Int):Void {
		Desktop.checkThread(); if (native == null) return;
		untyped __cpp__("((native_desktop::Window*){0})->x = {1}", native, x);
		untyped __cpp__("((native_desktop::Window*){0})->y = {1}", native, y);
	}
	public function setMenu(menu:Menu):Bool {
		Desktop.checkThread(); if (native == null) return false;
		var next:Array<MenuItem> = [];
		var bytes = menu != null ? menu.encode(next) : haxe.io.Bytes.alloc(0);
		var data = bytes.getData(); var length = bytes.length;
		var success:Bool = untyped __cpp__("native_desktop::Menu::SetWindowMenu((native_desktop::Window*){0}, (const unsigned char*){1}->GetBase(), {2})", native, data, length);
		if (success) commands = next;
		return success;
	}
	public function popup(menu:Menu, ?x:Int, ?y:Int):Void {
		Desktop.checkThread(); if (native == null || menu == null) return;
		var selected:Array<MenuItem> = []; var bytes = menu.encode(selected);
		var data = bytes.getData(); var length = bytes.length;
		var atCursor = x == null || y == null; var px = x == null ? 0 : x; var py = y == null ? 0 : y;
		var command:Int = untyped __cpp__("native_desktop::Menu::Popup((native_desktop::Window*){0}, (const unsigned char*){1}->GetBase(), {2}, {3}, {4}, {5})", native, data, length, px, py, atCursor);
		Menu.select(selected, command);
	}
	public function close():Void {
		Desktop.checkThread(); if (native == null) return;
		setMenu(null);
		untyped __cpp__("native_desktop::Menu::Remove((native_desktop::Window*){0})", native);
		untyped __cpp__("delete (native_desktop::Window*){0}", native);
		native = null; windows.remove(id); commands = [];
	}
	@:allow(desktop.Desktop)
	static function dispatch(id:Int, item:Int):Void {
		var window = windows.get(id); if (window != null) Menu.select(window.commands, item);
	}
}
#else
class NativeWindow {
	public function new(?handle:Dynamic, ?display:Dynamic, x:Int = 0, y:Int = 0) {}
	public function setPosition(x:Int, y:Int):Void {}
	public function setMenu(menu:Menu):Bool return false;
	public function popup(menu:Menu, ?x:Int, ?y:Int):Void {}
	public function close():Void {}
}
#end
