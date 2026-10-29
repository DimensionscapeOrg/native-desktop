# Native desktop

Native menus and tray icons for Windows, macOS and Linux. The C++ implementation
has no dependency on Haxe, Lime, SDL, a renderer or CrossByte. The Haxe bindings
compile it directly into an hxcpp executable. Other Haxe targets report unsupported.

Install the Haxe bindings:

```sh
haxelib install native-desktop 1.0.1
```

For development, clone this repository and register its directory:

```sh
haxelib dev native-desktop /path/to/native-desktop
```

```haxe
import desktop.Desktop;
import desktop.Menu;
import desktop.MenuItem;
import desktop.TrayIcon;

var menu = new Menu();
menu.add(new MenuItem("Open", openApplication));
menu.addSeparator();
menu.add(new MenuItem("Quit", quitApplication));
var icon = new TrayIcon(iconPath, "My application");
icon.setMenu(menu);
while (running) {
    advanceApplication();
    Desktop.poll();
    Sys.sleep(0.02);
}
icon.close();
```

## Ownership and host integration

Create, update, poll and close native objects on the same UI thread. On macOS
that must be the process main thread. The host owns its outer loop; polling is
bounded and nonblocking. No background thread or renderer is created.

Native menus track user input in an OS modal loop. A service can set
`Desktop.onModalTick` to its existing nonblocking update function so networking,
timers and shutdowns continue while a menu is open. Returning false cancels menu
tracking. Haxe callback errors cancel tracking and are reported by `Desktop.poll`.
Callbacks run on the owning thread. Close icons explicitly; garbage collection
does not own their OS lifetime.

`Menu` supports submenus, separators, enabled and checked states, keyboard
equivalents and `&` mnemonics (`&&` escapes an ampersand). Reinstall after editing
items. `NativeWindow` borrows an HWND, NSWindow pointer, or X11 Window and Display.
It owns menu state only, and never destroys the host window. `Desktop` also
provides application and Dock menus where supported. Query `menuSupport` for
popup (1), window (2) and application (4) capabilities.

On macOS the application menu bar starts with standard items, such as About, Hide,
Services, Quit and the Window menu. `Desktop.getApplicationMenu()` returns a copy
of the installed bar to add to. Its standard items carry an `action`, such as
`terminate:` for Quit, and keep doing it when the copy is installed; the
application is told of them as of its own items. macOS keeps filling the Services
and Window menus of the installed bar, and `setApplicationMenu(null)` restores the
original bar. Other systems have no application menu bar to copy, and return null.

Pass borrowed handles as `cpp.Pointer<cpp.Void>`; use `cpp.Pointer.fromRaw` to wrap
a raw pointer. The wrapper does not transfer ownership of the native handle.

Windows uses Shell_NotifyIcon and Win32 menus, including Explorer restart
recovery. macOS uses AppKit. Linux dynamically loads GTK 3 and Ayatana AppIndicator
(or legacy AppIndicator); desktop support and installed libraries determine tray
availability. Linux tray menus are exported to the desktop, which owns their
presentation and may not report icon clicks.

`setIcon` accepts an ICO file on Windows, an AppKit image file on macOS and an
absolute icon path on Linux. The C++ API additionally accepts borrowed RGBA pixels
and optional PNG bytes for AppIndicator. Encoding stays with the host.

Lime uses the same C++ implementation through its SDL adapter. Its public Menu,
MenuItem, TrayIcon and DockIcon APIs remain unchanged. The adapter resolves native
window handles, forwards events to SDL and supplies image data. Only one host
integration owns a linked library instance; do not install a second event sink
alongside Lime's integration.

## Tests

```sh
cmake -S . -B tests/bin/native -DBUILD_TESTING=ON
cmake --build tests/bin/native --config Debug
ctest --test-dir tests/bin/native -C Debug --output-on-failure
haxe tests/menu.hxml
haxe tests/smoke.hxml
```

The native tests check menu-data encoding and validation, thread ownership,
command delivery, modal host progress and tray cleanup. Windows tests also
exercise appearance, accessibility, boundary rendering and GDI resource cleanup.
macOS tests read a default menu bar, install a copy with a menu of the
application's own, choose Hide and restore the original bar. The Haxe tests check
encoding, decoding, menu validation, callback delivery, callback errors and
idempotent close, and on macOS read the installed menu bar back.

CI runs CMake/CTest and the Haxe bindings on Windows, macOS and Linux. Native
behavior is tested here. Hosts such as Lime keep only their own API and adapter
integration tests. No Lime or SDL installation is needed for this suite.
