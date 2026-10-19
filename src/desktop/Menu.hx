package desktop;

import haxe.io.Bytes;
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
			output.writeInt32(commands.length);
			output.writeInt32((item.enabled ? 1 : 0) | (item.checked ? 2 : 0) | (item.separator ? 4 : 0) | (item.submenu != null ? 8 : 0));
			output.writeInt32(item.keyCode); output.writeInt32(item.keyModifiers);
			output.writeInt32(label.length); output.write(label);
			if (item.submenu != null) item.submenu.write(output, commands, path);
		}
	}
	@:allow(desktop)
	static function select(commands:Array<MenuItem>, id:Int):Void {
		if (id > 0 && id <= commands.length) {
			var item = commands[id - 1];
			if (item.enabled && !item.separator && item.submenu == null && item.onSelect != null) item.onSelect();
		}
	}
}
