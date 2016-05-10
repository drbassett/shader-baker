#pragma once

#include "Types.h"
#include "Platform.h"
#include "Common.cpp"
#include "Project.cpp"
#include <gl/gl.h>
#include "../include/glcorearb.h"
#include "generated/glFunctions.cpp"

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

struct Vec2I32
{
	i32 x, y;
};

struct RectI32
{
	Vec2I32 min, max;
};

struct ApplicationState
{
	MemStack permMem, scratchMem;

	AsciiFont font;

	FillRectRenderConfig fillRectRenderConfig;
	TextRenderConfig textRenderConfig;

	UserRenderConfig userRenderConfig;

	char *keyBuffer;
	size_t keyBufferLength;

	unsigned windowWidth, windowHeight;

	char commandLine[256];
	size_t commandLineLength, commandLineCapacity;

	MicroSeconds currentTime;

	bool loadProject;
	FilePath projectPath;
	Project project;
	StringSlice previewProgramName;
//TODO concatenate these error types at project load time
	StringSlice readProjectFileError;
	void *projectErrorStrings;
	u32 projectErrorStringCount;
	StringSlice vertShaderErrors;
	StringSlice fragShaderErrors;
	StringSlice programErrors;
};

