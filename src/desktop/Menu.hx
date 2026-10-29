package desktop;

import haxe.io.Bytes;
import haxe.io.BytesInput;
import haxe.io.BytesOutput;

/** Native desktop menus. Reinstall a menu after changing its items. */
class Menu {
	public var items(default, null):Array<MenuItem> = [];
	public function new() {}
	public function add(item:MenuItem):MenuItem {
		if (item == null) throw "A menu item is required";
		items.push(item); return item;
	}
	public function addSeparator():MenuItem {
		var item = new MenuItem(); item.separator = true; return add(item);
	}
	public function addSubmenu(label:String, menu:Menu):MenuItem {
		if (menu == null) throw "A submenu is required";
		var item = new MenuItem(label); item.submenu = menu; return add(item);
	}
	@:allow(desktop)
	function encode(commands:Array<MenuItem>):Bytes {
		var output = new BytesOutput(); output.bigEndian = false;
		write(output, commands, []); return output.getBytes();
	}
	function write(output:BytesOutput, commands:Array<MenuItem>, parents:Array<Menu>):Void {
		if (parents.length > 32 || parents.indexOf(this) >= 0) throw "Menus must be acyclic and at most 32 levels deep";
		var path = parents.copy(); path.push(this);
		output.writeInt32(items.length);
		for (item in items) {
			if (commands.length >= 0xEFFF) throw "Too many menu commands";
			commands.push(item);
			var label = Bytes.ofString(item.label == null ? "" : item.label);
			var action = item.action != null && item.action != "" ? Bytes.ofString(item.action) : null;
			output.writeInt32(commands.length);
			output.writeInt32((item.enabled ? 1 : 0) | (item.checked ? 2 : 0) | (item.separator ? 4 : 0) | (item.submenu != null ? 8 : 0)
				| (action != null ? 16 : 0));
			output.writeInt32(item.keyCode); output.writeInt32(item.keyModifiers);
			output.writeInt32(label.length); output.write(label);
			if (action != null) { output.writeInt32(action.length); output.write(action); }
			if (item.submenu != null) item.submenu.write(output, commands, path);
		}
	}
	@:allow(desktop)
	static function decode(bytes:Bytes):Menu {
		var input = new BytesInput(bytes); input.bigEndian = false;
		return read(input, 0);
	}
	static function read(input:BytesInput, depth:Int):Menu {
		if (depth > 32) throw "Menus must be at most 32 levels deep";
		var menu = new Menu();
		for (_ in 0...input.readInt32()) {
			input.readInt32();
			var flags = input.readInt32();
			var item = new MenuItem();
			item.keyCode = input.readInt32(); item.keyModifiers = input.readInt32();
			item.label = input.readString(input.readInt32());
			item.enabled = flags & 1 != 0; item.checked = flags & 2 != 0; item.separator = flags & 4 != 0;
			if (flags & 16 != 0) item.action = input.readString(input.readInt32());
			if (flags & 8 != 0) item.submenu = read(input, depth + 1);
			menu.add(item);
		}
		return menu;
	}
	@:allow(desktop)
	static function select(commands:Array<MenuItem>, id:Int):Void {
		if (id > 0 && id <= commands.length) {
			var item = commands[id - 1];
			if (item.enabled && !item.separator && item.submenu == null && item.onSelect != null) item.onSelect();
		}
	}
}
