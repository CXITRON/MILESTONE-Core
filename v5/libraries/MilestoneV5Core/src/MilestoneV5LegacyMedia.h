#pragma once

// Reuse the validated MSM1 parser shared with the proven v3 MEDIA firmware.
// Storage and rendering are board-specific; the file contract remains one
// implementation so a browser-produced MSM1 file behaves identically.
#define MILESTONE_MEDIA_MAX_FILE_BYTES (4U * 1024U * 1024U)
#define MILESTONE_MEDIA_MAX_FRAMES 4096U
#include "../../../../CoreMedia.h"
