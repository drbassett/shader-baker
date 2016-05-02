#pragma once

struct MemoryStack
{
	u8 *begin, *end, *top;
};

struct MemoryStackMarker
{
	u8 *begin;
};

struct StringSlice
{
	char *begin, *end;
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

struct ApplicationState
{
	MemoryStack scratchMemory;

	AsciiFont font;

	TextRenderConfig textRenderConfig;

	FilePath userFragShaderPath, userVertShaderPath;
	UserRenderConfig userRenderConfig;

	char *keyBuffer;
	size_t keyBufferLength;

	unsigned windowWidth, windowHeight;

	size_t textLineCount;
	TextLine* textLines;

	char commandLine[256];
	size_t commandLineLength, commandLineCapacity;

	MicroSeconds currentTime;
};

struct Vec2I32
{
	i32 x, y;
};

struct RectI32
{
	Vec2I32 min, max;
};

