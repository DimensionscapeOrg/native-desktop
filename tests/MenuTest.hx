import desktop.Menu;
import desktop.MenuItem;
import haxe.io.BytesInput;

class MenuTest {
	static function check(value:Bool, message:String):Void {
		if (!value) throw message;
	}
	static function main():Void {
		var calls = 0;
		var menu = new Menu();
		var item = menu.add(new MenuItem("&Open café", () -> calls++));
		item.keyCode = 111;
		item.keyModifiers = 192;
		item.checked = true;
		menu.addSeparator();
		var child = new Menu();
		child.add(new MenuItem("Recent"));
		menu.addSubmenu("More", child);
		var commands:Array<MenuItem> = [];
		var input = new BytesInput(@:privateAccess menu.encode(commands));
		input.bigEndian = false;
		check(input.readInt32() == 3, "Root item count");
		check(input.readInt32() == 1 && input.readInt32() == 3, "Command ID and checked/enabled flags");
		check(input.readInt32() == 111 && input.readInt32() == 192, "Keyboard equivalent");
		var length = input.readInt32();
		check(input.readString(length) == "&Open café", "UTF-8 label and mnemonic");
		check(commands.length == 4 && commands[0] == item, "Command snapshot includes nested items");
		@:privateAccess Menu.select(commands, 1);
		check(calls == 1, "Command delivered once");
		item.enabled = false;
		for (id in [0, 1, 2, 3, 5]) @:privateAccess Menu.select(commands, id);
		check(calls == 1, "Invalid, disabled, separator and submenu commands do not invoke callbacks");
		var recursive = new Menu();
		recursive.addSubmenu("Loop", recursive);
		var rejected = false;
		try { @:privateAccess recursive.encode([]); } catch (_:Dynamic) rejected = true;
		check(rejected, "Recursive menu rejected");
		var deep = new Menu();
		var tail = deep;
		for (_ in 0...34) { var next = new Menu(); tail.addSubmenu("Child", next); tail = next; }
		rejected = false;
		try { @:privateAccess deep.encode([]); } catch (_:Dynamic) rejected = true;
		check(rejected, "Excessive nesting rejected");
		Sys.println("ok: Haxe menu encoding, command dispatch and validation");
	}
}
