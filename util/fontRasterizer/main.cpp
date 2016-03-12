#include <cstdint>
#include <cstdio>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#define arrayLength(array) sizeof(array) / sizeof(array[0])

typedef int8_t i8;
typedef uint8_t u8;
typedef int32_t i32;
typedef uint32_t u32;

struct GlyphMetrics
{
	i32 offsetTop, offsetLeft;
	u32 advanceX;
};

struct AsciiFont
{
	u32 bitmapWidth, bitmapHeight;
	u32 advanceY;
	GlyphMetrics glyphMetrics[256];
};

const size_t ttfFileBufferSize = 1024 * 1024;
unsigned char ttfFileBuffer[ttfFileBufferSize];

AsciiFont asciiFont;

bool readTtfFile(const char *fileName, size_t& fileSize)
{
	auto ttfFile = fopen(fileName, "rb");
	if (!ttfFile)
	{
		perror("ERROR: unable to open TTF file");
		return false;
	}

	bool result = true;

	if (ferror(ttfFile))
	{
		perror("ERROR: unable to read TTF file");
		result = false;
		goto closeFile;
	}

	fileSize = fread(ttfFileBuffer, 1, ttfFileBufferSize, ttfFile);
	if (!feof(ttfFile))
	{
		fputs("ERROR: not enough space to read TTF file. Allocate more memory to ttfFileBuffer\n", stderr);
		result = false;
		goto closeFile;
	}

	if (fclose(ttfFile))
	{
		fputs("WARNING: failed to close TTF file\n", stderr);
		result = false;
		goto closeFile;
	}

closeFile:
	fclose(ttfFile);
	return result;
}

#if 0
// do not delete this function - it is handy for debugging
void writePgmFile(const char *fileName, size_t imageWidth, size_t imageHeight, u8* image)
{
	auto outFile = fopen(fileName, "wb");
	fprintf(outFile, "P5\n%zu %zu\n255\n", imageWidth, imageHeight);
	fwrite(image, 1, imageWidth * imageHeight, outFile);
	fclose(outFile);
}
#endif

inline u32 roundUpPowerOf2(u32 a)
{
	// Thanks to the Bit Twiddling Hacks page for this:
	// http://graphics.stanford.edu/~seander/bithacks.html#IntegerLogDeBruijn 
	--a;
	a |= a >> 1;
	a |= a >> 2;
	a |= a >> 4;
	a |= a >> 8;
	a |= a >> 16;
	return a + 1;
}

int main(int argc, char **argv)
{
	if (argc != 3)
	{
		puts("Usage: rasterize-font <ttf-file-name> <out-file-name>");
		return 0;
	}

	auto ttfFileName = argv[1];
	auto outFileName = argv[2];

	size_t ttfFileSize;
	if (!readTtfFile(ttfFileName, ttfFileSize))
	{
		return 1;
	}

	u32 pixelsPerInch = 96;
	u32 fontPoint = 12;
	u32 fontPointsPerInch = 72;
	u32 bitmapHeightPx = roundUpPowerOf2(pixelsPerInch * fontPoint / fontPointsPerInch);
//TODO use font metrics to compute a tighter bound on the width
	u32 bitmapWidthPx = bitmapHeightPx;
	auto bitmapSizePx = bitmapWidthPx * bitmapHeightPx;

	asciiFont.bitmapWidth = bitmapWidthPx;
	asciiFont.bitmapHeight = bitmapHeightPx;

	stbtt_fontinfo font;
	if (!stbtt_InitFont(&font, ttfFileBuffer, 0))
	{
		fputs("Failed to parse TTF file\n", stderr);
		return 1;
	}

	auto bitmapStorageSize = 256 * bitmapSizePx;
	auto bitmapStorage = (u8*) calloc(bitmapStorageSize, 1);
	if (bitmapStorage == nullptr)
	{
		fputs("Not enough memory to store rendered bitmaps\n", stderr);
		return 1;
	}

	auto scale = stbtt_ScaleForPixelHeight(&font, (float) bitmapHeightPx);
	int ascentUnscaled, descentUnscaled, lineGapUnscaled;
	stbtt_GetFontVMetrics(&font, &ascentUnscaled, &descentUnscaled, &lineGapUnscaled);
	asciiFont.advanceY = (u32) round(((float) ascentUnscaled - descentUnscaled + lineGapUnscaled) * scale);

	auto nextBitmap = bitmapStorage;
	u8 c = 0;
	for (;;)
	{
		auto glyphIndex = stbtt_FindGlyphIndex(&font, c);

		stbtt_MakeGlyphBitmap(
			&font,
			nextBitmap,
			bitmapWidthPx, bitmapHeightPx,
			bitmapWidthPx,
			scale, scale,
			glyphIndex);

		int advanceXUnscaled, leftOffsetUnscaled;
		stbtt_GetGlyphHMetrics(&font, glyphIndex, &advanceXUnscaled, &leftOffsetUnscaled);

		auto advanceWidth = (u32) round((float) advanceXUnscaled * scale);
		auto offsetLeft = (i32) round((float) leftOffsetUnscaled * scale);

		i32 offsetTop;
		stbtt_GetGlyphBitmapBox(
			&font, glyphIndex,
			scale, scale,
			nullptr, &offsetTop, nullptr, nullptr);

		asciiFont.glyphMetrics[c].offsetTop = offsetTop;
		asciiFont.glyphMetrics[c].offsetLeft = offsetLeft;
		asciiFont.glyphMetrics[c].advanceX = advanceWidth;

		nextBitmap += bitmapSizePx;

		if (c == 255)
		{
			break;
		}
		++c;
	}

	int result = 0;

//TODO file portability: endianness, prohibit padding in AsciiFont struct
	auto outFile = fopen(outFileName, "wb");
	if (outFile)
	{
		fwrite(&asciiFont, sizeof(asciiFont), 1, outFile);
		fwrite(bitmapStorage, 1, bitmapStorageSize, outFile);

		if (ferror(outFile))
		{
			perror("ERROR: failed to write output file");
			result = 1;
		}

		if (fclose(outFile) != 0)
		{
			perror("WARNING: failed to close output file");
		}
	} else
	{
		perror("ERROR: could not open output file");
		result = 1;
	}

	free(bitmapStorage);

	return result;
}
