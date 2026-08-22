// Compile-time stubs keep CORE and MEDIA free of artwork networking/JPEG code.
void processNowArtwork() {}
bool nowArtworkNeedsNetwork() { return false; }
bool nowArtworkTransferBusy() { return false; }
bool nowArtworkAvailable() { return false; }
const uint8_t *nowArtworkSmallBitmap() { return nullptr; }
const uint8_t *nowArtworkLargeBitmap() { return nullptr; }
const char *nowArtworkStatus() { return "unsupported"; }
const char *nowArtworkInterruptedStageName() { return ""; }
void consumeNowArtworkResetBreadcrumb() {}
