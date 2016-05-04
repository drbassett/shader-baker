#pragma once

struct MemoryStack
{
	u8 *begin, *end, *top;
};

struct LinkedMemoryStack
{
	MemoryStack stack;
	LinkedMemoryStack *next;
};

struct MemoryStackMarker
{
	// this field cannot be a pointer since memory stacks can be reallocated
	size_t index;
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

struct InfoLogTextChunk
{
	u32 count;
	InfoLogTextChunk *next;
	char text[1024];
};

struct InfoLogErrors
{
	InfoLogTextChunk
		*vertShaderErrors,
		*fragShaderErrors,
		*programErrors,
		*freeList;
};

struct ApplicationState
{
	MemoryStack permMemory;
	LinkedMemoryStack scratchMemory;

	AsciiFont font;

	TextRenderConfig textRenderConfig;

	bool loadUserRenderConfig;
	FilePath userFragShaderPath, userVertShaderPath;
	UserRenderConfig userRenderConfig;

	InfoLogErrors infoLogErrors;

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

