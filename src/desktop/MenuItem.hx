package desktop;

/** A native menu command or submenu. A menu snapshots its items when installed. */
class MenuItem {
	public var label:String;
	public var enabled:Bool = true;
	public var checked:Bool = false;
	public var separator:Bool = false;
	public var keyCode:Int = 0;
	public var keyModifiers:Int = 0;
	public var submenu:Menu;
	/**
		What a standard item of the application menu bar does, as `terminate:` for Quit
		on macOS. Items from `Desktop.getApplicationMenu()` have their actions, and keep
		doing them when the menu is installed. Null for the application's own items.
	**/
	public var action:String;
	public var onSelect:() -> Void;
	public function new(label:String = "", ?onSelect:() -> Void) {
		this.label = label == null ? "" : label;
		this.onSelect = onSelect;
	}
}
