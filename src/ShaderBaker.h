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

struct PreviewRenderConfig
{
	GLuint vao;
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

	PreviewRenderConfig previewRenderConfig;

	char *keyBuffer;
	size_t keyBufferLength;

	unsigned windowWidth, windowHeight;

//TODO put this in the permanent memory
	char commandLine[256];
	size_t commandLineLength, commandLineCapacity;

	MicroSeconds currentTime;

	bool loadProject;
	Project project;
//TODO put this in the permanent memory
	char previewProgramNameStorage[256];
	StringSlice previewProgramName;
//TODO put this in the permanent memory
	char projectPathStorage[256];
	FilePath projectPath;
//TODO concatenate these error types at project load time
	StringSlice readProjectFileError;
	void *projectErrorStrings;
	u32 projectErrorStringCount;
	PackedString previewProgramErrors;
};

