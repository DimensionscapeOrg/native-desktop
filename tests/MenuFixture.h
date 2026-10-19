#ifndef NATIVE_DESKTOP_MENU_FIXTURE_H
#define NATIVE_DESKTOP_MENU_FIXTURE_H
#include <stdint.h>
#include <cstring>
#include <vector>
static void Integer (std::vector<unsigned char>& data, int value) {
 for (int i = 0; i < 4; ++i) data.push_back ((uint32_t)value >> (i * 8));
}
static void Item (std::vector<unsigned char>& data, int id, int flags, const char* label) {
 Integer (data, id); Integer (data, flags); Integer (data, 0); Integer (data, 0);
 Integer (data, (int)strlen (label)); data.insert (data.end (), label, label + strlen (label));
}
#endif
