#include <native_desktop/Desktop.h>
#include "ClipboardWindowsFormats.h"

#ifdef NATIVE_DESKTOP_WINDOWS

// Drag and drop through OLE: an IDropTarget registered on the window takes drops, which Windows then sends it
// instead of the WM_DROPFILES messages a host's own file drops use, and DoDragDrop drags an IDataObject out.

#include <windows.h>
#include <ole2.h>
#include <shlobj.h>
#include <map>
#include <string.h>


namespace native_desktop {


	// The shell's drag image helper, declared here so nothing more needs linking
	static const GUID DRAG_DROP_HELPER = { 0x4657278A, 0x411B, 0x11D2, { 0x83, 0x9A, 0x00, 0xC0, 0x4F, 0xD9, 0x18, 0xD0 } };
	static const GUID DRAG_SOURCE_HELPER = { 0xDE5BF786, 0x477A, 0x11D2, { 0x83, 0x9D, 0x00, 0xC0, 0x4F, 0xD9, 0x18, 0xD0 } };


	static bool InitializeOle () {

		static int state = 0;

		if (state == 0) {

			// OLE needs a single-threaded apartment on the UI thread, which it has unless something there
			// already asked for a multithreaded one
			HRESULT result = OleInitialize (NULL);
			state = SUCCEEDED (result) ? 1 : -1;

		}

		return state == 1;

	}


	static FORMATETC HGlobalFormat (UINT format) {

		FORMATETC etc = { (CLIPFORMAT)format, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
		return etc;

	}


	// What another application drags, read through its IDataObject while the drag goes on
	class OleDragData : public DragData {

		public:

			OleDragData (IDataObject* object) : object (object) {}

			bool HasFormat (int format) {

				return WindowsHasClipboardFormat (format, [this] (UINT native) { FORMATETC etc = HGlobalFormat (native); return object->QueryGetData (&etc) == S_OK; });

			}

			bool GetText (int format, std::string* text) {

				return text && WindowsReadClipboardText (format, Reader (), text);

			}

			bool GetFiles (std::vector<std::string>* files) {

				return files && WindowsReadClipboardFiles (Reader (), files);

			}

			bool GetImage (std::vector<unsigned char>* pixels, int* width, int* height) {

				return pixels && width && height && WindowsReadClipboardImage (Reader (), pixels, width, height);

			}

		private:

			WindowsFormatRead Reader () {

				return [this] (UINT native, std::vector<unsigned char>* data) {

					FORMATETC etc = HGlobalFormat (native);
					STGMEDIUM medium;
					if (object->GetData (&etc, &medium) != S_OK) return false;

					bool read = false;

					if (medium.tymed == TYMED_HGLOBAL && medium.hGlobal) {

						SIZE_T size = GlobalSize (medium.hGlobal);
						const unsigned char* bytes = (const unsigned char*)GlobalLock (medium.hGlobal);

						if (bytes) {

							data->assign (bytes, bytes + size);
							GlobalUnlock (medium.hGlobal);
							read = true;

						}

					}

					ReleaseStgMedium (&medium);
					return read;

				};

			}

			IDataObject* object;

	};


	class OleDropTarget : public IDropTarget {

		public:

			OleDropTarget (HWND window, uint32_t id) : allowed (0), data (NULL), effect (DROPEFFECT_NONE), id (id), references (1), window (window) {}

			~OleDropTarget () {

				if (data) data->Release ();

			}

			HRESULT STDMETHODCALLTYPE QueryInterface (REFIID iid, void** result) {

				if (!result) return E_POINTER;

				if (iid == __uuidof (IUnknown) || iid == __uuidof (IDropTarget)) {

					*result = static_cast<IDropTarget*> (this);
					AddRef ();
					return S_OK;

				}

				*result = NULL;
				return E_NOINTERFACE;

			}

			ULONG STDMETHODCALLTYPE AddRef () { return InterlockedIncrement (&references); }

			ULONG STDMETHODCALLTYPE Release () {

				ULONG count = InterlockedDecrement (&references);
				if (count == 0) delete this;
				return count;

			}

			HRESULT STDMETHODCALLTYPE DragEnter (IDataObject* object, DWORD keyState, POINTL point, DWORD* result) {

				if (data) data->Release ();
				data = object;
				if (data) data->AddRef ();

				allowed = result ? (int)(*result & (DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK)) : 0;
				effect = Send (DRAG_ENTER, point);
				if (result) *result = effect;
				return S_OK;

			}

			HRESULT STDMETHODCALLTYPE DragOver (DWORD keyState, POINTL point, DWORD* result) {

				effect = Send (DRAG_OVER, point);
				if (result) *result = effect;
				return S_OK;

			}

			HRESULT STDMETHODCALLTYPE DragLeave () {

				DragEvent event = { DRAG_LEAVE, id, 0, 0, allowed };
				DragDrop::Dispatch (event, NULL);

				if (data) data->Release ();
				data = NULL;
				return S_OK;

			}

			HRESULT STDMETHODCALLTYPE Drop (IDataObject* object, DWORD keyState, POINTL point, DWORD* result) {

				if (object && object != data) {

					if (data) data->Release ();
					data = object;
					data->AddRef ();

				}

				effect = Send (DRAG_DROP, point);
				if (result) *result = effect;

				if (data) data->Release ();
				data = NULL;
				return S_OK;

			}

		private:

			DWORD Send (int type, POINTL screen) {

				if (!data) return DROPEFFECT_NONE;

				POINT point = { screen.x, screen.y };
				ScreenToClient (window, &point);

				DragEvent event = { type, id, point.x, point.y, allowed };
				OleDragData dragData (data);
				return (DWORD)DragDrop::Dispatch (event, &dragData);

			}

			int allowed;
			IDataObject* data;
			DWORD effect;
			uint32_t id;
			LONG references;
			HWND window;

	};


	// What this application drags out: the clipboard's encodings of its items, and anything the shell's drag
	// image helper keeps on it with SetData
	class OleDataSource : public IDataObject {

		public:

			OleDataSource (const std::vector<WindowsFormatData>& formats) : formats (formats), references (1) {}

			HRESULT STDMETHODCALLTYPE QueryInterface (REFIID iid, void** result) {

				if (!result) return E_POINTER;

				if (iid == __uuidof (IUnknown) || iid == __uuidof (IDataObject)) {

					*result = static_cast<IDataObject*> (this);
					AddRef ();
					return S_OK;

				}

				*result = NULL;
				return E_NOINTERFACE;

			}

			ULONG STDMETHODCALLTYPE AddRef () { return InterlockedIncrement (&references); }

			ULONG STDMETHODCALLTYPE Release () {

				ULONG count = InterlockedDecrement (&references);
				if (count == 0) delete this;
				return count;

			}

			HRESULT STDMETHODCALLTYPE GetData (FORMATETC* etc, STGMEDIUM* medium) {

				if (!etc || !medium) return E_INVALIDARG;
				const WindowsFormatData* found = Find (etc);
				if (!found) return DV_E_FORMATETC;

				HGLOBAL memory = GlobalAlloc (GMEM_MOVEABLE, found->data.size () ? found->data.size () : 1);
				if (!memory) return E_OUTOFMEMORY;

				void* target = GlobalLock (memory);
				if (!found->data.empty ()) memcpy (target, &found->data[0], found->data.size ());
				GlobalUnlock (memory);

				medium->tymed = TYMED_HGLOBAL;
				medium->hGlobal = memory;
				medium->pUnkForRelease = NULL;
				return S_OK;

			}

			HRESULT STDMETHODCALLTYPE GetDataHere (FORMATETC*, STGMEDIUM*) { return E_NOTIMPL; }

			HRESULT STDMETHODCALLTYPE QueryGetData (FORMATETC* etc) {

				return etc && Find (etc) ? S_OK : DV_E_FORMATETC;

			}

			HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc (FORMATETC*, FORMATETC* result) {

				if (result) result->ptd = NULL;
				return DATA_S_SAMEFORMATETC;

			}

			HRESULT STDMETHODCALLTYPE SetData (FORMATETC* etc, STGMEDIUM* medium, BOOL release) {

				if (!etc || !medium || medium->tymed != TYMED_HGLOBAL || !medium->hGlobal) return E_NOTIMPL;

				WindowsFormatData entry;
				entry.format = etc->cfFormat;

				SIZE_T size = GlobalSize (medium->hGlobal);
				const unsigned char* bytes = (const unsigned char*)GlobalLock (medium->hGlobal);
				if (bytes) entry.data.assign (bytes, bytes + size);
				GlobalUnlock (medium->hGlobal);

				for (size_t i = 0; i < formats.size (); i++) {

					if (formats[i].format == entry.format) {

						formats.erase (formats.begin () + i);
						break;

					}

				}

				formats.push_back (entry);
				if (release) ReleaseStgMedium (medium);
				return S_OK;

			}

			HRESULT STDMETHODCALLTYPE EnumFormatEtc (DWORD direction, IEnumFORMATETC** result) {

				if (!result) return E_POINTER;
				if (direction != DATADIR_GET) return E_NOTIMPL;

				std::vector<FORMATETC> list;
				for (size_t i = 0; i < formats.size (); i++) list.push_back (HGlobalFormat (formats[i].format));
				return SHCreateStdEnumFmtEtc ((UINT)list.size (), list.empty () ? NULL : &list[0], result);

			}

			HRESULT STDMETHODCALLTYPE DAdvise (FORMATETC*, DWORD, IAdviseSink*, DWORD*) { return OLE_E_ADVISENOTSUPPORTED; }
			HRESULT STDMETHODCALLTYPE DUnadvise (DWORD) { return OLE_E_ADVISENOTSUPPORTED; }
			HRESULT STDMETHODCALLTYPE EnumDAdvise (IEnumSTATDATA**) { return OLE_E_ADVISENOTSUPPORTED; }

		private:

			const WindowsFormatData* Find (const FORMATETC* etc) {

				if (!(etc->tymed & TYMED_HGLOBAL) || etc->dwAspect != DVASPECT_CONTENT) return NULL;

				for (size_t i = 0; i < formats.size (); i++) {

					if (formats[i].format == etc->cfFormat) return &formats[i];

				}

				return NULL;

			}

			std::vector<WindowsFormatData> formats;
			LONG references;

	};


	class OleDropSource : public IDropSource {

		public:

			OleDropSource () : references (1) {}

			HRESULT STDMETHODCALLTYPE QueryInterface (REFIID iid, void** result) {

				if (!result) return E_POINTER;

				if (iid == __uuidof (IUnknown) || iid == __uuidof (IDropSource)) {

					*result = static_cast<IDropSource*> (this);
					AddRef ();
					return S_OK;

				}

				*result = NULL;
				return E_NOINTERFACE;

			}

			ULONG STDMETHODCALLTYPE AddRef () { return InterlockedIncrement (&references); }

			ULONG STDMETHODCALLTYPE Release () {

				ULONG count = InterlockedDecrement (&references);
				if (count == 0) delete this;
				return count;

			}

			HRESULT STDMETHODCALLTYPE QueryContinueDrag (BOOL escape, DWORD keyState) {

				if (escape) return DRAGDROP_S_CANCEL;
				if (!(keyState & (MK_LBUTTON | MK_RBUTTON))) return DRAGDROP_S_DROP;

				// DoDragDrop runs its own message loop, so the host draws from here meanwhile
				if (Menu::HasModalCallback () && !Menu::AdvanceModal ()) return DRAGDROP_S_CANCEL;
				return S_OK;

			}

			HRESULT STDMETHODCALLTYPE GiveFeedback (DWORD) { return DRAGDROP_S_USEDEFAULTCURSORS; }

		private:

			LONG references;

	};


	// The drag image, premultiplied BGRA as the shell's helper takes it, with the top row first
	static HBITMAP CreateDragBitmap (Image* image) {

		BITMAPINFO info;
		ZeroMemory (&info, sizeof (info));
		info.bmiHeader.biSize = sizeof (BITMAPINFOHEADER);
		info.bmiHeader.biWidth = image->width;
		info.bmiHeader.biHeight = -image->height;
		info.bmiHeader.biPlanes = 1;
		info.bmiHeader.biBitCount = 32;
		info.bmiHeader.biCompression = BI_RGB;

		void* bits = NULL;
		HBITMAP bitmap = CreateDIBSection (NULL, &info, DIB_RGB_COLORS, &bits, NULL, 0);
		if (!bitmap || !bits) return NULL;

		unsigned char* target = (unsigned char*)bits;

		for (int y = 0; y < image->height; y++) {

			const unsigned char* source = image->data + (size_t)y * image->stride;

			for (int x = 0; x < image->width; x++) {

				unsigned int alpha = source[3];
				target[0] = (unsigned char)(source[2] * alpha / 255);
				target[1] = (unsigned char)(source[1] * alpha / 255);
				target[2] = (unsigned char)(source[0] * alpha / 255);
				target[3] = (unsigned char)alpha;
				source += 4;
				target += 4;

			}

		}

		return bitmap;

	}


	static std::map<HWND, OleDropTarget*> targets;


	bool DragDrop::IsSupported () {

		return true;

	}


	bool DragDrop::SetTarget (Window* window, bool enabled) {

		if (!window || !window->handle || !Menu::IsOwnerThread () || !InitializeOle ()) return false;

		HWND handle = (HWND)window->handle;
		std::map<HWND, OleDropTarget*>::iterator found = targets.find (handle);

		if (enabled) {

			if (found != targets.end ()) return true;

			OleDropTarget* target = new OleDropTarget (handle, window->id);

			if (FAILED (RegisterDragDrop (handle, target))) {

				target->Release ();
				return false;

			}

			targets[handle] = target;
			return true;

		}

		if (found == targets.end ()) return true;

		// The window's own WM_DROPFILES drops come back once OLE lets it go
		RevokeDragDrop (handle);
		found->second->Release ();
		targets.erase (found);
		return true;

	}


	int DragDrop::StartDrag (Window* window, const std::vector<ClipboardItem>& items, Image* image, int offsetX, int offsetY, int allowedActions, int id) {

		if (!Menu::IsOwnerThread () || !InitializeOle ()) return DROP_ACTION_NONE;

		// A drag starts from a pressed button, and drops where it is let go
		int button = GetSystemMetrics (SM_SWAPBUTTON) ? VK_RBUTTON : VK_LBUTTON;
		if (!(GetAsyncKeyState (button) & 0x8000)) return DROP_ACTION_NONE;

		std::vector<WindowsFormatData> formats;
		WindowsEncodeClipboardItems (items, &formats);
		if (formats.empty ()) return DROP_ACTION_NONE;

		OleDataSource* source = new OleDataSource (formats);
		OleDropSource* dropSource = new OleDropSource ();

		if (image && image->Valid ()) {

			IDragSourceHelper* helper = NULL;

			if (SUCCEEDED (CoCreateInstance (DRAG_DROP_HELPER, NULL, CLSCTX_INPROC_SERVER, DRAG_SOURCE_HELPER, (void**)&helper))) {

				SHDRAGIMAGE dragImage;
				ZeroMemory (&dragImage, sizeof (dragImage));
				dragImage.sizeDragImage.cx = image->width;
				dragImage.sizeDragImage.cy = image->height;
				dragImage.ptOffset.x = offsetX;
				dragImage.ptOffset.y = offsetY;
				dragImage.hbmpDragImage = CreateDragBitmap (image);
				dragImage.crColorKey = CLR_NONE;

				// The helper owns the bitmap once it takes it
				if (dragImage.hbmpDragImage && FAILED (helper->InitializeFromBitmap (&dragImage, source))) DeleteObject (dragImage.hbmpDragImage);
				helper->Release ();

			}

		}

		DWORD allowed = (DWORD)(allowedActions & (DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK));
		DWORD effect = DROPEFFECT_NONE;
		HRESULT result = DoDragDrop (source, dropSource, allowed ? allowed : DROPEFFECT_COPY, &effect);

		dropSource->Release ();
		source->Release ();

		if (result != DRAGDROP_S_DROP) return DROP_ACTION_NONE;
		if (effect & DROPEFFECT_MOVE) return DROP_ACTION_MOVE;
		if (effect & DROPEFFECT_COPY) return DROP_ACTION_COPY;
		if (effect & DROPEFFECT_LINK) return DROP_ACTION_LINK;
		return DROP_ACTION_NONE;

	}


	void DragDrop::Update () {}


}


#endif
