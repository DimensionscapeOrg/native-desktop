#include <native_desktop/Desktop.h>
#include <set>
namespace native_desktop {
static const int MAX_MENU_DEPTH = 32;
	static bool ReadInt (const unsigned char* data, int length, int* position, int* result) {

		if (length - *position < 4) return false;

		const unsigned char* bytes = data + *position;
		*result = (int)((uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) | ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24));
		*position += 4;
		return true;

	}


	static bool ParseItems (const unsigned char* data, int length, int* position, std::vector<MenuItem>* items, int depth, std::set<int>* ids) {

		int count;

		if (depth > MAX_MENU_DEPTH || !ReadInt (data, length, position, &count) || count < 0 || count > 0xEFFF || count > (length - *position) / 20) return false;

		items->resize (count);

		for (int i = 0; i < count; i++) {

			MenuItem* item = &(*items)[i];
			int labelLength;

			if (!ReadInt (data, length, position, &item->id) || !ReadInt (data, length, position, &item->flags)
				|| !ReadInt (data, length, position, &item->keyCode) || !ReadInt (data, length, position, &item->keyModifiers)
				|| !ReadInt (data, length, position, &labelLength) || labelLength < 0 || length - *position < labelLength) {

				return false;

			}

			item->label.assign ((const char*)(data + *position), labelLength);
			if (item->id <= 0 || item->id > 0xEFFF || (item->flags & ~15) != 0 || !ids->insert (item->id).second) return false;
			*position += labelLength;

			if ((item->flags & MENU_ITEM_SUBMENU) && !ParseItems (data, length, position, &item->submenu, depth + 1, ids)) {

				return false;

			}

		}

		return true;

	}


	std::string Menu::GetLabel (const std::string& label, char mnemonicPrefix) {

		if (mnemonicPrefix == '&') return label;

		std::string result;
		result.reserve (label.size ());

		for (size_t i = 0; i < label.size (); i++) {

			char c = label[i];

			if (c == '&') {

				if (i + 1 < label.size () && label[i + 1] == '&') {

					result += '&';
					i++;

				} else if (mnemonicPrefix) {

					result += mnemonicPrefix;

				}

			} else if (mnemonicPrefix && c == mnemonicPrefix) {

				result += mnemonicPrefix;
				result += mnemonicPrefix;

			} else {

				result += c;

			}

		}

		return result;

	}


	bool Menu::Parse (const unsigned char* data, int length, std::vector<MenuItem>* items) {

		int position = 0;
		if (!items || !data || length <= 0) return false;
		std::set<int> ids;
		std::vector<MenuItem> parsed;
		if (!ParseItems (data, length, &position, &parsed, 0, &ids) || position != length) return false;
		items->swap (parsed);
		return true;

	}



}
