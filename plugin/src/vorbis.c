/*
 * Builds the stb_vorbis decoder (third_party/stb, public domain) into the plugin.
 * Only decoding from memory is used.
 */

/* Reported when code is generated, after the warning level is restored below. */
#pragma warning(disable: 4701)

#pragma warning(push, 0)

#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#include "stb/stb_vorbis.c"

#pragma warning(pop)
