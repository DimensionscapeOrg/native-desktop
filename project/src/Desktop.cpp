#include <native_desktop/Desktop.h>
#include <deque>
#include <cstdio>
#if defined(NATIVE_DESKTOP_WINDOWS)
#include <windows.h>
#elif defined(NATIVE_DESKTOP_MAC) || defined(NATIVE_DESKTOP_LINUX)
#include <pthread.h>
#endif

namespace native_desktop {
#ifndef NATIVE_DESKTOP_LINUX
void Menu::SetDisplayBackend (const char*) {}
#endif
static EventSink sink = NULL;
static void* sinkData = NULL;
static std::deque<Event> events;
static const char* (*keyNameProvider)(int) = NULL;
static ModalCallback modalCallback = NULL;
static void* modalData = NULL;
static bool inModalCallback = false;
void Menu::SetModalCallback (ModalCallback callback, void* data) { if (IsOwnerThread ()) { modalCallback = callback; modalData = data; } }
bool Menu::HasModalCallback () { return modalCallback != NULL; }
bool Menu::AdvanceModal () {
 if (!IsOwnerThread () || !modalCallback || inModalCallback) return true;
 inModalCallback = true;
 bool result = true;
 try { result = modalCallback (modalData); }
 catch (...) { result = false; }
 inModalCallback = false;
 return result;
}
bool Menu::IsOwnerThread () {
 #ifdef NATIVE_DESKTOP_WINDOWS
 static const DWORD owner = GetCurrentThreadId ();
 return GetCurrentThreadId () == owner;
 #elif defined(NATIVE_DESKTOP_MAC) || defined(NATIVE_DESKTOP_LINUX)
 static const pthread_t owner = pthread_self ();
 return pthread_equal (pthread_self (), owner) != 0;
 #else
 return true;
 #endif
}
static void Push (int kind, uint32_t target, int code, int value) {
 if (!Menu::IsOwnerThread ()) return;
 Event event = {kind, target, code, value};
 if (sink) sink (event, sinkData); else events.push_back (event);
}
void Menu::SetEventSink (EventSink value, void* data) { if (IsOwnerThread ()) { sink = value; sinkData = data; } }
bool Menu::PollEvent (Event* event) {
 if (!IsOwnerThread () || !event || events.empty ()) return false;
 *event = events.front (); events.pop_front (); return true;
}
void Menu::PushSelection (uint32_t window, int id) { Push (MENU_EVENT_SELECTION, window, id, 0); }
void Menu::PushDockMenuSelection (int id) { Push (MENU_EVENT_DOCK_SELECTION, 0, id, 0); }
void Menu::PushTrayIconEvent (int id, int type, int item) { Push (MENU_EVENT_TRAY_ICON, id, type, item); }
void Menu::SetKeyNameProvider (const char* (*provider)(int)) { if (IsOwnerThread ()) keyNameProvider = provider; }
const char* Menu::KeyName (int key) {
 if (keyNameProvider) return keyNameProvider (key);
 static thread_local char text[32];
 if (key >= KEY_F1 && key <= KEY_F12) { snprintf (text, sizeof(text), "F%d", key - KEY_F1 + 1); return text; }
 if (key >= KEY_F13 && key <= KEY_F24) { snprintf (text, sizeof(text), "F%d", key - KEY_F13 + 13); return text; }
 if (key >= 33 && key < 127) { text[0] = key >= 'a' && key <= 'z' ? key - 32 : key; text[1] = 0; return text; }
 switch (key) {
  case 32: return "Space"; case KEY_BACKSPACE: return "Backspace"; case KEY_TAB: return "Tab";
  case KEY_RETURN: return "Return"; case KEY_ESCAPE: return "Escape"; case KEY_DELETE: return "Delete";
  case KEY_HOME: return "Home"; case KEY_END: return "End"; case KEY_INSERT: return "Insert";
  case KEY_PAGEUP: return "PageUp"; case KEY_PAGEDOWN: return "PageDown";
  case KEY_LEFT: return "Left"; case KEY_RIGHT: return "Right"; case KEY_UP: return "Up"; case KEY_DOWN: return "Down";
  default: return "";
 }
}

#ifndef NATIVE_DESKTOP_MAC
// Only macOS has an application menu bar that starts with standard items
bool Menu::GetApplicationMenu (std::vector<unsigned char>*) { return false; }
void DockIcon::Bounce (bool) {}
bool DockIcon::IsSupported () { return false; }
void DockIcon::SetIcon (Image*) {}
bool DockIcon::SetMenu (const unsigned char*, int) { return false; }
#endif
#if !defined(NATIVE_DESKTOP_WINDOWS) && !defined(NATIVE_DESKTOP_MAC) && !defined(NATIVE_DESKTOP_LINUX)
int Menu::GetSupport () { return 0; }
int Menu::Popup (Window*, const unsigned char*, int, int, int, bool) { return 0; }
void Menu::Remove (Window*) {}
bool Menu::SetApplicationMenu (const unsigned char*, int) { return false; }
bool Menu::SetWindowMenu (Window*, const unsigned char*, int) { return false; }
TrayIcon::TrayIcon () : id(0), platform(NULL) {}
TrayIcon::~TrayIcon () {}
void TrayIcon::Close () {}
int TrayIcon::PopupMenu (const unsigned char*, int) { return 0; }
void TrayIcon::SetIcon (Image*) {}
bool TrayIcon::SetIconFile (const char*) { return false; }
bool TrayIcon::SetMenu (const unsigned char*, int) { return false; }
void TrayIcon::SetTooltip (const char*) {}
bool TrayIcon::IsSupported () { return false; }
void TrayIcon::Update () {}
#endif
}
