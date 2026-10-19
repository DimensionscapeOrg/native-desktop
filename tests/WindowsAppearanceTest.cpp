#include <native_desktop/Desktop.h>
#include <native_desktop/WindowsAppearance.h>
#include "MenuFixture.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <windows.h>
#include <oleacc.h>
#include <iostream>
using namespace native_desktop;
static int InspectMenuAppearance (HWND window, int operation) {
 HMENU bar = window ? GetMenu (window) : NULL;
 HMENU menu = bar ? GetSubMenu (bar, 0) : NULL;
 if (!menu) return -1;
 MENUITEMINFOW info = {}; info.cbSize = sizeof (info);
 info.fMask = MIIM_FTYPE | MIIM_DATA | MIIM_STATE;
 if (!GetMenuItemInfoW (menu, 0, TRUE, &info)) return -2;
 if (operation == 0) return (info.fType & MFT_OWNERDRAW) != 0;
 if (operation == 1) {
  MSAAMENUINFO* accessible = (MSAAMENUINFO*)info.dwItemData;
  return accessible && accessible->dwMSAASignature == MSAA_MENU_SIG && accessible->cchWText == 4 && wcscmp (accessible->pszWText, L"Open") == 0;
 }
 if (operation == 2) {
  LRESULT result = SendMessageW (window, WM_MENUCHAR, MAKEWPARAM (L'o', MF_POPUP), (LPARAM)menu);
  return HIWORD (result) == MNC_EXECUTE && LOWORD (result) == 0;
 }
 if (operation == 3) {
  MEASUREITEMSTRUCT measure = {}; measure.CtlType = ODT_MENU; measure.itemData = info.dwItemData;
  return SendMessageW (window, WM_MEASUREITEM, 0, (LPARAM)&measure) && measure.itemWidth > 0 && measure.itemHeight > 0;
 }
 if (operation == 4) {
  HDC dc = CreateCompatibleDC (NULL); HBITMAP bitmap = CreateBitmap (200, 40, 1, 32, NULL);
  HGDIOBJ old = SelectObject (dc, bitmap);
  DRAWITEMSTRUCT draw = {}; draw.CtlType = ODT_MENU; draw.itemData = info.dwItemData;
  draw.hDC = dc; draw.hwndItem = (HWND)menu; SetRect (&draw.rcItem, 0, 0, 200, 40);
  LRESULT handled = SendMessageW (window, WM_DRAWITEM, 0, (LPARAM)&draw);
  COLORREF pixel = GetPixel (dc, 1, 1);
  SelectObject (dc, old); DeleteObject (bitmap); DeleteDC (dc);
  return handled && pixel == RGB (32, 32, 32);
 }
 if (operation == 5) return (info.fState & MFS_CHECKED) != 0;
 if (operation == 6) {
  SendMessageW (window, WM_THEMECHANGED, 0, 0);
  MENUITEMINFOW after = {}; after.cbSize = sizeof (after); after.fMask = MIIM_FTYPE;
  return GetMenuItemInfoW (menu, 0, TRUE, &after) && (after.fType & MFT_OWNERDRAW) != 0;
 }
 if (operation == 7) {
  // Exercise the actual non-client paint path, including the strip outside rcBar.
  // Do not activate the test window or change the desktop theme.
  ShowWindow (window, SW_SHOWNOACTIVATE);
  RedrawWindow (window, NULL, NULL, RDW_INVALIDATE | RDW_FRAME | RDW_UPDATENOW);
  RECT frame, client; POINT origin = {0, 0};
  GetWindowRect (window, &frame); GetClientRect (window, &client); ClientToScreen (window, &origin);
  HDC dc = GetWindowDC (window);
  COLORREF pixel = RGB (32, 32, 32);
  int positions[] = {8, (client.right - client.left) / 2, client.right - client.left - 8};
  // Cover both the edge below an item and the unused menu-bar surface.
  for (int x : positions) {
   COLORREF sample = GetPixel (dc, origin.x - frame.left + x, origin.y - frame.top - 1);
   if (sample != RGB (32, 32, 32)) { pixel = sample; break; }
  }
  ReleaseDC (window, dc);
  ShowWindow (window, SW_HIDE);
  return (int)pixel;
 }
 return -3;
}

int main () {
 HINSTANCE instance = GetModuleHandleW (NULL);
 WNDCLASSW type = {}; type.lpfnWndProc = DefWindowProcW; type.hInstance = instance;
 type.lpszClassName = L"NativeDesktopAppearanceTest";
 assert (RegisterClassW (&type));
 HWND handle = CreateWindowExW (0, type.lpszClassName, L"", WS_OVERLAPPEDWINDOW, 0, 0, 320, 200, NULL, NULL, instance, NULL);
 assert (handle);
 Window window; window.handle = handle; window.id = 1;
 std::vector<unsigned char> data;
 Integer (data, 1); Item (data, 1, MENU_ITEM_ENABLED | MENU_ITEM_SUBMENU, "&File");
 Integer (data, 3);
 Item (data, 2, MENU_ITEM_ENABLED | MENU_ITEM_CHECKED, "&Open");
 Item (data, 3, MENU_ITEM_SEPARATOR, "");
 Item (data, 4, 0, "Disabled");
 assert (Menu::SetWindowMenu (&window, data.data (), (int)data.size ()));
 SetWindowsMenuAppearance (handle, 1);
 assert (InspectMenuAppearance (handle, 0) == 0);
 SetWindowsMenuAppearance (handle, 2);
 HIGHCONTRASTW contrast = {}; contrast.cbSize = sizeof(contrast);
 bool highContrast = SystemParametersInfoW (SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0) && (contrast.dwFlags & HCF_HIGHCONTRASTON);
 assert (InspectMenuAppearance (handle, 0) == (highContrast ? 0 : 1));
 if (!highContrast) {
  // Accessible names, mnemonics, measurement, drawing and system notifications.
  for (int operation : {1, 2, 3, 4, 6}) assert (InspectMenuAppearance (handle, operation) == 1);
  assert (InspectMenuAppearance (handle, 7) == 0x202020);
 }
 assert (InspectMenuAppearance (handle, 5) == 1);
 MENUITEMINFOW disabled = {}; disabled.cbSize = sizeof(disabled); disabled.fMask = MIIM_STATE;
 assert (GetMenuItemInfoW (GetSubMenu (GetMenu (handle), 0), 2, TRUE, &disabled) && (disabled.fState & MFS_DISABLED));
 SetWindowsMenuAppearance (handle, 1);
 assert (InspectMenuAppearance (handle, 0) == 0);
 SetWindowsMenuAppearance (handle, 2);
 assert (Menu::SetWindowMenu (&window, data.data (), (int)data.size ()));
 assert (InspectMenuAppearance (handle, 0) == (highContrast ? 0 : 1));
 assert (Menu::SetWindowMenu (&window, NULL, 0));
 DWORD objectsBefore = GetGuiResources (GetCurrentProcess (), GR_GDIOBJECTS);
 for (int i = 0; i < 20; ++i) {
  assert (Menu::SetWindowMenu (&window, data.data (), (int)data.size ()));
  SetWindowsMenuAppearance (handle, 1); SetWindowsMenuAppearance (handle, 2);
  assert (Menu::SetWindowMenu (&window, NULL, 0));
 }
 assert (GetGuiResources (GetCurrentProcess (), GR_GDIOBJECTS) <= objectsBefore + 2);
 Menu::Remove (&window);
 DestroyWindow (handle); UnregisterClassW (type.lpszClassName, instance);
 std::cout << "Native menu appearance, accessibility and resource cleanup passed\n";
}
