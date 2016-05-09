#pragma once

struct MemStack
{
	u8 *begin, *top, *end;
};

struct MemStackMarker
{
	u8 *p;
};

struct StringSlice
{
	char *begin, *end;
};

/// Represents a string with a size and characters packed together in memory contiguously
struct PackedString
{
	void *ptr;
};

struct TextLine
{
	i32 leftEdge, baseline;
	StringSlice text;
};

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

struct FillRectRenderConfig
{
	GLuint vao;
	GLuint program;
	GLint unifCorners, unifColor;
};

struct TextRenderConfig
{
	GLuint texture;
	GLuint textureSampler;
	GLint textureUnit;

	GLuint vao;
	GLuint charDataBuffer;

	GLuint program;
	GLint unifViewportSizePx, unifCharacterSizePx, unifCharacterSampler;
	GLint attribLowerLeft, attribCharacterIndex;
};

struct UserRenderConfig
{
	GLuint vao;
	GLint vertShader, fragShader;
	GLuint program;
};

struct MicroSeconds
{
	u64 value;
};

struct FilePath
{
	StringSlice path;
};

struct Vec2I32
{
	i32 x, y;
};

struct RectI32
{
	Vec2I32 min, max;
};

