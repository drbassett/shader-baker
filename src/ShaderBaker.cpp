#include <cstdio>
#include <gl/gl.h>
#include "../include/glcorearb.h"
#include "generated/glFunctions.cpp"

#define arrayLength(array) sizeof(array) / sizeof(array[0])

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

struct SimpleRenderConfig
{
	GLuint program;
	GLuint vao;
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

struct MicroSeconds
{
	u64 value;
};

struct ApplicationState
{
	AsciiFont font;

	SimpleRenderConfig simpleRenderConfig;
	TextRenderConfig textRenderConfig;

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

inline void assert(bool condition)
{
	if (!condition)
	{
		*((char*) nullptr);
	}
}

inline size_t stringSliceLength(StringSlice str)
{
	return str.end - str.begin;
}

inline i32 rectWidth(RectI32 const& rect)
{
	return rect.max.x - rect.min.x;
}

inline i32 rectHeight(RectI32 const& rect)
{
	return rect.max.y - rect.min.y;
}

static bool compileShaderChecked(
	GLuint shader,
	const char* source,
	GLsizei maxLogLength,
	GLchar *log,
	GLsizei& logLength)
{
	glShaderSource(shader, 1, &source, 0);
	glCompileShader(shader);

	GLint compileStatus;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &compileStatus);
	if (compileStatus == GL_TRUE)
	{
		return true;
	}

	// GLint infoLogLength;
	// glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLogLength);

	glGetShaderInfoLog(shader, maxLogLength, &logLength, log);

	return false;
}

static bool linkProgramChecked(
	GLuint program,
	GLsizei maxLogLength,
	GLchar *log,
	GLsizei& logLength)
{
	glLinkProgram(program);

	GLint linkStatus;
	glGetProgramiv(program, GL_LINK_STATUS, &linkStatus);
	if (linkStatus == GL_TRUE)
	{
		return true;
	}

	// GLint infoLogLength;
	// glGetProgramiv(program, GL_INFO_LOG_LENGTH, &infoLogLength);

	glGetProgramInfoLog(program, maxLogLength, &logLength, log);

	return false;
}

inline bool createSimpleProgram(GLsizei maxLogLength, GLchar* infoLog, GLuint program)
{
	const char* vsSource = R"(
		#version 330

		void main()
		{
			gl_Position = vec4(0.0, 0.0, 0.0, 1.0);
		}
	)";

	const char* fsSource = R"(
		#version 330

		out vec4 color;

		void main()
		{
			color = vec4(0.8, 0.0, 0.0, 1.0);
		}
	)";

	bool success = true;
	GLsizei logLength;

	auto vs = glCreateShader(GL_VERTEX_SHADER);
	auto fs = glCreateShader(GL_FRAGMENT_SHADER);
	
	if (!compileShaderChecked(vs, vsSource, maxLogLength, infoLog, logLength))
	{
		goto resultFail;
	}

	if (!compileShaderChecked(fs, fsSource, maxLogLength, infoLog, logLength))
	{
		goto resultFail;
	}

	glAttachShader(program, vs);
	glAttachShader(program, fs);
	if (!linkProgramChecked(program, maxLogLength, infoLog, logLength))
	{
		goto resultFail;
	}
	glDetachShader(program, vs);
	glDetachShader(program, fs);

	goto resultSuccess;

resultFail:
	success = false;
resultSuccess:
	glDeleteShader(vs);
	glDeleteShader(fs);

	return success;
}

inline bool createTextRenderingProgram(GLsizei maxLogLength, GLchar* infoLog, GLuint program)
{
	const char* vsSource = R"(
		#version 330

		uniform vec2 viewportSizePx;

		layout(location = 0) in uvec2 topLeft;
		layout(location = 1) in uint characterIndex;

		flat out uint vsCharacter;

		void main()
		{
			vsCharacter = characterIndex;
			gl_Position.xy = 2.0f * topLeft / viewportSizePx - 1.0f;
			gl_Position.z = 0.0;
			gl_Position.w = 1.0;
		}
	)";

	const char* gsSource = R"(
		#version 330

		layout(points) in;
		layout(triangle_strip, max_vertices = 4) out;

		uniform vec2 characterSizePx;
		uniform vec2 viewportSizePx;

		flat in uint vsCharacter[];

		flat out uint gsCharacter;
		out vec2 texCoord;

		void main()
		{
			vec2 topLeftNdc = gl_in[0].gl_Position.xy;
			vec2 characterSizeNdc = 2.0 * characterSizePx / viewportSizePx;

			gsCharacter = vsCharacter[0];
			gl_Position.z = 0.0;
			gl_Position.w = 1.0;

			float minX = topLeftNdc.x;
			float maxX = minX + characterSizeNdc.x;
			float maxY = topLeftNdc.y;
			float minY = maxY - characterSizeNdc.y;

			// upper-left corner
			gl_Position.xy = vec2(minX, maxY);
			texCoord = vec2(0.0, 0.0);
			EmitVertex();

			// lower-left corner
			gl_Position.xy = vec2(minX, minY);
			texCoord = vec2(0.0, 1.0);
			EmitVertex();

			// upper-right corner
			gl_Position.xy = vec2(maxX, maxY);
			texCoord = vec2(1.0, 0.0);
			EmitVertex();

			// lower-right corner
			gl_Position.xy = vec2(maxX, minY);
			texCoord = vec2(1.0, 1.0);
			EmitVertex();

			EndPrimitive();
		}
	)";

	const char* fsSource = R"(
		#version 330

		uniform sampler2DArray characterSampler;

		flat in uint gsCharacter;
		in vec2 texCoord;

		out vec4 color;

		void main()
		{
			float alpha = texture(characterSampler, vec3(texCoord, gsCharacter)).r;
			color = vec4(1.0, 1.0, 1.0, alpha);
		}
	)";

	bool success = true;
	GLsizei logLength;

	auto vs = glCreateShader(GL_VERTEX_SHADER);
	auto gs = glCreateShader(GL_GEOMETRY_SHADER);
	auto fs = glCreateShader(GL_FRAGMENT_SHADER);

	if (!compileShaderChecked(vs, vsSource, maxLogLength, infoLog, logLength))
	{
		goto resultFail;
	}

	if (!compileShaderChecked(gs, gsSource, maxLogLength, infoLog, logLength))
	{
		goto resultFail;
	}

	if (!compileShaderChecked(fs, fsSource, maxLogLength, infoLog, logLength))
	{
		goto resultFail;
	}

	glAttachShader(program, vs);
	glAttachShader(program, gs);
	glAttachShader(program, fs);
	if (!linkProgramChecked(program, maxLogLength, infoLog, logLength))
	{
		goto resultFail;
	}
	glDetachShader(program, vs);
	glDetachShader(program, gs);
	glDetachShader(program, fs);

	goto resultSuccess;

resultFail:
	success = false;
resultSuccess:
	glDeleteShader(vs);
	glDeleteShader(gs);
	glDeleteShader(fs);

	return success;
}

inline bool readFontFile(ApplicationState& appState, const char *fileName)
{
	auto fontFile = fopen(fileName, "rb");
	if (!fontFile)
	{
		perror("ERROR: unable to open font file");
		return false;
	}

	bool result = true;

	fread(&appState.font, sizeof(appState.font), 1, fontFile);
	auto bitmapSize = appState.font.bitmapWidth * appState.font.bitmapHeight;
	auto bitmapStorageSize = bitmapSize * 256;
//TODO consider using a fixed size buffer on the stack and uploading the texture in chunks
	auto bitmapStorage = (u8*) malloc(bitmapStorageSize);
	if (bitmapStorage == nullptr)
	{
		puts("Not enough memory to read font file");
		goto cleanup1;
	}
	fread(bitmapStorage, 1, bitmapStorageSize, fontFile);

	if (ferror(fontFile))
	{
		perror("ERROR: failed to read font file");
		goto cleanup2;
	}

	//NOTE glMapBuffer with GL_PIXEL_UNPACK_BUFFER can be used to updload textures. It probably
	// will not have any advantage in this situtation, however.
	glBindTexture(GL_TEXTURE_2D_ARRAY, appState.textRenderConfig.texture);
	glTexStorage3D(GL_TEXTURE_2D_ARRAY, 1, GL_RGBA8, appState.font.bitmapWidth, appState.font.bitmapHeight, 256);
	glTexSubImage3D(
		GL_TEXTURE_2D_ARRAY,
		0,
		0, 0, 0,
		appState.font.bitmapWidth, appState.font.bitmapHeight, 256,
		GL_RED,
		GL_UNSIGNED_BYTE,
		bitmapStorage);

cleanup2:
	free(bitmapStorage);
cleanup1:
	if (fclose(fontFile) != 0)
	{
		perror("WARNING: failed to close output file");
	}
	return result;
}

void destroyApplication(ApplicationState& appState)
{
	glDeleteProgram(appState.simpleRenderConfig.program);
	glDeleteVertexArrays(1, &appState.simpleRenderConfig.vao);

	glDeleteTextures(1, &appState.textRenderConfig.texture);
	glDeleteSamplers(1, &appState.textRenderConfig.textureSampler);
	glDeleteBuffers(1, &appState.textRenderConfig.charDataBuffer);
	glDeleteVertexArrays(1, &appState.textRenderConfig.vao);
	glDeleteProgram(appState.textRenderConfig.program);
}

bool initApplication(ApplicationState& appState)
{
	glPointSize(10.0f);

	GLchar infoLog[1024];
	GLsizei maxLogLength = 1024;

	appState.simpleRenderConfig.program = glCreateProgram();
	glGenVertexArrays(1, &appState.simpleRenderConfig.vao);

	glGenTextures(1, &appState.textRenderConfig.texture);
	glGenSamplers(1, &appState.textRenderConfig.textureSampler);
	glSamplerParameteri(
		appState.textRenderConfig.textureSampler,
		GL_TEXTURE_MIN_FILTER,
		GL_NEAREST);
	glSamplerParameteri(
		appState.textRenderConfig.textureSampler,
		GL_TEXTURE_MAG_FILTER,
		GL_NEAREST);
	appState.textRenderConfig.textureUnit = 0;

	glGenBuffers(1, &appState.textRenderConfig.charDataBuffer);
	glBindBuffer(GL_ARRAY_BUFFER, appState.textRenderConfig.charDataBuffer);

	appState.textRenderConfig.attribLowerLeft = 0;
	appState.textRenderConfig.attribCharacterIndex = 1;
	glGenVertexArrays(1, &appState.textRenderConfig.vao);
	glBindVertexArray(appState.textRenderConfig.vao);
	auto sizeAttrib0 = sizeof(GLuint) * 2;
	auto sizeAttrib1 = sizeof(GLuint);
	auto stride = (GLsizei) (sizeAttrib0 + sizeAttrib1);
	glVertexAttribIPointer(
		appState.textRenderConfig.attribLowerLeft,
		2,
		GL_UNSIGNED_INT,
		stride,
		0);
	glVertexAttribIPointer(
		appState.textRenderConfig.attribCharacterIndex,
		1,
		GL_UNSIGNED_INT,
		stride,
		(GLvoid*) sizeAttrib0);
	glEnableVertexAttribArray(appState.textRenderConfig.attribLowerLeft);
	glEnableVertexAttribArray(appState.textRenderConfig.attribCharacterIndex);

	appState.textRenderConfig.program = glCreateProgram();

	if (!createSimpleProgram(maxLogLength, infoLog, appState.simpleRenderConfig.program))
	{
		goto resultFail;
	}

	if (!createTextRenderingProgram(maxLogLength, infoLog, appState.textRenderConfig.program))
	{
		goto resultFail;
	}

	appState.textRenderConfig.unifViewportSizePx = glGetUniformLocation(
		appState.textRenderConfig.program, "viewportSizePx");
	appState.textRenderConfig.unifCharacterSizePx = glGetUniformLocation(
		appState.textRenderConfig.program, "characterSizePx");
	appState.textRenderConfig.unifCharacterSampler = glGetUniformLocation(
		appState.textRenderConfig.program, "characterSampler");

//TODO replace hard-coded file here
	auto fontFileName = "arial.font";
	if (!readFontFile(appState, fontFileName))
	{
		goto resultFail;
	}

	appState.commandLineLength = 0;
	appState.commandLineCapacity = arrayLength(appState.commandLine);

	return true;

resultFail:
	destroyApplication(appState);
	return false;
}

static bool operator==(StringSlice lhs, const char* rhs)
{
	auto pLhs = lhs.begin;
	auto pRhs = rhs;
	for (;;)
	{
		auto lhsEnd = pLhs == lhs.end;
		auto rhsEnd = *pRhs == '\0';
		if (rhsEnd)
		{
			return lhsEnd;
		}

		if (lhsEnd)
		{
			return false;
		}

		if (*pLhs != *pRhs)
		{
			return false;
		}

		++pLhs;
		++pRhs;
	}
}

static void drawText(ApplicationState& appState)
{
	auto textLines = appState.textLines;
	auto lineCount = appState.textLineCount;

	size_t charCount = 0;
	for (int i = 0; i < lineCount; ++i)
	{
		charCount += stringSliceLength(textLines[i].text);
	}

	auto charDataBufferSize = charCount * sizeof(GLuint) * 3;
	glBindBuffer(GL_ARRAY_BUFFER, appState.textRenderConfig.charDataBuffer);
	glBufferData(GL_ARRAY_BUFFER, charDataBufferSize, 0, GL_STREAM_DRAW);
	auto pCharData = (GLuint*) glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);
	for (int i = 0; i < lineCount; ++i)
	{
		auto line = textLines[i];
		auto cPtr = line.text.begin;
		auto charX = line.leftEdge;

		while (cPtr != line.text.end)
		{
			auto c = *cPtr;
			auto glyphMetrics = appState.font.glyphMetrics[c];

			pCharData[0] = charX + glyphMetrics.offsetLeft;
			pCharData[1] = line.baseline - glyphMetrics.offsetTop;
			pCharData[2] = c;

			charX += glyphMetrics.advanceX;
			++cPtr;
			pCharData += 3;
		}
	}

	if (glUnmapBuffer(GL_ARRAY_BUFFER) == GL_FALSE)
	{
		// Under rare circumstances, glUnmapBuffer will return false, indicating
		// that the buffer is corrupt due to "system-specific reasons". If this
		// happens, skip text rendering for this frame. As long as the frame
		// rate is high enough, this will just cause an imperceptible flicker in
		// all the text to be rendered.
		return;
	}

	glBindBuffer(GL_ARRAY_BUFFER, appState.textRenderConfig.charDataBuffer);
	glBindVertexArray(appState.textRenderConfig.vao);
	glUseProgram(appState.textRenderConfig.program);

	glUniform2f(
		appState.textRenderConfig.unifViewportSizePx,
		(GLfloat) appState.windowWidth,
		(GLfloat) appState.windowHeight);
	glUniform2f(
		appState.textRenderConfig.unifCharacterSizePx,
		(float) appState.font.bitmapWidth,
		(float) appState.font.bitmapHeight);

	glActiveTexture(GL_TEXTURE0 + appState.textRenderConfig.textureUnit);
	glBindTexture(GL_TEXTURE_2D_ARRAY, appState.textRenderConfig.texture);
	glBindSampler(
		appState.textRenderConfig.textureUnit,
		appState.textRenderConfig.textureSampler);
	glUniform1i(
		appState.textRenderConfig.unifCharacterSampler,
		appState.textRenderConfig.textureUnit);

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	glDrawArrays(GL_POINTS, 0, (GLsizei) charCount);

	glDisable(GL_BLEND);
}

inline void fillRectangle(RectI32 const& rect, float color[4])
{
	glScissor(rect.min.x, rect.min.y, rectWidth(rect), rectHeight(rect));
	glClearBufferfv(GL_COLOR, 0, color);
}

static inline void processCommand(ApplicationState& appState)
{
	auto command = StringSlice{
		appState.commandLine,
		appState.commandLine + appState.commandLineLength};
	appState.commandLineLength = 0;

	if (command == "set-color")
	{
		glClearColor(0.2f, 0.15f, 0.15f, 1.0f);
	} else if (command == "set-point-size")
	{
		glPointSize(15.0f);
	} else
	{
//TODO handle unknown command
	}
}

static inline void processKeyBuffer(ApplicationState& appState)
{
	auto pKey = appState.keyBuffer;
	auto pEnd = appState.keyBuffer + appState.keyBufferLength;

	while (pKey != pEnd)
	{
		auto key = *pKey;

		switch (key)
		{
		case '\b':
		{
			if (appState.commandLineLength > 0)
			{
				--appState.commandLineLength;
			}
		} break;
		case '\r':
		{
			processCommand(appState);
		} break;
		default:
		{
			if (appState.commandLineLength < appState.commandLineCapacity)
			{
				appState.commandLine[appState.commandLineLength] = key;
				++appState.commandLineLength;
			}
		} break;
		}

		++pKey;
	}
}

void updateApplication(ApplicationState& appState)
{
	processKeyBuffer(appState);

	auto windowWidth = (i32) appState.windowWidth;
	auto windowHeight = (i32) appState.windowHeight;

	TextLine textLines[1];

	textLines[0] = {};
	textLines[0].leftEdge = 5;
	textLines[0].baseline = windowHeight - 20;
	textLines[0].text.begin = appState.commandLine;
	textLines[0].text.end = appState.commandLine + appState.commandLineLength;

	appState.textLineCount = arrayLength(textLines);
	appState.textLines = textLines;

	i32 commandInputAreaHeight = 30;
	i32 commandInputAreaBottom = windowHeight - commandInputAreaHeight;

	auto commandInputArea = RectI32{
		Vec2I32{0, commandInputAreaBottom},
		Vec2I32{windowWidth, windowHeight}};

	auto previewArea = RectI32{
		Vec2I32{0, 0},
		Vec2I32{windowWidth, commandInputAreaBottom}};

	float cornflowerBlue[4] = {0.3921568627451f, 0.5843137254902f, 0.9294117647059f, 1.0f};
	float commandAreaColorDark[4] = {0.1f, 0.05f, 0.05f, 1.0f};
	float commandAreaColorLight[4] = {0.2f, 0.1f, 0.1f, 1.0f};

	u64 blinkPeriod = 2000000;
	u64 halfBlinkPeriod = blinkPeriod >> 1;
	bool useDarkCommandAreaColor = appState.currentTime.value % blinkPeriod < halfBlinkPeriod;
	auto commandAreaColor = useDarkCommandAreaColor ? commandAreaColorDark : commandAreaColorLight;

	glEnable(GL_SCISSOR_TEST);
	fillRectangle(previewArea, cornflowerBlue);
	fillRectangle(commandInputArea, commandAreaColor);
	glDisable(GL_SCISSOR_TEST);

	glViewport(0, 0, windowWidth, windowHeight);
	drawText(appState);

	glViewport(
		previewArea.min.x,
		previewArea.min.y,
		rectWidth(previewArea),
		rectHeight(previewArea));
	glBindVertexArray(appState.simpleRenderConfig.vao);
	glUseProgram(appState.simpleRenderConfig.program);
	glDrawArrays(GL_POINTS, 0, 1);
}

