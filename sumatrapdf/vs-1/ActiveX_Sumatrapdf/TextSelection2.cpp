/* Copyright 2013 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

#include "BaseUtil.h"
#include "TextSelection2.h"


TextSelection2::TextSelection2(BaseEngine *engine, PageTextCache *textCache) :
    engine(engine), textCache(textCache), startPage(-1),
    endPage(-1), startGlyph(-1), endGlyph(-1)
{
}

TextSelection2::~TextSelection2()
{
    while(ResultVec.Count()>0)
	{
		TextSel2* tmpresult = ResultVec.At(0);
		ResultVec.Remove(tmpresult);
		ResetResult(tmpresult);
		delete tmpresult;
	}
}

void TextSelection2::Reset()
{
    result->len = 0;
    free(result->pages);
    result->pages = NULL;
    free(result->rects);
    result->rects = NULL;

	result->whole = false;
	result->Color = 0;
	str::ReplacePtr(&result->Text,NULL);
}


void TextSelection2::ResetResult(TextSel2* presult)
{
	result->whole = false;
	presult->len = 0;
	if(presult->pages)
		free(presult->pages);
	presult->pages = NULL;
	if(presult->rects)
		free(presult->rects);
	presult->rects = NULL;

	presult->Color = 0;
	str::ReplacePtr(&presult->Text,NULL);
}

// returns the index of the glyph closest to the right of the given coordinates
// (i.e. when over the right half of a glyph, the returned index will be for the
// glyph following it, which will be the first glyph (not) to be selected)
int TextSelection2::FindClosestGlyph(int pageNo, double x, double y)
{
    int textLen;
    RectI *coords;
    textCache->GetData(pageNo, &textLen, &coords);
    PointD pt = PointD(x, y);

    unsigned int maxDist = UINT_MAX;
    PointI pti = pt.Convert<int>();
    bool overGlyph = false;
    int result = -1;

    for (int i = 0; i < textLen; i++) {
        if (!coords[i].x && !coords[i].dx)
            continue;
        if (overGlyph && !coords[i].Contains(pti))
            continue;

        unsigned int dist = distSq((int)x - coords[i].x - coords[i].dx / 2,
                                   (int)y - coords[i].y - coords[i].dy / 2);
        if (dist < maxDist) {
            result = i;
            maxDist = dist;
        }
        // prefer glyphs the cursor is actually over
        if (!overGlyph && coords[i].Contains(pti)) {
            overGlyph = true;
            result = i;
            maxDist = dist;
        }
    }

    if (-1 == result)
        return 0;
    CrashIf(result < 0 || result >= textLen);

    // the result indexes the first glyph to be selected in a forward selection
    RectD bbox = engine->Transform(coords[result].Convert<double>(), pageNo, 1.0, 0);
    pt = engine->Transform(pt, pageNo, 1.0, 0);
    if (pt.x > bbox.x + 0.5 * bbox.dx) {
        result++;
        // for some (DjVu) documents, all glyphs of a word share the same bbox
        while (result < textLen && coords[result - 1] == coords[result])
            result++;
    }
    CrashIf(result > 0 && result < textLen && coords[result] == coords[result - 1]);

    return result;
}

void TextSelection2::FillResultRects(int pageNo, int glyph, int length, WStrVec *lines)
{
    int len;
    RectI *coords;
    const WCHAR *text = textCache->GetData(pageNo, &len, &coords);
    CrashIf(len < glyph + length);
    RectI mediabox = engine->PageMediabox(pageNo).Round();
    RectI *c = &coords[glyph], *end = c + length;
    while (c < end) {
        // skip line breaks
        for (; c < end && !c->x && !c->dx; c++);

        RectI bbox, *c0 = c;
        for (; c < end && (c->x || c->dx); c++) {
            bbox = bbox.Union(*c);
        }
        bbox = bbox.Intersect(mediabox);
        // skip text that's completely outside a page's mediabox
        if (bbox.IsEmpty())
            continue;

        if (lines) {
            lines->Push(str::DupN(text + (c0 - coords), c - c0));
            continue;
        }

        // cut the right edge, if it overlaps the next character
        if (c < coords + len && (c->x || c->dx) && bbox.x < c->x && bbox.x + bbox.dx > c->x)
            bbox.dx = c->x - bbox.x;

        result->len++;
        int *newPages = (int *)realloc(result->pages, sizeof(int) * result->len);
        CrashIf(!newPages); // TODO: use infallible realloc
        result->pages = newPages;
        result->pages[result->len - 1] = pageNo;
        RectI *newRects = (RectI *)realloc(result->rects, sizeof(RectI) * result->len);
        CrashIf(!newRects); // TODO: use infallible realloc
        result->rects = newRects;
        result->rects[result->len - 1] = bbox;

	}
}

//bool TextSelection2::IsOverGlyph(int pageNo, double x, double y)
//{
//    int textLen;
//    RectI *coords;
//    textCache->GetData(pageNo, &textLen, &coords);
//
//    int glyphIx = FindClosestGlyph(pageNo, x, y);
//    PointI pt = PointD(x, y).Convert<int>();
//    // when over the right half of a glyph, FindClosestGlyph returns the
//    // index of the next glyph, in which case glyphIx must be decremented
//    if (glyphIx == textLen || !coords[glyphIx].Contains(pt))
//        glyphIx--;
//    if (-1 == glyphIx)
//        return false;
//    return coords[glyphIx].Contains(pt);
//}

void TextSelection2::StartAt(int pageNo, int glyphIx)
{
    startPage = pageNo;
    startGlyph = glyphIx;
    if (glyphIx < 0) {
        int textLen;
        textCache->GetData(pageNo, &textLen);
        startGlyph += textLen + 1;
    }
}

void TextSelection2::SelectUpTo(int pageNo, int glyphIx)
{
    if (startPage == -1 || startGlyph == -1)
        return;

    endPage = pageNo;
    endGlyph = glyphIx;
    if (glyphIx < 0) {
        int textLen;
        textCache->GetData(pageNo, &textLen);
        endGlyph = textLen + glyphIx + 1;
    }

    //result.len = 0;
    int fromPage = min(startPage, endPage), toPage = max(startPage, endPage);
    int fromGlyph = (fromPage == endPage ? endGlyph : startGlyph);
    int toGlyph = (fromPage == endPage ? startGlyph : endGlyph);
    if (fromPage == toPage && fromGlyph > toGlyph)
        Swap(fromGlyph, toGlyph);

    for (int page = fromPage; page <= toPage; page++) {
        int textLen;
        textCache->GetData(page, &textLen);

        int glyph = page == fromPage ? fromGlyph : 0;
        int length = (page == toPage ? toGlyph : textLen) - glyph;
        if (length > 0)
            FillResultRects(page, glyph, length);
    }
}

//void TextSelection2::SelectWordAt(int pageNo, double x, double y)
//{
//    int ix = FindClosestGlyph(pageNo, x, y);
//    int textLen;
//    const WCHAR *text = textCache->GetData(pageNo, &textLen);
//
//    for (; ix > 0; ix--)
//        if (!iswordchar(text[ix - 1]))
//            break;
//    StartAt(pageNo, ix);
//
//    for (; ix < textLen; ix++)
//        if (!iswordchar(text[ix]))
//            break;
//    SelectUpTo(pageNo, ix);
//}

//void TextSelection2::CopySelection(TextSelection2 *orig)
//{
//    Reset();
//    StartAt(orig->startPage, orig->startGlyph);
//    SelectUpTo(orig->endPage, orig->endGlyph);
//}

//WCHAR *TextSelection2::ExtractText(WCHAR *lineSep)
//{
//    WStrVec lines;
//
//    int fromPage, fromGlyph, toPage, toGlyph;
//    GetGlyphRange(&fromPage, &fromGlyph, &toPage, &toGlyph);
//
//    for (int page = fromPage; page <= toPage; page++) {
//        int textLen;
//        textCache->GetData(page, &textLen);
//        int glyph = page == fromPage ? fromGlyph : 0;
//        int length = (page == toPage ? toGlyph : textLen) - glyph;
//        if (length > 0)
//            FillResultRects(page, glyph, length, &lines);
//    }
//
//    return lines.Join(lineSep);
//}

//void TextSelection2::GetGlyphRange(int *fromPage, int *fromGlyph, int *toPage, int *toGlyph) const
//{
//    *fromPage = min(startPage, endPage);
//    *toPage = max(startPage, endPage);
//    *fromGlyph = (*fromPage == endPage ? endGlyph : startGlyph);
//    *toGlyph = (*fromPage == endPage ? startGlyph : endGlyph);
//    if (*fromPage == *toPage && *fromGlyph > *toGlyph)
//        Swap(*fromGlyph, *toGlyph);
//}
