#include <native_desktop/Desktop.h>
#include "MenuFixture.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <thread>
#ifndef _WIN32
#include <unistd.h>
#endif
#ifdef _WIN32
#include <windows.h>
#include <native_desktop/WindowsAppearance.h>
#include <shellapi.h>
#endif
using namespace native_desktop;
static int calls = 0;
static int modalTicks = 0;
static bool ModalTick (void*) { return ++modalTicks < 3; }
static void Receive (const Event& event, void*) { assert (event.kind == MENU_EVENT_SELECTION && event.target == 77 && event.code == 2); ++calls; }
static int opens = 0;
// Replaces the File menu (item 1) of window 77 with one item
static bool Open (const Event& event, std::vector<unsigned char>* items, void*) {
 assert (event.kind == MENU_EVENT_OPEN && event.target == 77 && event.code == 1 && event.value == MENU_SOURCE_WINDOW);
 ++opens;
 Integer (*items, 1); Item (*items, 3, MENU_ITEM_ENABLED, "Replaced");
 return true;
}
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
 Integer (invalidFlags, 1); Item (invalidFlags, 1, 32, "Unknown");
 assert (!Menu::Parse (invalidFlags.data (), (int)invalidFlags.size (), &items));
 // The action flag needs an action after the label.
 std::vector<unsigned char> noAction;
 Integer (noAction, 1); Item (noAction, 1, MENU_ITEM_ENABLED | MENU_ITEM_ACTION, "Quit"); Integer (noAction, 0);
 assert (!Menu::Parse (noAction.data (), (int)noAction.size (), &items));
 // Failed decoding must not replace the caller's previously installed menu data.
 assert (items.size () == 1 && items[0].submenu[0].id == 2);
 // Standard items keep their actions through Write and Parse; others have none.
 MenuItem quit = MenuItem (); quit.id = 2; quit.flags = MENU_ITEM_ENABLED; quit.keyCode = 'q'; quit.keyModifiers = MOD_GUI;
 quit.label = "Quit"; quit.action = "terminate:";
 MenuItem services = MenuItem (); services.id = 3; services.flags = MENU_ITEM_ENABLED | MENU_ITEM_SUBMENU; services.label = "Services";
 services.action = "services";
 MenuItem application = MenuItem (); application.id = 1; application.flags = MENU_ITEM_ENABLED | MENU_ITEM_SUBMENU; application.label = "Q&&A";
 application.submenu.push_back (quit); application.submenu.push_back (services);
 std::vector<unsigned char> written (3, 0);
 Menu::Write (std::vector<MenuItem> (1, application), &written);
 std::vector<MenuItem> read;
 assert (Menu::Parse (written.data (), (int)written.size (), &read));
 assert (read.size () == 1 && read[0].label == "Q&&A" && read[0].action.empty () && read[0].flags == application.flags);
 assert (read[0].submenu.size () == 2 && read[0].submenu[0].action == "terminate:" && read[0].submenu[0].flags == (MENU_ITEM_ENABLED | MENU_ITEM_ACTION));
 assert (read[0].submenu[0].keyCode == 'q' && read[0].submenu[0].keyModifiers == MOD_GUI && read[0].submenu[0].label == "Quit");
 assert (read[0].submenu[1].action == "services" && read[0].submenu[1].submenu.empty ());
 for (int size = 0; size < (int)written.size (); ++size) assert (!Menu::Parse (written.data (), size, &read));
#ifndef __APPLE__
 assert (!Menu::GetApplicationMenu (&written));
#endif
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
 // Without an open handler, open events are queued and the menu stays as it is
 std::vector<unsigned char> replacement;
 assert (!Menu::PushOpen (77, 1, MENU_SOURCE_WINDOW, &replacement) && replacement.empty ());
 assert (Menu::PollEvent (&event) && event.kind == MENU_EVENT_OPEN && event.target == 77 && event.code == 1 && event.value == MENU_SOURCE_WINDOW);
 Menu::SetOpenHandler (Open, NULL);
 assert (Menu::PushOpen (77, 1, MENU_SOURCE_WINDOW, &replacement) && opens == 1 && !Menu::PollEvent (&event));
 assert (Menu::Parse (replacement.data (), (int)replacement.size (), &items) && items.size () == 1 && items[0].label == "Replaced");
 // Tray icon mouse events carry the button in value and the screen position in x and y
 Menu::PushTrayIconMouseEvent (5, TRAY_ICON_DOUBLE_CLICK, MOUSE_BUTTON_RIGHT, -20, 1080);
 assert (Menu::PollEvent (&event) && event.kind == MENU_EVENT_TRAY_ICON && event.target == 5 && event.code == TRAY_ICON_DOUBLE_CLICK);
 assert (event.value == MOUSE_BUTTON_RIGHT && event.x == -20 && event.y == 1080);
 Menu::PushTrayIconEvent (5, TRAY_ICON_MENU_SELECT, 9);
 assert (Menu::PollEvent (&event) && event.code == TRAY_ICON_MENU_SELECT && event.value == 9 && event.x == 0 && event.y == 0);
 Menu::PushClose (5, MENU_TRACKING_ENDED, MENU_SOURCE_TRAY_ICON);
 assert (Menu::PollEvent (&event) && event.kind == MENU_EVENT_CLOSE && event.target == 5 && event.code == MENU_TRACKING_ENDED && event.value == MENU_SOURCE_TRAY_ICON);
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
 // A menu of the bar asks the open handler as it opens, and is built again from its answer
 HMENU fileMenu = GetSubMenu (GetMenu (handle), 0);
 SendMessageW (handle, WM_INITMENUPOPUP, (WPARAM)fileMenu, MAKELPARAM (0, FALSE));
 wchar_t label[32] = {};
 assert (opens == 2 && GetMenuItemCount (fileMenu) == 1 && GetMenuStringW (fileMenu, 0, label, 32, MF_BYPOSITION) && wcscmp (label, L"Replaced") == 0);
 assert (GetMenuItemID (fileMenu, 0) == 0x1000 + 3);
 SendMessageW (handle, WM_UNINITMENUPOPUP, (WPARAM)fileMenu, 0);
 assert (Menu::PollEvent (&event) && event.kind == MENU_EVENT_CLOSE && event.target == 77 && event.code == 1 && event.value == MENU_SOURCE_WINDOW);
 SendMessageW (handle, WM_EXITMENULOOP, FALSE, 0);
 assert (Menu::PollEvent (&event) && event.kind == MENU_EVENT_CLOSE && event.code == MENU_TRACKING_ENDED);
 assert (!Menu::PollEvent (&event));
 Menu::SetOpenHandler (NULL, NULL);
 assert (Menu::SetWindowMenu (&window, data.data (), (int)data.size ()));
 SetWindowsMenuAppearance (handle, 2); SetWindowsMenuAppearance (handle, 1);
 USEROBJECTFLAGS station = {}; DWORD stationSize = 0;
 if (GetUserObjectInformationW (GetProcessWindowStation (), UOI_FLAGS, &station, sizeof(station), &stationSize) && (station.dwFlags & WSF_VISIBLE)) {
  Menu::SetModalCallback (ModalTick, NULL);
  ShowWindow (handle, SW_SHOWNOACTIVATE);
  Menu::Popup (&window, data.data (), (int)data.size (), 20, 20, false);
  ShowWindow (handle, SW_HIDE);
  // Without an open handler, the popup's open and close events wait in the queue
  bool poppedUp = false, tracked = false;
  while (Menu::PollEvent (&event)) {
   assert ((event.kind == MENU_EVENT_OPEN || event.kind == MENU_EVENT_CLOSE) && event.target == 77 && event.value == MENU_SOURCE_WINDOW);
   if (event.kind == MENU_EVENT_OPEN && event.code == 0) poppedUp = true;
   if (event.kind == MENU_EVENT_CLOSE && event.code == MENU_TRACKING_ENDED) tracked = true;
  }
  assert (poppedUp && tracked);
  Menu::SetModalCallback (NULL, NULL);
  assert (modalTicks >= 3);
 } else std::cout << "No interactive window station; modal tracking was not tested\n";
 Menu::Remove (&window); Menu::Remove (&window);
 assert (!GetMenu (handle));
 assert (!GetPropW (handle, L"NativeDesktop.MenuAppearance"));
 assert (!GetPropW (handle, L"NativeDesktop.DarkMenus"));
 SendMessageW (handle, WM_COMMAND, MAKEWPARAM (0x1000 + 2, 0), 0);
 assert (!Menu::PollEvent (&event));
 {
  // Stacking: each window goes just above or below the other, or to the back
  HWND second = CreateWindowExW (0, type.lpszClassName, L"", WS_OVERLAPPEDWINDOW, 40, 40, 320, 200, NULL, NULL, instance, NULL);
  assert (second);
  ShowWindow (handle, SW_SHOWNOACTIVATE); ShowWindow (second, SW_SHOWNOACTIVATE);
  Window first; first.handle = handle; first.id = 77;
  Window other; other.handle = second; other.id = 78;
  assert (WindowControl::OrderBelow (&first, &other) && GetWindow (second, GW_HWNDNEXT) == handle);
  assert (WindowControl::OrderAbove (&first, &other) && GetWindow (handle, GW_HWNDNEXT) == second);
  // Hidden system windows may stay below the back, so the other window is checked to be above it
  assert (WindowControl::OrderToBack (&first));
  bool secondAbove = false;
  for (HWND next = GetWindow (second, GW_HWNDNEXT); next; next = GetWindow (next, GW_HWNDNEXT)) if (next == handle) secondAbove = true;
  assert (secondAbove);
  assert (!WindowControl::OrderAbove (&first, &first));
  // Without the button held there is nothing to follow
  if (!(GetAsyncKeyState (VK_LBUTTON) & 0x8000)) assert (!WindowControl::StartMove (&first) && !WindowControl::StartResize (&first, WINDOW_EDGE_RIGHT));
  assert (!WindowControl::StartResize (&first, 0));
  ShowWindow (handle, SW_HIDE);
  DestroyWindow (second);
 }
 DestroyWindow (handle); UnregisterClassW (type.lpszClassName, instance);
 {
  // The notification icon's messages carry the pointer's screen position in wParam
  TrayIcon icon; icon.SetTooltip ("Native desktop mouse test");
  if (icon.IsVisible ()) {
   HWND tray = NULL; DWORD process = 0;
   while ((tray = FindWindowExW (NULL, tray, L"NativeDesktopTrayIconWindow", NULL)) != NULL) {
    GetWindowThreadProcessId (tray, &process);
    if (process == GetCurrentProcessId ()) break;
   }
   assert (tray);
   SendMessageW (tray, WM_APP + 1, MAKEWPARAM (-30, 40), MAKELPARAM (WM_LBUTTONDBLCLK, icon.GetID ()));
   assert (Menu::PollEvent (&event) && event.kind == MENU_EVENT_TRAY_ICON && event.target == (uint32_t)icon.GetID () && event.code == TRAY_ICON_DOUBLE_CLICK);
   assert (event.value == MOUSE_BUTTON_LEFT && event.x == -30 && event.y == 40);
   SendMessageW (tray, WM_APP + 1, MAKEWPARAM (5, 6), MAKELPARAM (NIN_SELECT, icon.GetID ()));
   assert (Menu::PollEvent (&event) && event.code == TRAY_ICON_CLICK && event.x == 5 && event.y == 6 && !Menu::PollEvent (&event));
  } else std::cout << "No notification area; tray icon mouse events were not tested" << std::endl;
  icon.Close ();
 }
 for (int i = 0; i < 20; ++i) { TrayIcon icon; icon.SetTooltip ("Native desktop test"); icon.Close (); icon.Close (); }
 // Other applications may use this library too. Only this process owns these icons.
 HWND tray = NULL;
 while ((tray = FindWindowExW (NULL, tray, L"NativeDesktopTrayIconWindow", NULL)) != NULL) {
  DWORD process = 0; GetWindowThreadProcessId (tray, &process);
  assert (process != GetCurrentProcessId ());
 }
#endif
 // Writing the clipboard replaces what the user copied, so it is only tested where asked
 const char* testClipboard = getenv ("NATIVE_DESKTOP_TEST_CLIPBOARD");
 if (testClipboard && testClipboard[0] == '1' && Clipboard::IsSupported ()) {
  std::vector<ClipboardItem> clip (5);
  clip[0].format = CLIPBOARD_TEXT; clip[0].text = "Caf\xc3\xa9 text";
  clip[1].format = CLIPBOARD_HTML; clip[1].text = "<b>bold</b> caf\xc3\xa9";
  clip[2].format = CLIPBOARD_RICH_TEXT; clip[2].text = "{\\rtf1\\ansi bold}";
  clip[3].format = CLIPBOARD_FILE_LIST;
  #ifdef _WIN32
  clip[3].files.push_back ("C:\\Windows\\notepad.exe"); clip[3].files.push_back ("C:\\Windows\\caf\xc3\xa9.txt");
  #else
  clip[3].files.push_back ("/tmp/one.txt"); clip[3].files.push_back ("/tmp/caf\xc3\xa9.txt");
  #endif
  clip[4].format = CLIPBOARD_IMAGE; clip[4].width = 2; clip[4].height = 2;
  unsigned char rgba[] = { 255, 0, 0, 255, 0, 255, 0, 128, 0, 0, 255, 255, 255, 255, 255, 0 };
  clip[4].pixels.assign (rgba, rgba + 16);
  assert (Clipboard::SetItems (clip));
  std::string text; std::vector<std::string> files; std::vector<unsigned char> pixels; int width = 0, height = 0;
  for (int i = 0; i < 10; ++i) Clipboard::Update ();
  assert (Clipboard::HasFormat (CLIPBOARD_TEXT) && Clipboard::GetText (CLIPBOARD_TEXT, &text) && text == clip[0].text);
  assert (Clipboard::HasFormat (CLIPBOARD_HTML) && Clipboard::GetText (CLIPBOARD_HTML, &text) && text == clip[1].text);
  assert (Clipboard::HasFormat (CLIPBOARD_RICH_TEXT) && Clipboard::GetText (CLIPBOARD_RICH_TEXT, &text) && text == clip[2].text);
  assert (Clipboard::HasFormat (CLIPBOARD_FILE_LIST) && Clipboard::GetFiles (&files) && files == clip[3].files);
  assert (Clipboard::HasFormat (CLIPBOARD_IMAGE) && Clipboard::GetImage (&pixels, &width, &height) && width == 2 && height == 2);
  // The pixel that is fully transparent may lose its color on the way
  assert (memcmp (&pixels[0], rgba, 12) == 0 && pixels[15] == 0);
  std::vector<ClipboardItem> url (1); url[0].format = CLIPBOARD_URL; url[0].text = "https://example.com/caf%C3%A9";
  assert (Clipboard::SetItems (url) && Clipboard::GetText (CLIPBOARD_URL, &text) && text == url[0].text && !Clipboard::HasFormat (CLIPBOARD_HTML));
  std::cout << "Clipboard formats passed\n";
 }
 // A file sent to the trash stays there for the user, so this only runs where asked
 const char* testTrash = getenv ("NATIVE_DESKTOP_TEST_TRASH");
 if (testTrash && testTrash[0] == '1' && Trash::IsSupported ()) {
  #ifdef _WIN32
  char folder[MAX_PATH]; GetTempPathA (MAX_PATH, folder);
  std::string path = std::string (folder) + "native-desktop trash test caf\xc3\xa9.txt";
  std::wstring widePath (path.size () + 1, L'\0');
  widePath.resize (MultiByteToWideChar (CP_UTF8, 0, path.c_str (), -1, &widePath[0], (int)widePath.size ()) - 1);
  FILE* file = _wfopen (widePath.c_str (), L"w");
  #else
  std::string path = "/tmp/native-desktop trash test caf\xc3\xa9.txt";
  FILE* file = fopen (path.c_str (), "w");
  #endif
  assert (file); fputs ("trash", file); fclose (file);
  assert (Trash::MoveToTrash (path.c_str ()));
  #ifdef _WIN32
  assert (GetFileAttributesW (widePath.c_str ()) == INVALID_FILE_ATTRIBUTES);
  #else
  assert (access (path.c_str (), F_OK) != 0);
  #endif
  assert (!Trash::MoveToTrash (path.c_str ()) && !Trash::MoveToTrash ("") && !Trash::MoveToTrash (NULL));
  std::cout << "Moving to the trash passed\n";
 }
 std::cout << "Native menu decoding, ownership, event delivery and lifecycle passed\n";
}
