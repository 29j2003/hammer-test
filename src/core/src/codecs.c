/* codecs.c - Builds stb_vorbis (Ogg Vorbis, public domain / MIT) for audio.cpp.
 * Fetched pinned to a commit and checked by hash (cmake/Dependencies.cmake).
 * minimp3 is built in codecs_mp3.c (both define helpers with the same names). */

#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#include "stb_vorbis.c"
