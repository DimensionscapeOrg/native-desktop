import desktop.Desktop;
import desktop.Menu;
import desktop.MenuItem;
import desktop.TrayIcon;
import desktop.NativeWindow;

#if cpp
@:cppFileCode('#include <native_desktop/Desktop.h>\n#ifdef _WIN32\n#include <windows.h>\n#endif')
#end
class Smoke {
 static function main() {
  var selected = false;
  var menu = new Menu();
  menu.add(new MenuItem("Open", () -> selected = true));
  menu.addSeparator();
  var tray = new TrayIcon(null, "Native desktop smoke test");
  if (Sys.args().length > 0 && !tray.setIcon(Sys.args()[0])) throw "Native icon file could not be loaded";
  tray.setMenu(menu);
  Desktop.poll();
  var rejected = false; var completed = new sys.thread.Lock();
  sys.thread.Thread.create(() -> { try tray.close() catch (_:Dynamic) rejected = true; completed.release(); });
  if (!completed.wait(5) || !rejected) throw "A worker thread was allowed to own native UI operations";
  tray.close(); tray.close();
  if (tray.visible) throw "Closed icon is visible";
  var recursive = new Menu(); recursive.addSubmenu("Cycle", recursive);
  var cycleRejected = false;
  try { @:privateAccess recursive.encode([]); } catch (_:Dynamic) cycleRejected = true;
  if (!cycleRejected) throw "Recursive menu was accepted";
		var modalErrorReported = false;
		Desktop.onModalTick = () -> throw "modal callback fixture";
		var continued:Bool = untyped __cpp__("native_desktop::Menu::AdvanceModal()");
		if (continued) throw "A failed modal callback continued tracking";
		try Desktop.poll() catch (_:Dynamic) modalErrorReported = true;
		Desktop.onModalTick = null;
		if (!modalErrorReported) throw "A modal callback error was lost";
  #if windows
  var handle:cpp.RawPointer<cpp.Void> = untyped __cpp__('(void*)CreateWindowExW(0, L"STATIC", L"", WS_OVERLAPPEDWINDOW, 0, 0, 320, 200, NULL, NULL, GetModuleHandleW(NULL), NULL)');
  var window = new NativeWindow(cpp.Pointer.fromRaw(handle));
  if (!window.setMenu(menu)) throw "Window menu was not installed";
  untyped __cpp__('SendMessageW((HWND){0}, WM_COMMAND, MAKEWPARAM(0x1001, 0), 0)', handle);
  Desktop.poll(); if (!selected) throw "Native selection did not reach Haxe";
  var interactive:Bool = untyped __cpp__('([]() { USEROBJECTFLAGS flags = {}; DWORD size; return GetUserObjectInformationW(GetProcessWindowStation(), UOI_FLAGS, &flags, sizeof(flags), &size) && (flags.dwFlags & WSF_VISIBLE); })()');
  if (interactive) {
  var modalTicks = 0;
  Desktop.onModalTick = () -> ++modalTicks < 3;
  untyped __cpp__('ShowWindow((HWND){0}, SW_SHOWNOACTIVATE)', handle);
  window.popup(menu, 20, 20);
  Desktop.onModalTick = null;
  if (modalTicks < 3) throw "Host progress stalled while the menu was open";
  var callbackFailed = false;
  Desktop.onModalTick = () -> throw "modal callback fixture";
  window.popup(menu, 20, 20);
  try Desktop.poll() catch (_:Dynamic) callbackFailed = true;
  Desktop.onModalTick = null;
  if (!callbackFailed) throw "A modal callback error was lost";
  Sys.println("ok: native selection, modal host progress and callback error recovery");
  }
  window.close(); window.close();
  untyped __cpp__('DestroyWindow((HWND){0})', handle);
  #end
  Sys.println("ok: standalone Haxe bindings, thread ownership, menu validation and cleanup");
 }
}
