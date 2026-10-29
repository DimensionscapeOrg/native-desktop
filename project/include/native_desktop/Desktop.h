#ifndef NATIVE_DESKTOP_H
#define NATIVE_DESKTOP_H
#include <stdint.h>
#include <stddef.h>
#include <string>
#include <vector>
#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#if defined(_WIN32) && !defined(HX_WINRT)
#define NATIVE_DESKTOP_WINDOWS
#elif defined(__APPLE__) && TARGET_OS_OSX
#define NATIVE_DESKTOP_MAC
#elif defined(__linux__) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__) && !defined(EMSCRIPTEN)
#define NATIVE_DESKTOP_LINUX
#endif

namespace native_desktop {
enum MenuSupport { MENU_SUPPORT_POPUP = 1, MENU_SUPPORT_WINDOW = 2, MENU_SUPPORT_APPLICATION = 4 };
enum ItemFlags { MENU_ITEM_ENABLED = 1, MENU_ITEM_CHECKED = 2, MENU_ITEM_SEPARATOR = 4, MENU_ITEM_SUBMENU = 8, MENU_ITEM_ACTION = 16 };
enum EventKind { MENU_EVENT_SELECTION = 1, MENU_EVENT_TRAY_ICON = 2, MENU_EVENT_DOCK_SELECTION = 3 };
enum TrayEvent { TRAY_ICON_CLICK = 0, TRAY_ICON_MENU_SELECT = 1, TRAY_ICON_RIGHT_CLICK = 2 };
// Printable key values are Unicode. Non-printable keys use USB HID usage | (1 << 30).
enum KeyCode {
 KEY_BACKSPACE=8, KEY_TAB=9, KEY_RETURN=13, KEY_ESCAPE=27, KEY_DELETE=127,
 KEY_F1=0x4000003a, KEY_F12=0x40000045, KEY_F13=0x40000068, KEY_F24=0x40000073,
 KEY_INSERT=0x40000049, KEY_HOME=0x4000004a, KEY_PAGEUP=0x4000004b,
 KEY_END=0x4000004d, KEY_PAGEDOWN=0x4000004e, KEY_RIGHT=0x4000004f,
 KEY_LEFT=0x40000050, KEY_DOWN=0x40000051, KEY_UP=0x40000052
};
enum Modifier { MOD_SHIFT=0x0003, MOD_CTRL=0x00c0, MOD_ALT=0x0300, MOD_GUI=0x0c00 };

// Menu data is little-endian Int32 values:
//  menu := count, item[count]
//  item := id, flags, keyCode, keyModifiers, labelLength, label,
//          [actionLength, action if MENU_ITEM_ACTION], [menu if MENU_ITEM_SUBMENU]
// An action names what a standard item of the application menu bar does, as
// "terminate:" for Quit on macOS, where the item keeps doing it when installed.
struct MenuItem {
 int flags, id, keyCode, keyModifiers;
 std::string label, action;
 std::vector<MenuItem> submenu;
};
// A borrowed native window. The host retains ownership of the window and display.
// handle is HWND, NSWindow*, or the X11 Window integer cast to void*.
struct Window {
 void* handle = NULL;
 void* display = NULL;
 uint32_t id = 0;
 int x = 0, y = 0, appearance = 0;
};
struct Image {
 const unsigned char* data = NULL;
 size_t length = 0;
 int width = 0, height = 0, stride = 0, bitsPerPixel = 32;
 const unsigned char* png = NULL;
 int pngLength = 0;
 bool Valid () const {
  return data && width > 0 && height > 0 && bitsPerPixel == 32
   && (size_t)width <= (size_t)stride / 4 && (size_t)stride <= length / (size_t)height;
 }
};
struct Event { int kind; uint32_t target; int code, value; };
typedef void (*EventSink) (const Event&, void*);
typedef bool (*ModalCallback) (void*);
class Menu {
 public:
 static int GetSupport ();
 static int Popup (Window*, const unsigned char*, int, int, int, bool);
 static void Remove (Window*);
 static bool SetApplicationMenu (const unsigned char*, int);
 static bool SetWindowMenu (Window*, const unsigned char*, int);
 static bool Parse (const unsigned char*, int, std::vector<MenuItem>*);
 // Replaces data with items in the format Parse reads.
 static void Write (const std::vector<MenuItem>&, std::vector<unsigned char>*);
 // The installed application menu bar, in the same format. On macOS it holds standard
 // items from the start. False where there is no application menu bar.
 static bool GetApplicationMenu (std::vector<unsigned char>*);
 static std::string GetLabel (const std::string&, char);
 static void PushSelection (uint32_t, int);
 static void PushDockMenuSelection (int);
 static void PushTrayIconEvent (int, int, int);
 // A host may forward events immediately, or leave the sink null and poll.
 // One host integration owns this library instance, on one UI thread.
 static void SetEventSink (EventSink sink, void* userData);
 static bool PollEvent (Event*);
 static void SetKeyNameProvider (const char* (*provider)(int));
 static const char* KeyName (int);
 static bool IsOwnerThread ();
 // Optional host progress while the OS tracks a modal menu. False cancels tracking.
 static void SetModalCallback (ModalCallback callback, void* userData);
 static bool HasModalCallback ();
 static bool AdvanceModal ();
 // Optional host toolkit selection before GTK is initialized (for an X11 host).
 static void SetDisplayBackend (const char* backend);
};
class TrayIcon {
 public:
 TrayIcon ();
 ~TrayIcon ();
 TrayIcon (const TrayIcon&) = delete;
 TrayIcon& operator= (const TrayIcon&) = delete;
 void Close ();
 int GetID () const { return id; }
 bool IsVisible () const { return platform != NULL; }
 int PopupMenu (const unsigned char*, int);
 void SetIcon (Image*);
 bool SetIconFile (const char*);
 bool SetMenu (const unsigned char*, int);
 void SetTooltip (const char*);
 static bool IsSupported ();
 // Nonblocking, bounded native-event processing. The owning UI thread calls this.
 static void Update ();
 int id;
 void* platform;
};
class DockIcon {
 public:
 static void Bounce (bool);
 static bool IsSupported ();
 static void SetIcon (Image*);
 static bool SetMenu (const unsigned char*, int);
};
}
#endif
