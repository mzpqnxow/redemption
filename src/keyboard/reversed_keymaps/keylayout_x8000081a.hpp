#ifndef _REDEMPTION_KEYBOARD_KEYLAYOUT_R_X8000081a_HPP_
#define _REDEMPTION_KEYBOARD_KEYLAYOUT_R_X8000081a_HPP_

#include "keylayout_r.hpp"

namespace x8000081a{ 

const static int LCID = 0x81a;

const static char * const locale_name = "sr-La";

const Keylayout_r::KeyLayoutMap_t noMod
{
	{ 0x001b, 0x1 },
	{ 0x0031, 0x2 },
	{ 0x0032, 0x3 },
	{ 0x0033, 0x4 },
	{ 0x0034, 0x5 },
	{ 0x0035, 0x6 },
	{ 0x0036, 0x7 },
	{ 0x0037, 0x8 },
	{ 0x0038, 0x9 },
	{ 0x0039, 0xa },
	{ 0x0030, 0xb },
	{ 0x0027, 0xc },
	{ 0x002b, 0xd },
	{ 0x0008, 0xe },
	{ 0x0009, 0xf },
	{ 0x0071, 0x10 },
	{ 0x0077, 0x11 },
	{ 0x0065, 0x12 },
	{ 0x0072, 0x13 },
	{ 0x0074, 0x14 },
	{ 0x007a, 0x15 },
	{ 0x0075, 0x16 },
	{ 0x0069, 0x17 },
	{ 0x006f, 0x18 },
	{ 0x0070, 0x19 },
	{ 0x0161, 0x1a },
	{ 0x0111, 0x1b },
	{ 0x000d, 0x1c },
	{ 0x0061, 0x1e },
	{ 0x0073, 0x1f },
	{ 0x0064, 0x20 },
	{ 0x0066, 0x21 },
	{ 0x0067, 0x22 },
	{ 0x0068, 0x23 },
	{ 0x006a, 0x24 },
	{ 0x006b, 0x25 },
	{ 0x006c, 0x26 },
	{ 0x010d, 0x27 },
	{ 0x0107, 0x28 },
	{ 0x201a, 0x29 },
	{ 0x017e, 0x2b },
	{ 0x0079, 0x2c },
	{ 0x0078, 0x2d },
	{ 0x0063, 0x2e },
	{ 0x0076, 0x2f },
	{ 0x0062, 0x30 },
	{ 0x006e, 0x31 },
	{ 0x006d, 0x32 },
	{ 0x002c, 0x33 },
	{ 0x002e, 0x34 },
	{ 0x002d, 0x35 },
	{ 0x002a, 0x37 },
	{ 0x0020, 0x39 },
	{ 0x0037, 0x47 },
	{ 0x0038, 0x48 },
	{ 0x0039, 0x49 },
	{ 0x002d, 0x4a },
	{ 0x0034, 0x4b },
	{ 0x0035, 0x4c },
	{ 0x0036, 0x4d },
	{ 0x002b, 0x4e },
	{ 0x0031, 0x4f },
	{ 0x0032, 0x50 },
	{ 0x0033, 0x51 },
	{ 0x0030, 0x52 },
	{ 0x002e, 0x53 },
	{ 0x003c, 0x56 },
	{ 0x002f, 0x62 },
	{ 0x000d, 0x64 },
	{ 0x002e, 0x7e },
};


const Keylayout_r::KeyLayoutMap_t shift
{
	{ 0x001b, 0x1 },
	{ 0x0021, 0x2 },
	{ 0x0022, 0x3 },
	{ 0x0023, 0x4 },
	{ 0x0024, 0x5 },
	{ 0x0025, 0x6 },
	{ 0x0026, 0x7 },
	{ 0x002f, 0x8 },
	{ 0x0028, 0x9 },
	{ 0x0029, 0xa },
	{ 0x003d, 0xb },
	{ 0x003f, 0xc },
	{ 0x002a, 0xd },
	{ 0x0008, 0xe },
	{ 0x0051, 0x10 },
	{ 0x0057, 0x11 },
	{ 0x0045, 0x12 },
	{ 0x0052, 0x13 },
	{ 0x0054, 0x14 },
	{ 0x005a, 0x15 },
	{ 0x0055, 0x16 },
	{ 0x0049, 0x17 },
	{ 0x004f, 0x18 },
	{ 0x0050, 0x19 },
	{ 0x0160, 0x1a },
	{ 0x0110, 0x1b },
	{ 0x000d, 0x1c },
	{ 0x0041, 0x1e },
	{ 0x0053, 0x1f },
	{ 0x0044, 0x20 },
	{ 0x0046, 0x21 },
	{ 0x0047, 0x22 },
	{ 0x0048, 0x23 },
	{ 0x004a, 0x24 },
	{ 0x004b, 0x25 },
	{ 0x004c, 0x26 },
	{ 0x010c, 0x27 },
	{ 0x0106, 0x28 },
	{ 0x007e, 0x29 },
	{ 0x017d, 0x2b },
	{ 0x0059, 0x2c },
	{ 0x0058, 0x2d },
	{ 0x0043, 0x2e },
	{ 0x0056, 0x2f },
	{ 0x0042, 0x30 },
	{ 0x004e, 0x31 },
	{ 0x004d, 0x32 },
	{ 0x003b, 0x33 },
	{ 0x003a, 0x34 },
	{ 0x005f, 0x35 },
	{ 0x002a, 0x37 },
	{ 0x0020, 0x39 },
	{ 0x002d, 0x4a },
	{ 0x002b, 0x4e },
	{ 0x002e, 0x53 },
	{ 0x003e, 0x56 },
	{ 0x007f, 0x63 },
	{ 0x000d, 0x64 },
	{ 0x002f, 0x68 },
	{ 0x002e, 0x7e },
};


const Keylayout_r::KeyLayoutMap_t altGr
{
	{ 0x001b, 0x1 },
	{ 0x007e, 0x2 },
	{ 0x02c7, 0x3 },
	{ 0x005e, 0x4 },
	{ 0x02d8, 0x5 },
	{ 0x00b0, 0x6 },
	{ 0x02db, 0x7 },
	{ 0x0060, 0x8 },
	{ 0x02d9, 0x9 },
	{ 0x00b4, 0xa },
	{ 0x02dd, 0xb },
	{ 0x00a8, 0xc },
	{ 0x00b8, 0xd },
	{ 0x0008, 0xe },
	{ 0x0009, 0xf },
	{ 0x005c, 0x10 },
	{ 0x007c, 0x11 },
	{ 0x20ac, 0x12 },
	{ 0x00f7, 0x1a },
	{ 0x00d7, 0x1b },
	{ 0x000d, 0x1c },
	{ 0x005b, 0x21 },
	{ 0x005d, 0x22 },
	{ 0x0142, 0x25 },
	{ 0x0141, 0x26 },
	{ 0x00df, 0x28 },
	{ 0x00a4, 0x2b },
	{ 0x0040, 0x2f },
	{ 0x007b, 0x30 },
	{ 0x007d, 0x31 },
	{ 0x00a7, 0x32 },
	{ 0x003c, 0x33 },
	{ 0x003e, 0x34 },
	{ 0x002a, 0x37 },
	{ 0x002d, 0x4a },
	{ 0x002b, 0x4e },
	{ 0x002f, 0x62 },
	{ 0x000d, 0x64 },
};


const Keylayout_r::KeyLayoutMap_t shiftAltGr
{
	{ 0x001b, 0x1 },
	{ 0x0008, 0xe },
	{ 0x0009, 0xf },
	{ 0x000d, 0x1c },
	{ 0x002a, 0x37 },
	{ 0x002d, 0x4a },
	{ 0x002b, 0x4e },
	{ 0x002f, 0x62 },
	{ 0x000d, 0x64 },
};


const Keylayout_r::KeyLayoutMap_t capslock_noMod
{
	{ 0x001b, 0x1 },
	{ 0x0031, 0x2 },
	{ 0x0032, 0x3 },
	{ 0x0033, 0x4 },
	{ 0x0034, 0x5 },
	{ 0x0035, 0x6 },
	{ 0x0036, 0x7 },
	{ 0x0037, 0x8 },
	{ 0x0038, 0x9 },
	{ 0x0039, 0xa },
	{ 0x0030, 0xb },
	{ 0x0027, 0xc },
	{ 0x002b, 0xd },
	{ 0x0008, 0xe },
	{ 0x0009, 0xf },
	{ 0x0051, 0x10 },
	{ 0x0057, 0x11 },
	{ 0x0045, 0x12 },
	{ 0x0052, 0x13 },
	{ 0x0054, 0x14 },
	{ 0x005a, 0x15 },
	{ 0x0055, 0x16 },
	{ 0x0049, 0x17 },
	{ 0x004f, 0x18 },
	{ 0x0050, 0x19 },
	{ 0x0160, 0x1a },
	{ 0x0110, 0x1b },
	{ 0x000d, 0x1c },
	{ 0x0041, 0x1e },
	{ 0x0053, 0x1f },
	{ 0x0044, 0x20 },
	{ 0x0046, 0x21 },
	{ 0x0047, 0x22 },
	{ 0x0048, 0x23 },
	{ 0x004a, 0x24 },
	{ 0x004b, 0x25 },
	{ 0x004c, 0x26 },
	{ 0x010c, 0x27 },
	{ 0x0106, 0x28 },
	{ 0x201a, 0x29 },
	{ 0x017d, 0x2b },
	{ 0x0059, 0x2c },
	{ 0x0058, 0x2d },
	{ 0x0043, 0x2e },
	{ 0x0056, 0x2f },
	{ 0x0042, 0x30 },
	{ 0x004e, 0x31 },
	{ 0x004d, 0x32 },
	{ 0x002c, 0x33 },
	{ 0x002e, 0x34 },
	{ 0x002d, 0x35 },
	{ 0x002a, 0x37 },
	{ 0x0020, 0x39 },
	{ 0x002d, 0x4a },
	{ 0x002b, 0x4e },
	{ 0x002e, 0x53 },
	{ 0x003c, 0x56 },
	{ 0x002f, 0x62 },
	{ 0x000d, 0x64 },
	{ 0x002e, 0x7e },
};


const Keylayout_r::KeyLayoutMap_t capslock_shift
{
	{ 0x001b, 0x1 },
	{ 0x0021, 0x2 },
	{ 0x0022, 0x3 },
	{ 0x0023, 0x4 },
	{ 0x0024, 0x5 },
	{ 0x0025, 0x6 },
	{ 0x0026, 0x7 },
	{ 0x002f, 0x8 },
	{ 0x0028, 0x9 },
	{ 0x0029, 0xa },
	{ 0x003d, 0xb },
	{ 0x003f, 0xc },
	{ 0x002a, 0xd },
	{ 0x0008, 0xe },
	{ 0x0009, 0xf },
	{ 0x0071, 0x10 },
	{ 0x0077, 0x11 },
	{ 0x0065, 0x12 },
	{ 0x0072, 0x13 },
	{ 0x0074, 0x14 },
	{ 0x007a, 0x15 },
	{ 0x0075, 0x16 },
	{ 0x0069, 0x17 },
	{ 0x006f, 0x18 },
	{ 0x0070, 0x19 },
	{ 0x0161, 0x1a },
	{ 0x0111, 0x1b },
	{ 0x000d, 0x1c },
	{ 0x0061, 0x1e },
	{ 0x0073, 0x1f },
	{ 0x0064, 0x20 },
	{ 0x0066, 0x21 },
	{ 0x0067, 0x22 },
	{ 0x0068, 0x23 },
	{ 0x006a, 0x24 },
	{ 0x006b, 0x25 },
	{ 0x006c, 0x26 },
	{ 0x010d, 0x27 },
	{ 0x0107, 0x28 },
	{ 0x007e, 0x29 },
	{ 0x017e, 0x2b },
	{ 0x0079, 0x2c },
	{ 0x0078, 0x2d },
	{ 0x0063, 0x2e },
	{ 0x0076, 0x2f },
	{ 0x0062, 0x30 },
	{ 0x006e, 0x31 },
	{ 0x006d, 0x32 },
	{ 0x003b, 0x33 },
	{ 0x003a, 0x34 },
	{ 0x005f, 0x35 },
	{ 0x002a, 0x37 },
	{ 0x0020, 0x39 },
	{ 0x002d, 0x4a },
	{ 0x002b, 0x4e },
	{ 0x002e, 0x53 },
	{ 0x003e, 0x56 },
	{ 0x002f, 0x62 },
	{ 0x000d, 0x64 },
	{ 0x002e, 0x7e },
};


const Keylayout_r::KeyLayoutMap_t capslock_altGr
{
	{ 0x001b, 0x1 },
	{ 0x007e, 0x2 },
	{ 0x02c7, 0x3 },
	{ 0x005e, 0x4 },
	{ 0x02d8, 0x5 },
	{ 0x00b0, 0x6 },
	{ 0x02db, 0x7 },
	{ 0x0060, 0x8 },
	{ 0x02d9, 0x9 },
	{ 0x00b4, 0xa },
	{ 0x02dd, 0xb },
	{ 0x00a8, 0xc },
	{ 0x00b8, 0xd },
	{ 0x0008, 0xe },
	{ 0x0009, 0xf },
	{ 0x005c, 0x10 },
	{ 0x007c, 0x11 },
	{ 0x20ac, 0x12 },
	{ 0x00f7, 0x1a },
	{ 0x00d7, 0x1b },
	{ 0x000d, 0x1c },
	{ 0x005b, 0x21 },
	{ 0x005d, 0x22 },
	{ 0x0142, 0x25 },
	{ 0x0141, 0x26 },
	{ 0x00df, 0x28 },
	{ 0x00a4, 0x2b },
	{ 0x0040, 0x2f },
	{ 0x007b, 0x30 },
	{ 0x007d, 0x31 },
	{ 0x00a7, 0x32 },
	{ 0x003c, 0x33 },
	{ 0x003e, 0x34 },
	{ 0x002a, 0x37 },
	{ 0x002d, 0x4a },
	{ 0x002b, 0x4e },
	{ 0x002f, 0x62 },
	{ 0x000d, 0x64 },
};


const Keylayout_r::KeyLayoutMap_t capslock_shiftAltGr
{
	{ 0x001b, 0x1 },
	{ 0x0008, 0xe },
	{ 0x0009, 0xf },
	{ 0x000d, 0x1c },
	{ 0x002a, 0x37 },
	{ 0x002d, 0x4a },
	{ 0x002b, 0x4e },
	{ 0x002f, 0x62 },
	{ 0x000d, 0x64 },
};


const Keylayout_r::KeyLayoutMap_t ctrl
{
	{ 0x001b, 0x1 },
	{ 0x0008, 0xe },
	{ 0x0009, 0xf },
	{ 0x001b, 0x1a },
	{ 0x001d, 0x1b },
	{ 0x000d, 0x1c },
	{ 0x001c, 0x2b },
	{ 0x002a, 0x37 },
	{ 0x0020, 0x39 },
	{ 0x002d, 0x4a },
	{ 0x002b, 0x4e },
	{ 0x001c, 0x56 },
	{ 0x002f, 0x62 },
	{ 0x000d, 0x64 },
};


const Keylayout_r::KeyLayoutMap_t deadkeys
{
	{ 0x2c7, 0x3},
	{ 0x7a, 0x17e},
	{ 0x43, 0x10c},
	{ 0x6e, 0x148},
	{ 0x6c, 0x13e},
	{ 0x53, 0x160},
	{ 0x52, 0x158},
	{ 0x54, 0x164},
	{ 0x72, 0x159},
	{ 0x44, 0x10e},
	{ 0x73, 0x161},
	{ 0x45, 0x11a},
	{ 0x20, 0x2c7},
	{ 0x64, 0x10f},
	{ 0x65, 0x11b},
	{ 0x63, 0x10d},
	{ 0x5a, 0x17d},
	{ 0x4c, 0x13d},
	{ 0x4e, 0x147},
	{ 0x74, 0x165},
	{ 0x5e, 0x4},
	{ 0x6f, 0xf4},
	{ 0x49, 0xce},
	{ 0x20, 0x5e},
	{ 0x41, 0xc2},
	{ 0x61, 0xe2},
	{ 0x4f, 0xd4},
	{ 0x69, 0xee},
	{ 0x2d8, 0x5},
	{ 0x41, 0x102},
	{ 0x20, 0x2d8},
	{ 0x61, 0x103},
	{ 0xb0, 0x6},
	{ 0x20, 0xb0},
	{ 0x75, 0x16f},
	{ 0x55, 0x16e},
	{ 0x2db, 0x7},
	{ 0x41, 0x104},
	{ 0x20, 0x2db},
	{ 0x65, 0x119},
	{ 0x45, 0x118},
	{ 0x61, 0x105},
	{ 0x2d9, 0x9},
	{ 0x7a, 0x17c},
	{ 0x5a, 0x17b},
	{ 0x20, 0x2d9},
	{ 0xb4, 0xa},
	{ 0x59, 0xdd},
	{ 0x53, 0x15a},
	{ 0x52, 0x154},
	{ 0x55, 0xda},
	{ 0x4c, 0x139},
	{ 0x4e, 0x143},
	{ 0x4f, 0xd3},
	{ 0x7a, 0x17a},
	{ 0x65, 0xe9},
	{ 0x63, 0x107},
	{ 0x61, 0xe1},
	{ 0x69, 0xed},
	{ 0x49, 0xcd},
	{ 0x20, 0xb4},
	{ 0x41, 0xc1},
	{ 0x43, 0x106},
	{ 0x45, 0xc9},
	{ 0x5a, 0x179},
	{ 0x6f, 0xf3},
	{ 0x6e, 0x144},
	{ 0x6c, 0x13a},
	{ 0x73, 0x15b},
	{ 0x72, 0x155},
	{ 0x75, 0xfa},
	{ 0x79, 0xfd},
	{ 0x2dd, 0xb},
	{ 0x6f, 0x151},
	{ 0x20, 0x2dd},
	{ 0x75, 0x171},
	{ 0x4f, 0x150},
	{ 0x55, 0x170},
	{ 0xa8, 0xc},
	{ 0x6f, 0xf6},
	{ 0x20, 0xa8},
	{ 0x41, 0xc4},
	{ 0x45, 0xcb},
	{ 0x55, 0xdc},
	{ 0x65, 0xeb},
	{ 0x75, 0xfc},
	{ 0x61, 0xe4},
	{ 0x4f, 0xd6},
	{ 0xb8, 0xd},
	{ 0x53, 0x15e},
	{ 0x73, 0x15f},
	{ 0x43, 0xc7},
	{ 0x63, 0xe7},
	{ 0x20, 0xb8},
	{ 0x201a, 0x29},
	{ 0x53, 0x15e},
	{ 0x73, 0x15f},
	{ 0x43, 0xc7},
	{ 0x63, 0xe7},
	{ 0x20, 0x201a},
};


const static uint8_t nbDeadkeys = 11;

}

static const Keylayout_r keylayout_x8000081a( x8000081a::LCID
                                 , x8000081a::locale_name
                                 , x8000081a::noMod
                                 , x8000081a::shift
                                 , x8000081a::altGr
                                 , x8000081a::shiftAltGr
                                 , x8000081a::ctrl
                                 , x8000081a::capslock_noMod
                                 , x8000081a::capslock_shift
                                 , x8000081a::capslock_altGr
                                 , x8000081a::capslock_shiftAltGr
                                 , x8000081a::deadkeys
                                 , x8000081a::nbDeadkeys

);

#endif
