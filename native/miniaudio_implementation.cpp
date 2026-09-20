// Keep miniaudio's implementation in one native translation unit shared by
// the diagnostic probe and ordinary application hosts.
#define MA_NO_FLAC
#define MA_NO_MP3
#define MA_NO_VORBIS
#define MA_NO_OPUS
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
