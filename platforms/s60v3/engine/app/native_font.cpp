#include <e32base.h>
#include <fbs.h>
#include <bitdev.h>
#include <bitstd.h>
#include <openfont.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../source/cpymo-backends/sdl1/cpymo_s60_native_font.h"
#include "../source/cpymo-backends/sdl1/cpymo_s60_diagnostics.h"

namespace {
const TInt KTile = 64;
const TInt KOriginX = 12;
const TInt KOriginY = 44;
const int KCacheCount = 64;
const unsigned KUnassigned = 0xfa6e;

struct CachedGlyph {
    unsigned codepoint;
    int size;
    bool available;
    cpymo_s60_native_glyph glyph;
};
CachedGlyph cache[KCacheCount];
int next_slot;
bool session_connected;
CFbsBitmap *bitmap;
CFbsBitmapDevice *device;
CFbsBitGc *gc;
CFont *font;
TInt font_size;
TBuf<64> face_name;
unsigned char missing_mask[KTile * KTile];
TInt missing_advance;
bool missing_mask_valid;

void select_face()
{
    // Match PyMO's order for the Chinese typefaces supplied by the handset.
    const TPtrC preferred[] = { _L("936"), _L("GB"), _L("UNIT_HKSCS_S60") };
    const TInt count = device->NumTypefaces();
    for (unsigned i = 0; i < sizeof(preferred) / sizeof(preferred[0]); ++i) {
        for (TInt j = 0; j < count; ++j) {
            TTypefaceSupport support;
            device->TypefaceSupport(support, j);
            const TDesC &name = support.iTypeface.iName;
            if (name.FindF(preferred[i]) != KErrNotFound) {
                face_name.Copy(name.Left(face_name.MaxLength()));
                return;
            }
        }
    }
    if (count > 0) {
        TTypefaceSupport support;
        device->TypefaceSupport(support, 0);
        face_name.Copy(support.iTypeface.iName.Left(face_name.MaxLength()));
    }
}

void open_device_l()
{
    User::LeaveIfError(RFbsSession::Connect());
    session_connected = true;
    bitmap = new(ELeave) CFbsBitmap;
    User::LeaveIfError(bitmap->Create(TSize(KTile,KTile), EGray256));
    device = CFbsBitmapDevice::NewL(bitmap);
    User::LeaveIfError(device->CreateContext(gc));
    select_face();
}

bool draw_mask(unsigned codepoint, unsigned char *pixels, TInt &advance)
{
    TBuf<1> text;
    text.Append(TChar(codepoint));
    advance = font->TextWidthInPixels(text);
    gc->SetBrushColor(KRgbBlack);
    gc->Clear();
    gc->SetPenStyle(CGraphicsContext::ESolidPen);
    gc->SetPenColor(KRgbWhite);
    gc->DrawText(text, TPoint(KOriginX, KOriginY));

    TBuf8<KTile> scan;
    for (TInt y = 0; y < KTile; ++y) {
        bitmap->GetScanLine(scan, TPoint(0, y), KTile, EGray256);
        if (scan.Length() != KTile) return false;
        memcpy(pixels + y * KTile, scan.Ptr(), KTile);
    }
    return true;
}

bool select_size(TInt size)
{
    if (font && font_size == size) return true;
    if (font) {
        gc->DiscardFont();
        device->ReleaseFont(font);
        font = NULL;
    }
    if (device->GetNearestFontToDesignHeightInPixels(font, TFontSpec(face_name, size)) != KErrNone)
        return false;
    font_size = size;
    gc->UseFont(font);
    // Some font servers return data for an unavailable character by drawing
    // the same replacement box used for unassigned Unicode code points.
    missing_mask_valid = draw_mask(KUnassigned, missing_mask, missing_advance);
    return true;
}

bool rasterize(unsigned codepoint, TInt size, cpymo_s60_native_glyph &out)
{
    if (!select_size(size)) return false;
    // CFbsBitmapDevice returns a CFbsFont. Query coverage before DrawText,
    // which can substitute a visible default glyph for a missing character.
    if (!static_cast<CFbsFont *>(font)->HasCharacter(codepoint)) return false;
    TOpenFontCharMetrics metrics;
    const TUint8 *font_bitmap = NULL;
    TSize font_bitmap_size;
    if (font->GetCharacterData(codepoint, metrics, font_bitmap, font_bitmap_size) ==
            CFont::ENoCharacterData) return false;

    // Read pixels through FBS. The font's internal glyph format can vary by ROM.
    unsigned char tile[KTile * KTile];
    if (!draw_mask(codepoint, tile, out.advance) ||
            out.advance < 0 || out.advance > KTile - KOriginX) return false;
    TInt left = KTile, right = 0, top = KTile, bottom = 0;
    for (TInt y = 0; y < KTile; ++y) {
        for (TInt x = 0; x < KTile; ++x) {
            if (!tile[y * KTile + x]) continue;
            if (x < left) left = x;
            if (x + 1 > right) right = x + 1;
            if (y < top) top = y;
            bottom = y + 1;
        }
    }
    if (!right) {
        // A space can legitimately have an advance and an empty mask.
        return codepoint == 0x20 || codepoint == 0xa0 || codepoint == 0x3000;
    }
    if (!left || right == KTile || !top || bottom == KTile) return false;
    if (missing_mask_valid && !memcmp(tile, missing_mask, sizeof(tile))) return false;

    const TInt width = right - left, height = bottom - top;
    unsigned char *pixels = static_cast<unsigned char *>(malloc(width * height));
    if (!pixels) return false;
    for (TInt y = 0; y < height; ++y)
        memcpy(pixels + y * width, tile + (top + y) * KTile + left, width);
    out.left = left - KOriginX;
    out.top = top - KOriginY;
    out.width = width;
    out.height = height;
    out.pixels = pixels;
    return true;
}
}

extern "C" int cpymo_s60_native_font_open(void)
{
    if (gc) return 1;
    TRAPD(error, open_device_l());
    if (error != KErrNone || !select_size(16)) {
        printf("[Font] system font initialization failed (%d).\n", error);
        cpymo_s60_native_font_close();
        return 0;
    }
    if (cpymo_s60_diagnostics_enabled()) {
        TBuf8<64> label;
        label.Copy(face_name);
        printf("[Font] selected Symbian typeface: %.*s\n", label.Length(), label.Ptr());
    }
    return 1;
}

extern "C" void cpymo_s60_native_font_close(void)
{
    for (int i = 0; i < KCacheCount; ++i)
        free(const_cast<unsigned char *>(cache[i].glyph.pixels));
    memset(cache, 0, sizeof(cache));
    next_slot = 0;
    if (font) {
        gc->DiscardFont();
        device->ReleaseFont(font);
        font = NULL;
    }
    delete gc;
    gc = NULL;
    delete device;
    device = NULL;
    delete bitmap;
    bitmap = NULL;
    if (session_connected) {
        RFbsSession::Disconnect();
        session_connected = false;
    }
}

extern "C" int cpymo_s60_native_font_glyph(unsigned codepoint, int size,
    cpymo_s60_native_glyph *out)
{
    if (!gc || !codepoint || codepoint > 0xffff || size < 4 || size > 38) return 0;
    for (int i = 0; i < KCacheCount; ++i) {
        if (cache[i].size == size && cache[i].codepoint == codepoint) {
            if (cache[i].available) *out = cache[i].glyph;
            return cache[i].available;
        }
    }
    CachedGlyph &entry = cache[next_slot];
    next_slot = (next_slot + 1) % KCacheCount;
    free(const_cast<unsigned char *>(entry.glyph.pixels));
    memset(&entry.glyph, 0, sizeof(entry.glyph));
    entry.codepoint = codepoint;
    entry.size = size;
    entry.available = rasterize(codepoint, size, entry.glyph);
    if (entry.available) *out = entry.glyph;
    return entry.available;
}

