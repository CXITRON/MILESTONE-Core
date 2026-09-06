#pragma once

// Reuse the validated MSM1 parser shared with the proven v3 MEDIA firmware.
// Storage and rendering are board-specific; the file contract remains one
// implementation so a browser-produced MSM1 file behaves identically.
#include "../../../../CoreMedia.h"
