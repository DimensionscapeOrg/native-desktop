#include <native_desktop/Desktop.h>
#include "MenuFixture.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <climits>
#include <cstring>
#include <iostream>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#include <native_desktop/WindowsAppearance.h>
#endif
using namespace native_desktop;
static int calls = 0;
static int modalTicks = 0;
static bool ModalTick (void*) { return ++modalTicks < 3; }
static void Receive (const Event& event, void*) { assert (event.kind == MENU_EVENT_SELECTION && event.target == 77 && event.code == 2); ++calls; }
int main () {
 // These checks remain enabled in Release builds too.
 if (!Menu::IsOwnerThread ()) return 1;
 std::vector<unsigned char> data;
 Integer (data, 1); Item (data, 1, MENU_ITEM_ENABLED | MENU_ITEM_SUBMENU, "&File");
 Integer (data, 1); Item (data, 2, MENU_ITEM_ENABLED | MENU_ITEM_CHECKED, "&Open");
 std::vector<MenuItem> items;
 assert (Menu::Parse (data.data (), (int)data.size (), &items));
 assert (items.size () == 1 && items[0].submenu.size () == 1 && items[0].submenu[0].id == 2);
 assert (Menu::GetLabel ("A&&B &Open", 0) == "A&B Open");
 assert (Menu::GetLabel ("A&&B &Open", '_') == "A&B _Open");
 for (int size = 0; size < (int)data.size (); ++size) assert (!Menu::Parse (data.data (), size, &items));
 auto extra = data; extra.push_back (0); assert (!Menu::Parse (extra.data (), (int)extra.size (), &items));
 auto huge = data; huge[0] = 255; huge[1] = 255; huge[2] = 255; huge[3] = 127;
 assert (!Menu::Parse (huge.data (), (int)huge.size (), &items));
 auto invalidID = data; for (int i = 4; i < 8; ++i) invalidID[i] = 255;
 assert (!Menu::Parse (invalidID.data (), (int)invalidID.size (), &items));
 std::vector<unsigned char> duplicate;
 Integer (duplicate, 2); Item (duplicate, 1, MENU_ITEM_ENABLED, "One"); Item (duplicate, 1, MENU_ITEM_ENABLED, "Two");
 assert (!Menu::Parse (duplicate.data (), (int)duplicate.size (), &items));
 std::vector<unsigned char> invalidFlags;
 Integer (invalidFlags, 1); Item (invalidFlags, 1, 16, "Unknown");
 assert (!Menu::Parse (invalidFlags.data (), (int)invalidFlags.size (), &items));
 // Failed decoding must not replace the caller's previously installed menu data.
 assert (items.size () == 1 && items[0].submenu[0].id == 2);
 std::vector<unsigned char> deep;
 for (int i = 1; i <= 34; ++i) { Integer (deep, 1); Item (deep, i, MENU_ITEM_SUBMENU, "Child"); }
 Integer (deep, 0);
 assert (!Menu::Parse (deep.data (), (int)deep.size (), &items));
 bool workerAllowed = true;
 std::thread worker ([&] { workerAllowed = Menu::IsOwnerThread (); }); worker.join ();
 assert (!workerAllowed);
 Event event;
 Menu::PushSelection (77, 2); assert (Menu::PollEvent (&event) && event.target == 77 && event.code == 2);
 assert (!Menu::PollEvent (&event));
 Menu::SetEventSink (Receive, NULL); Menu::PushSelection (77, 2); assert (calls == 1 && !Menu::PollEvent (&event));
 Menu::SetEventSink (NULL, NULL);
#ifdef _WIN32
 HINSTANCE instance = GetModuleHandleW (NULL);
 WNDCLASSW type = {}; type.lpfnWndProc = DefWindowProcW; type.hInstance = instance; type.lpszClassName = L"NativeDesktopUnitTest";
 assert (RegisterClassW (&type));
 HWND handle = CreateWindowExW (0, type.lpszClassName, L"", WS_OVERLAPPEDWINDOW, 0, 0, 320, 200, NULL, NULL, instance, NULL);
 assert (handle);
 Window window; window.handle = handle; window.id = 77;
 assert (Menu::SetWindowMenu (&window, data.data (), (int)data.size ()));
 Window otherOwner = window; otherOwner.id = 88;
 assert (!Menu::SetWindowMenu (&otherOwner, data.data (), (int)data.size ()));
 Menu::Remove (&otherOwner);
 assert (GetMenu (handle));
 bool workerChangedMenu = true;
 std::thread badOwner ([&] { workerChangedMenu = Menu::SetWindowMenu (&window, NULL, 0); }); badOwner.join ();
 assert (!workerChangedMenu && GetMenu (handle));
 SendMessageW (handle, WM_COMMAND, MAKEWPARAM (0x1000 + 2, 0), 0);
 assert (Menu::PollEvent (&event) && event.kind == MENU_EVENT_SELECTION && event.target == 77 && event.code == 2);
 assert (!Menu::PollEvent (&event));
 SetWindowsMenuAppearance (handle, 2); SetWindowsMenuAppearance (handle, 1);
 USEROBJECTFLAGS station = {}; DWORD stationSize = 0;
 if (GetUserObjectInformationW (GetProcessWindowStation (), UOI_FLAGS, &station, sizeof(station), &stationSize) && (station.dwFlags & WSF_VISIBLE)) {
  Menu::SetModalCallback (ModalTick, NULL);
  ShowWindow (handle, SW_SHOWNOACTIVATE);
  Menu::Popup (&window, data.data (), (int)data.size (), 20, 20, false);
  ShowWindow (handle, SW_HIDE);
  Menu::SetModalCallback (NULL, NULL);
  assert (modalTicks >= 3);
 } else std::cout << "No interactive window station; modal tracking was not tested\n";
 Menu::Remove (&window); Menu::Remove (&window);
 assert (!GetMenu (handle));
 assert (!GetPropW (handle, L"NativeDesktop.MenuAppearance"));
 assert (!GetPropW (handle, L"NativeDesktop.DarkMenus"));
 SendMessageW (handle, WM_COMMAND, MAKEWPARAM (0x1000 + 2, 0), 0);
 assert (!Menu::PollEvent (&event));
 DestroyWindow (handle); UnregisterClassW (type.lpszClassName, instance);
 for (int i = 0; i < 20; ++i) { TrayIcon icon; icon.SetTooltip ("Native desktop test"); icon.Close (); icon.Close (); }
 assert (!FindWindowW (L"NativeDesktopTrayIconWindow", NULL));
#endif
 std::cout << "Native menu decoding, ownership, event delivery and lifecycle passed\n";
}
