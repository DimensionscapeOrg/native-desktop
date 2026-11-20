#include <native_desktop/Desktop.h>

// Moving files and folders to the system's trash: IFileOperation on Windows, GIO or the freedesktop.org trash
// specification on Linux. macOS is in TrashMac.mm.

#if defined(NATIVE_DESKTOP_WINDOWS)

#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <string>

namespace native_desktop {


	// Defined here, as not every SDK's libraries have them
	static const CLSID FILE_OPERATION_CLSID = { 0x3ad05575, 0x8857, 0x4850, { 0x92, 0x77, 0x11, 0xb8, 0x5b, 0xdb, 0x8e, 0x09 } };
	static const IID FILE_OPERATION_IID = { 0x947aab5f, 0x0a5c, 0x4c13, { 0xb4, 0xd6, 0x4b, 0xf7, 0x83, 0x6f, 0xc9, 0xf8 } };
	static const IID SHELL_ITEM_IID = { 0x43826d1e, 0xe718, 0x42ee, { 0xbc, 0x55, 0xa1, 0xe2, 0x61, 0xc3, 0x7b, 0xfe } };


	static std::wstring TrashWide (const char* text) {

		int size = MultiByteToWideChar (CP_UTF8, 0, text, -1, NULL, 0);
		if (size <= 0) return std::wstring ();
		std::wstring result (size, L'\0');
		MultiByteToWideChar (CP_UTF8, 0, text, -1, &result[0], size);
		result.resize (size - 1);
		return result;

	}


	bool Trash::IsSupported () {

		return true;

	}


	bool Trash::MoveToTrash (const char* path) {

		if (!path || !path[0]) return false;

		std::wstring wide = TrashWide (path);
		DWORD length = GetFullPathNameW (wide.c_str (), 0, NULL, NULL);
		if (length == 0) return false;

		std::wstring full (length, L'\0');
		length = GetFullPathNameW (wide.c_str (), length, &full[0], NULL);
		full.resize (length);

		// A trailing separator would name the folder's contents to the shell
		while (full.size () > 3 && (full.back () == L'\\' || full.back () == L'/')) full.pop_back ();
		if (GetFileAttributesW (full.c_str ()) == INVALID_FILE_ATTRIBUTES) return false;

		// Without a Recycle Bin, as on a network drive, Windows would delete the file without asking, as
		// confirmations are off, so it is left where it is
		SHQUERYRBINFO info;
		ZeroMemory (&info, sizeof (info));
		info.cbSize = sizeof (info);
		if (FAILED (SHQueryRecycleBinW (full.c_str (), &info))) return false;

		// Any thread may call this, so COM is started here, unless the thread already has it in another mode
		HRESULT initialized = CoInitializeEx (NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

		bool result = false;
		IFileOperation* operation = NULL;

		if (SUCCEEDED (CoCreateInstance (FILE_OPERATION_CLSID, NULL, CLSCTX_ALL, FILE_OPERATION_IID, (void**)&operation))) {

			IShellItem* item = NULL;

			if (SUCCEEDED (operation->SetOperationFlags (FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI | FOFX_RECYCLEONDELETE | FOFX_EARLYFAILURE))
				&& SUCCEEDED (SHCreateItemFromParsingName (full.c_str (), NULL, SHELL_ITEM_IID, (void**)&item))) {

				BOOL aborted = FALSE;
				result = SUCCEEDED (operation->DeleteItem (item, NULL)) && SUCCEEDED (operation->PerformOperations ())
					&& SUCCEEDED (operation->GetAnyOperationsAborted (&aborted)) && !aborted;

				item->Release ();

			}

			operation->Release ();

		}

		if (SUCCEEDED (initialized)) CoUninitialize ();

		// Done when the item is gone from where it was
		return result && GetFileAttributesW (full.c_str ()) == INVALID_FILE_ATTRIBUTES;

	}


}

#elif defined(NATIVE_DESKTOP_LINUX)

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <string>

namespace native_desktop {


	struct GIOLibrary {

		bool loaded;
		bool available;
		void* (*g_file_new_for_path) (const char*);
		int (*g_file_trash) (void*, void*, void**);
		void (*g_object_unref) (void*);
		void (*g_error_free) (void*);

	};


	static GIOLibrary gio;


	static bool LoadGIO () {

		if (gio.loaded) return gio.available;
		gio.loaded = true;

		void* library = dlopen ("libgio-2.0.so.0", RTLD_NOW | RTLD_LOCAL);
		if (!library) return false;

		gio.g_file_new_for_path = (decltype (gio.g_file_new_for_path))dlsym (library, "g_file_new_for_path");
		gio.g_file_trash = (decltype (gio.g_file_trash))dlsym (library, "g_file_trash");
		gio.g_object_unref = (decltype (gio.g_object_unref))dlsym (library, "g_object_unref");
		gio.g_error_free = (decltype (gio.g_error_free))dlsym (library, "g_error_free");
		gio.available = gio.g_file_new_for_path && gio.g_file_trash && gio.g_object_unref && gio.g_error_free;
		return gio.available;

	}


	static std::string PercentEncode (const std::string& path) {

		static const char* hex = "0123456789ABCDEF";
		std::string result;

		for (size_t i = 0; i < path.size (); i++) {

			unsigned char c = (unsigned char)path[i];

			if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || strchr ("/-_.~", c)) {

				result += (char)c;

			} else {

				result += '%';
				result += hex[c >> 4];
				result += hex[c & 15];

			}

		}

		return result;

	}


	// The user's home trash of the freedesktop.org trash specification, for files on the same file system
	static bool MoveToHomeTrash (const std::string& path) {

		const char* dataHome = getenv ("XDG_DATA_HOME");
		const char* home = getenv ("HOME");
		std::string trash = (dataHome && dataHome[0]) ? std::string (dataHome) + "/Trash" : (home ? std::string (home) + "/.local/share/Trash" : std::string ());
		if (trash.empty ()) return false;

		std::string parent = trash.substr (0, trash.rfind ('/'));
		mkdir (parent.c_str (), 0700);
		mkdir (trash.c_str (), 0700);
		mkdir ((trash + "/files").c_str (), 0700);
		mkdir ((trash + "/info").c_str (), 0700);

		// Another file system's files go to its own trash, which this does not make
		struct stat item, trashInfo;
		if (lstat (path.c_str (), &item) != 0 || stat ((trash + "/files").c_str (), &trashInfo) != 0 || item.st_dev != trashInfo.st_dev) return false;

		std::string name = path.substr (path.rfind ('/') + 1);
		time_t now = time (NULL);
		struct tm local;
		localtime_r (&now, &local);
		char date[32];
		strftime (date, sizeof (date), "%Y-%m-%dT%H:%M:%S", &local);

		for (int attempt = 1; attempt < 1000; attempt++) {

			std::string candidate = attempt == 1 ? name : name + "." + std::to_string (attempt);
			std::string info = trash + "/info/" + candidate + ".trashinfo";

			// The info file is made first, and only where none exists, which claims the name
			int file = open (info.c_str (), O_WRONLY | O_CREAT | O_EXCL, 0600);
			if (file < 0) { if (errno == EEXIST) continue; return false; }

			std::string content = "[Trash Info]\nPath=" + PercentEncode (path) + "\nDeletionDate=" + date + "\n";
			bool written = write (file, content.c_str (), content.size ()) == (ssize_t)content.size ();
			close (file);

			std::string target = trash + "/files/" + candidate;

			if (written && access (target.c_str (), F_OK) != 0 && rename (path.c_str (), target.c_str ()) == 0) return true;

			unlink (info.c_str ());
			if (!written || access (target.c_str (), F_OK) != 0) return false;

		}

		return false;

	}


	bool Trash::IsSupported () {

		return true;

	}


	bool Trash::MoveToTrash (const char* path) {

		if (!path || !path[0]) return false;

		// The item itself, not what a link points to, in an absolute path for its trash info
		std::string absolute = path;
		while (absolute.size () > 1 && absolute.back () == '/') absolute.pop_back ();

		size_t slash = absolute.rfind ('/');
		std::string folder = slash == std::string::npos ? "." : (slash == 0 ? "/" : absolute.substr (0, slash));
		std::string name = slash == std::string::npos ? absolute : absolute.substr (slash + 1);

		char resolved[PATH_MAX];
		if (!realpath (folder.c_str (), resolved)) return false;
		absolute = std::string (resolved) + (strcmp (resolved, "/") == 0 ? "" : "/") + name;

		struct stat item;
		if (lstat (absolute.c_str (), &item) != 0) return false;

		if (LoadGIO ()) {

			void* file = gio.g_file_new_for_path (absolute.c_str ());
			void* error = NULL;
			bool result = gio.g_file_trash (file, NULL, &error) != 0;
			if (error) gio.g_error_free (error);
			gio.g_object_unref (file);
			if (result) return true;

		}

		return MoveToHomeTrash (absolute);

	}


}

#endif
