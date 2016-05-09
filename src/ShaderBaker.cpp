#include <cassert>
#include <cstdio>
#include <gl/gl.h>
#include "../include/glcorearb.h"
#include "generated/glFunctions.cpp"
#include "Types.h"
#include "ShaderBaker.h"
#include "Platform.h"

#define arrayLength(array) sizeof(array) / sizeof(array[0])
#define memStackPushType(mem, type) (type*) memStackPush(mem, sizeof(type))
#define memStackPushArray(mem, type, size) (type*) memStackPush(mem, (size) * sizeof(type))
#define unreachable() assert(false)

//TODO the include and struct do not belong here. They are here
// temporarily because of a circular dependency.
#include "Project.h"
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
	ProjectErrors projectErrors;
	StringSlice vertShaderErrors;
	StringSlice fragShaderErrors;
	StringSlice programErrors;
};

static bool memStackInit(MemStack& stack, size_t capacity)
{
	assert(capacity > 0);
	
	auto memory = (u8*) PLATFORM_alloc(capacity);
	if (memory == nullptr)
	{
		return false;
	}

	stack.begin = memory;
	stack.top = memory;
	stack.end = memory + capacity;
	return true;
}

//TODO memory alignment
inline void* memStackPush(MemStack& mem, size_t size)
{
	size_t remainingSize = mem.end - mem.top;
//TODO figure out how to "gracefully crash" the application if memory runs out
	assert(size <= remainingSize);
	auto result = mem.top;
 	mem.top += size;
	return result;
}

inline MemStackMarker memStackMark(MemStack const& mem)
{
	return MemStackMarker{mem.top};
}

inline void memStackPop(MemStack& mem, MemStackMarker marker)
{
	assert(marker.p >= mem.begin && marker.p < mem.end);
	mem.top = marker.p;
}

inline void memStackClear(MemStack& mem)
{
	mem.top = mem.begin;
}

/// Finds the length of a C string, excluding the null terminator
/// Examples: "" -> 0, "abc123" -> 6
inline size_t cStringLength(char *c)
{
	auto end = c;
	while (*end != '\0')
	{
		++end;
	}
	return end - c;
}

inline StringSlice stringSliceFromCString(char *cstr)
{
	StringSlice result = {};
	result.begin = cstr;
	for (;;)
	{
		auto c = *cstr;
		if (c == 0)
		{
			break;
		}
		++cstr;
	}
	result.end = cstr;
	return result;
}

inline size_t stringSliceLength(StringSlice str)
{
	return str.end - str.begin;
}

bool operator==(StringSlice lhs, StringSlice rhs)
{
	if (stringSliceLength(lhs) != stringSliceLength(rhs))
	{
		return false;
	}
	while (lhs.begin != lhs.end)
	{
		if (*lhs.begin != *rhs.begin)
		{
			return false;
		}
		++lhs.begin;
		++rhs.begin;
	}
	return true;
}

inline bool operator!=(StringSlice lhs, StringSlice rhs)
{
	return !(lhs == rhs);
}

inline PackedString packString(MemStack& mem, StringSlice str)
{
	size_t stringLength = stringSliceLength(str);
	auto ptr = memStackPush(mem, sizeof(stringLength) + stringLength);
	auto sizePtr = (size_t*) ptr;
	auto charPtr = (char*) (sizePtr + 1);
	*sizePtr = stringLength;
	memcpy(charPtr, str.begin, stringLength);
	return PackedString{ptr};
}

inline StringSlice unpackString(PackedString str)
{
	auto sizePtr = (size_t*) str.ptr;
	auto begin = (char*) (sizePtr + 1);
	auto end = begin + (*sizePtr);
	return StringSlice{begin, end};
}

static void u32ToString(MemStack& mem, u32 value, char*& result, u32& length)
{
	// 10 characters is large enough to hold any 32 bit integer
	result = memStackPushArray(mem, char, 10);
	auto resultEnd = result;
	// This loop always has to execute at least once.
	// Otherwise, nothing gets printed for zero.
	do
	{
		*resultEnd = (value % 10) + '0';
		value /= 10;
		++resultEnd;
	} while (value > 0);
	length = (u32) (resultEnd - result);

	// reverse the string
	u32 lo = 0;
	u32 hi = length - 1;
	while (lo < hi)
	{
		char tmp = result[lo];
		result[lo] = result[hi];
		result[hi] = tmp;
		++lo;
		--hi;
	}

	// "deallocate" the extra space
	mem.top = (u8*) resultEnd;
}

inline i32 rectWidth(RectI32 const& rect)
{
	return rect.max.x - rect.min.x;
}

inline i32 rectHeight(RectI32 const& rect)
{
	return rect.max.y - rect.min.y;
}

static inline bool shaderCompileSuccessful(GLuint shader)
{
	GLint compileStatus;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &compileStatus);
	return compileStatus == GL_TRUE;
}

static bool compileShaderChecked(GLuint shader, const char* source)
{
	glShaderSource(shader, 1, &source, 0);
	glCompileShader(shader);
	return shaderCompileSuccessful(shader);
}

static bool programLinkSuccessful(GLuint program)
{
	GLint linkStatus;
	glGetProgramiv(program, GL_LINK_STATUS, &linkStatus);
	return linkStatus == GL_TRUE;
}

static inline bool initTextRenderingProgram(GLuint program)
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

	auto vs = glCreateShader(GL_VERTEX_SHADER);
	auto gs = glCreateShader(GL_GEOMETRY_SHADER);
	auto fs = glCreateShader(GL_FRAGMENT_SHADER);

	if (!compileShaderChecked(vs, vsSource))
	{
		goto error;
	}

	if (!compileShaderChecked(gs, gsSource))
	{
		goto error;
	}

	if (!compileShaderChecked(fs, fsSource))
	{
		goto error;
	}

	glAttachShader(program, vs);
	glAttachShader(program, gs);
	glAttachShader(program, fs);
	glLinkProgram(program);
	if (!programLinkSuccessful(program))
	{
		goto error;
	}
	glDetachShader(program, vs);
	glDetachShader(program, gs);
	glDetachShader(program, fs);

	bool success = true;
	goto success;

error:
	success = false;
success:
	glDeleteShader(vs);
	glDeleteShader(gs);
	glDeleteShader(fs);

	return success;
}

static inline bool initFillRectProgram(GLuint program)
{
	const char* vsSource = R"(
		#version 330

		uniform vec4 corners;

		void main()
		{
			float minX = corners.x;
			float minY = corners.y;
			float maxX = corners.z;
			float maxY = corners.w;
			switch (gl_VertexID)
			{
			case 0:
				gl_Position.xy = vec2(minX, maxY);
				break;
			case 1:
				gl_Position.xy = vec2(minX, minY);
				break;
			case 2:
				gl_Position.xy = vec2(maxX, maxY);
				break;
			case 3:
				gl_Position.xy = vec2(maxX, minY);
				break;
			}
			
			gl_Position.z = 0.0;
			gl_Position.w = 1.0;
		}
	)";

	const char* fsSource = R"(
		#version 330

		uniform vec4 color;

		out vec4 fragColor;

		void main()
		{
			fragColor = color;
		}
	)";

	auto vs = glCreateShader(GL_VERTEX_SHADER);
	auto fs = glCreateShader(GL_FRAGMENT_SHADER);

	if (!compileShaderChecked(vs, vsSource))
	{
		goto error;
	}

	if (!compileShaderChecked(fs, fsSource))
	{
		goto error;
	}

	glAttachShader(program, vs);
	glAttachShader(program, fs);
	glLinkProgram(program);
	if (!programLinkSuccessful(program))
	{
		goto error;
	}
	glDetachShader(program, vs);
	glDetachShader(program, fs);

	bool success = true;
	goto success;

error:
	success = false;
success:
	glDeleteShader(vs);
	glDeleteShader(fs);

	return success;
}

static inline void initUserRenderConfig(UserRenderConfig& userRenderConfig)
{
	const char* vsSource = R"(
		#version 330

		void main() { }
	)";

	const char* fsSource = R"(
		#version 330

		out vec4 fragColor;

		void main() { }
	)";

	glGenVertexArrays(1, &userRenderConfig.vao);
	userRenderConfig.vertShader = glCreateShader(GL_VERTEX_SHADER);
	userRenderConfig.fragShader = glCreateShader(GL_FRAGMENT_SHADER);
	userRenderConfig.program = glCreateProgram();

	glAttachShader(userRenderConfig.program, userRenderConfig.vertShader);
	glAttachShader(userRenderConfig.program, userRenderConfig.fragShader);

	glShaderSource(userRenderConfig.vertShader, 1, &vsSource, 0);
	glCompileShader(userRenderConfig.vertShader);
	assert(shaderCompileSuccessful(userRenderConfig.vertShader));

	glShaderSource(userRenderConfig.fragShader, 1, &fsSource, 0);
	glCompileShader(userRenderConfig.fragShader);
	assert(shaderCompileSuccessful(userRenderConfig.fragShader));

	glLinkProgram(userRenderConfig.program);
	assert(programLinkSuccessful(userRenderConfig.program));
}

static inline bool readFontFile(
	MemStack& scratchMem, TextRenderConfig& textRenderConfig, AsciiFont& font, const char *fileName)
{
	auto fontFile = fopen(fileName, "rb");
	if (!fontFile)
	{
		perror("ERROR: unable to open font file");
		return false;
	}

	bool success = true;

	auto memMarker = memStackMark(scratchMem);

//TODO replace fread with a platform-specific calls
	fread(&font, sizeof(font), 1, fontFile);
	auto bitmapSize = font.bitmapWidth * font.bitmapHeight;
	auto bitmapStorageSize = bitmapSize * 256;
	auto bitmapStorage = (u8*) memStackPush(scratchMem, bitmapStorageSize);
	if (bitmapStorage == nullptr)
	{
		puts("Not enough memory to read font file");
		success = false;
		goto returnResult;
	}
	fread(bitmapStorage, 1, bitmapStorageSize, fontFile);

	if (ferror(fontFile))
	{
		perror("ERROR: failed to read font file");
		success = false;
		goto returnResult;
	}

	glBindTexture(GL_TEXTURE_2D_ARRAY, textRenderConfig.texture);
	glTexStorage3D(GL_TEXTURE_2D_ARRAY, 1, GL_RGBA8, font.bitmapWidth, font.bitmapHeight, 256);
	glTexSubImage3D(
		GL_TEXTURE_2D_ARRAY,
		0,
		0, 0, 0,
		font.bitmapWidth, font.bitmapHeight, 256,
		GL_RED,
		GL_UNSIGNED_BYTE,
		bitmapStorage);

returnResult:
	if (fclose(fontFile) != 0)
	{
		perror("WARNING: failed to close output file");
	}
	memStackPop(scratchMem, memMarker);
	return success;
}

void destroyApplication(ApplicationState& appState)
{
	glDeleteVertexArrays(1, &appState.fillRectRenderConfig.vao);
	glDeleteProgram(appState.fillRectRenderConfig.program);

	glDeleteTextures(1, &appState.textRenderConfig.texture);
	glDeleteSamplers(1, &appState.textRenderConfig.textureSampler);
	glDeleteBuffers(1, &appState.textRenderConfig.charDataBuffer);
	glDeleteVertexArrays(1, &appState.textRenderConfig.vao);
	glDeleteProgram(appState.textRenderConfig.program);

	glDeleteVertexArrays(1, &appState.userRenderConfig.vao);
	glDeleteShader(appState.userRenderConfig.vertShader);
	glDeleteShader(appState.userRenderConfig.fragShader);
	glDeleteProgram(appState.userRenderConfig.program);
}

static inline size_t megabytes(size_t value)
{
	return value * 1024 * 1024;
}

bool initApplication(ApplicationState& appState)
{
	if (!memStackInit(appState.permMem, megabytes(64)))
	{
		return false;
	}
	if (!memStackInit(appState.scratchMem, megabytes(256)))
	{
		return false;
	}

	glGenVertexArrays(1, &appState.fillRectRenderConfig.vao);
	appState.fillRectRenderConfig.program = glCreateProgram();

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

	initUserRenderConfig(appState.userRenderConfig);

	if (!initFillRectProgram(appState.fillRectRenderConfig.program))
	{
		goto resultFail;
	}

	if (!initTextRenderingProgram(appState.textRenderConfig.program))
	{
		goto resultFail;
	}

	appState.fillRectRenderConfig.unifCorners = glGetUniformLocation(
		appState.fillRectRenderConfig.program, "corners");
	appState.fillRectRenderConfig.unifColor = glGetUniformLocation(
		appState.fillRectRenderConfig.program, "color");

	appState.textRenderConfig.unifViewportSizePx = glGetUniformLocation(
		appState.textRenderConfig.program, "viewportSizePx");
	appState.textRenderConfig.unifCharacterSizePx = glGetUniformLocation(
		appState.textRenderConfig.program, "characterSizePx");
	appState.textRenderConfig.unifCharacterSampler = glGetUniformLocation(
		appState.textRenderConfig.program, "characterSampler");

//TODO replace hard-coded file here
	auto fontFileName = "arial.font";
	if (!readFontFile(appState.scratchMem, appState.textRenderConfig, appState.font, fontFileName))
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

static bool operator!=(StringSlice lhs, const char* rhs)
{
	return !(lhs == rhs);
}

static void drawText(
	TextRenderConfig const& textRenderConfig,
	AsciiFont& font,
	unsigned windowWidth,
	unsigned windowHeight,
	TextLine *textLinesBegin,
	TextLine *textLinesEnd)
{
	size_t charCount = 0;
	auto pTextLine = textLinesBegin;
	while (pTextLine != textLinesEnd)
	{
		charCount += stringSliceLength(pTextLine->text);
		++pTextLine;
	}

	auto charDataBufferSize = charCount * sizeof(GLuint) * 3;
	glBindBuffer(GL_ARRAY_BUFFER, textRenderConfig.charDataBuffer);
	glBufferData(GL_ARRAY_BUFFER, charDataBufferSize, 0, GL_STREAM_DRAW);
	pTextLine = textLinesBegin;
	auto pCharData = (GLuint*) glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);
	while (pTextLine != textLinesEnd)
	{
		auto cPtr = pTextLine->text.begin;
		auto cPtrEnd = pTextLine->text.end;
		auto charX = pTextLine->leftEdge;
		auto baseline = pTextLine->baseline;
		++pTextLine;

		while (cPtr != cPtrEnd)
		{
			auto c = *cPtr;
			auto glyphMetrics = font.glyphMetrics[c];

			pCharData[0] = charX + glyphMetrics.offsetLeft;
			pCharData[1] = baseline - glyphMetrics.offsetTop;
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

	glBindBuffer(GL_ARRAY_BUFFER, textRenderConfig.charDataBuffer);
	glBindVertexArray(textRenderConfig.vao);
	glUseProgram(textRenderConfig.program);

	glUniform2f(
		textRenderConfig.unifViewportSizePx,
		(GLfloat) windowWidth,
		(GLfloat) windowHeight);
	glUniform2f(
		textRenderConfig.unifCharacterSizePx,
		(float) font.bitmapWidth,
		(float) font.bitmapHeight);

	glActiveTexture(GL_TEXTURE0 + textRenderConfig.textureUnit);
	glBindTexture(GL_TEXTURE_2D_ARRAY, textRenderConfig.texture);
	glBindSampler(
		textRenderConfig.textureUnit,
		textRenderConfig.textureSampler);
	glUniform1i(
		textRenderConfig.unifCharacterSampler,
		textRenderConfig.textureUnit);

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	glDrawArrays(GL_POINTS, 0, (GLsizei) charCount);

	glDisable(GL_BLEND);
}

static inline void fillOpaqueRectangle(RectI32 const& rect, float color[4])
{
	glScissor(rect.min.x, rect.min.y, rectWidth(rect), rectHeight(rect));
	glClearBufferfv(GL_COLOR, 0, color);
}

static inline void fillRectangle(
	FillRectRenderConfig const& renderConfig,
	float windowWidth,
	float windowHeight,
	RectI32 const& rect,
	float color[4])
{
	GLfloat corners[4] = {
		2.0f * ((float) rect.min.x / windowWidth - 0.5f),
		2.0f * ((float) rect.min.y / windowHeight - 0.5f),
		2.0f * ((float) rect.max.x / windowWidth - 0.5f),
		2.0f * ((float) rect.max.y / windowHeight - 0.5f),};
	glUniform4fv(renderConfig.unifCorners, 1, corners);
	glUniform4fv(renderConfig.unifColor, 1, color);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
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

StringSlice readShaderLog(MemStack& mem, GLint shader)
{
	GLint logLength;
	glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);

	auto log = memStackPushArray(mem, GLchar, logLength);
	GLsizei readLogLength;
	glGetShaderInfoLog(shader, logLength, &readLogLength, log);
	// pop the null terminator
	mem.top -= sizeof(GLchar);
	return StringSlice{log, log + logLength - 1};
}

StringSlice readProgramLog(MemStack& mem, GLint program)
{
	GLint logLength;
	glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLength);

	auto log = memStackPushArray(mem, GLchar, logLength);
	GLsizei readLogLength;
	glGetProgramInfoLog(program, logLength, &readLogLength, log);
	// pop the null terminator
	mem.top -= sizeof(GLchar);
	return StringSlice{log, log + logLength - 1};
}

void loadUserShader(MemStack& mem, GLint shader, StringSlice source, StringSlice& compileErrors)
{
//TODO there is no guarantee that the shader source will fit in a GLint - bulletproof this
	auto shaderSourceLength = (GLint) stringSliceLength(source);
	glShaderSource(shader, 1, (GLchar**) &source.begin, &shaderSourceLength);
	glCompileShader(shader);
	if (shaderCompileSuccessful(shader))
	{
		compileErrors = {};
	} else
	{
		compileErrors = readShaderLog(mem, shader);
	}
}

void loadProject(MemStack& permMem, MemStack& scratchMem, ApplicationState& app)
{
	memStackClear(permMem);
	app.readProjectFileError = {};
	app.projectErrors = {};
	app.vertShaderErrors = {};
	app.fragShaderErrors = {};
	app.programErrors = {};

	auto memMarker = memStackMark(scratchMem);

	ReadFileError readError;
	u8 *fileContents;
	size_t fileSize;
	PLATFORM_readWholeFile(scratchMem, app.projectPath, readError, fileContents, fileSize);
	if (fileContents == nullptr)
	{
		char *errorString;
		switch (readError)
		{
		case ReadFileError::FileNotFound:
			errorString = "The project file does not exist";
			break;
		case ReadFileError::FileInUse:
			errorString = "The project file is in use by another process";
			break;
		case ReadFileError::AccessDenied:
			errorString =
				"The Operating System denied access to the project file. You may have insufficient \
				permissions, or the file may be pending deletion.";
			break;
		case ReadFileError::Other:
			unreachable();
			errorString = "The project file could not be read";
			break;
		default:
			unreachable();
			errorString = "";
		}

		auto errorStringLength = (GLint) cStringLength(errorString);
		app.readProjectFileError.begin = memStackPushArray(permMem, char, errorStringLength);
		app.readProjectFileError.end = app.readProjectFileError.begin + errorStringLength;
		memcpy(app.readProjectFileError.begin, errorString, errorStringLength);
		goto cleanup;
	}

	{
		StringSlice projectText{(char*) fileContents, (char*) fileContents + fileSize}; 
		app.project = parseProject(scratchMem, permMem, projectText, app.projectErrors);
	}

	if (app.projectErrors.count != 0)
	{
		goto cleanup;
	}

	if (stringSliceLength(app.previewProgramName) == 0)
	{
		goto cleanup;
	}

	Program *previewProgram = nullptr;
	for (u32 i = 0; i < app.project.programCount; ++i)
	{
		auto projectName = app.project.programs[i].name;
		if (unpackString(projectName) == app.previewProgramName)
		{
			previewProgram = app.project.programs + i;
		}
	}
	if (previewProgram == nullptr)
	{
//TODO report error - could not find user program
		goto cleanup;
	}

//TODO this loop is just to get things working for now. It should
// be loading whatever the user gives us, rather than picking an
// arbitrary vertex and fragment shader.
	Shader *vertShader = nullptr;
	Shader *fragShader = nullptr;
	for (u32 i = 0; i < previewProgram->attachedShaderCount; ++i)
	{
		auto pShader = previewProgram->attachedShaders[i];
		if (pShader->type == ShaderType::Vertex)
		{
			vertShader = pShader;
		}
		if (pShader->type == ShaderType::Fragment)
		{
			fragShader = pShader;
		}
	}
	if (vertShader == nullptr || fragShader == nullptr)
	{
		goto cleanup;
	}

	auto vertShaderSource = unpackString(vertShader->source);
	loadUserShader(permMem, app.userRenderConfig.vertShader, vertShaderSource, app.vertShaderErrors);

	auto fragShaderSource = unpackString(fragShader->source);
	loadUserShader(permMem, app.userRenderConfig.fragShader, fragShaderSource, app.fragShaderErrors);

	if (app.vertShaderErrors.begin != nullptr || app.fragShaderErrors.begin != nullptr)
	{
		goto cleanup;
	}
	
	glLinkProgram(app.userRenderConfig.program);
	if (!programLinkSuccessful(app.userRenderConfig.program))
	{
		app.programErrors = readProgramLog(permMem, app.userRenderConfig.program);
	}

cleanup:
	memStackPop(scratchMem, memMarker);
}

void pushSingleTextLine(MemStack& mem, StringSlice str)
{
	auto textLine = memStackPushType(mem, TextLine);
	textLine->text = str;
}

void pushMultiTextLine(MemStack& mem, StringSlice str)
{
	auto lineBegin = str.begin;
	auto p = str.begin;
	while (p != str.end)
	{
		if (*p == '\n')
		{
			auto textLine = memStackPushType(mem, TextLine);
			textLine->text = StringSlice{lineBegin, p};
			lineBegin = p + 1;
		}
		++p;
	}
	auto textLine = memStackPushType(mem, TextLine);
	textLine->text = StringSlice{lineBegin, str.end};
}

char* projectErrorTypeToString(ProjectErrorType errorType)
{
	switch (errorType)
	{
	case ProjectErrorType::MissingVersionStatement:
		return "First statement in document should be a 'Version' statement";
	case ProjectErrorType::VersionInvalidFormat:
		return "Version number is not correctly formatted. It should have the syntax \"Major.Minor\", where \"Major\" and \"Minor\" are numbers";
	case ProjectErrorType::UnsupportedVersion:
		return "Unsupported version - this parser only supports version 1.0";
	case ProjectErrorType::UnknownValueType:
		return "Unknown type for value";
	case ProjectErrorType::MissingHereStringMarker:
		return "Expected marker token for here string";
	case ProjectErrorType::UnclosedHereStringMarker:
		return "Unclosed here string marker. Markers must be closed with a ':'";
	case ProjectErrorType::HereStringMarkerWhitespace:
		return "Here string markers contains whitespace";
	case ProjectErrorType::EmptyHereStringMarker:
		return "Here string marker is empty";
	case ProjectErrorType::UnclosedHereString:
		return "Here string not closed. Make sure its marker ends with a ':'";
	case ProjectErrorType::ShaderMissingIdentifier:
		return "Expected name for shader";
	case ProjectErrorType::ProgramMissingShaderList:
		return "Expected a shader list to follow the program name";
	case ProjectErrorType::ProgramUnclosedShaderList:
		return "Unclosed attached shader list";
	case ProjectErrorType::DuplicateShaderName:
		return "Another shader in this project has the same name";
	case ProjectErrorType::DuplicateProgramName:
		return "Another program in this project has the same name";
	case ProjectErrorType::ProgramExceedsAttachedShaderLimit:
		return "Programs cannot have more than 255 shaders attached";
	case ProjectErrorType::ProgramUnresolvedShaderIdent:
		return "No shader with this name exists in this project";
	default:
		unreachable();
		return "???";
	}
}

void updateApplication(ApplicationState& appState)
{
	processKeyBuffer(appState);
	if (appState.loadProject)
	{
		loadProject(appState.permMem, appState.scratchMem, appState);
		appState.loadProject = false;
	}

	auto windowWidth = (i32) appState.windowWidth;
	auto windowHeight = (i32) appState.windowHeight;

	i32 commandInputAreaHeight = 30;
	i32 commandInputAreaBottom = windowHeight - commandInputAreaHeight;

	auto commandInputArea = RectI32{
		Vec2I32{0, commandInputAreaBottom},
		Vec2I32{windowWidth, windowHeight}};

	auto previewArea = RectI32{
		Vec2I32{0, 0},
		Vec2I32{windowWidth, commandInputAreaBottom}};

	auto errorOverlayArea = RectI32{
		Vec2I32{previewArea.min.x + 20, previewArea.min.y + 20},
		Vec2I32{previewArea.max.x - 20, previewArea.max.y - 20}};

	float cornflowerBlue[4] = {0.3921568627451f, 0.5843137254902f, 0.9294117647059f, 1.0f};
	float errorOverlayColor[4] = {0.0f, 0.0f, 0.0f, 0.5f};
	float commandAreaColorDark[4] = {0.1f, 0.05f, 0.05f, 1.0f};
	float commandAreaColorLight[4] = {0.2f, 0.1f, 0.1f, 1.0f};

	u64 blinkPeriod = 2000000;
	u64 halfBlinkPeriod = blinkPeriod >> 1;
	bool useDarkCommandAreaColor = appState.currentTime.value % blinkPeriod < halfBlinkPeriod;
	auto commandAreaColor = useDarkCommandAreaColor ? commandAreaColorDark : commandAreaColorLight;

	glEnable(GL_SCISSOR_TEST);
	fillOpaqueRectangle(previewArea, cornflowerBlue);
	fillOpaqueRectangle(commandInputArea, commandAreaColor);
	glDisable(GL_SCISSOR_TEST);

	glViewport(
		previewArea.min.x,
		previewArea.min.y,
		rectWidth(previewArea),
		rectHeight(previewArea));
	glBindVertexArray(appState.userRenderConfig.vao);
	glUseProgram(appState.userRenderConfig.program);
	glDrawArrays(GL_TRIANGLES, 0, 3);

	glViewport(0, 0, windowWidth, windowHeight);

	if (appState.readProjectFileError.begin != nullptr
		|| appState.projectErrors.count > 0
		|| appState.vertShaderErrors.begin != nullptr
		|| appState.fragShaderErrors.begin != nullptr
		|| appState.programErrors.begin != nullptr)
	{
		auto windowWidthF = (float) appState.windowWidth;
		auto windowHeightF = (float) appState.windowHeight;
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glBindVertexArray(appState.fillRectRenderConfig.vao);
		glUseProgram(appState.fillRectRenderConfig.program);
		fillRectangle(
			appState.fillRectRenderConfig,
			windowWidthF,
			windowHeightF,
			errorOverlayArea,
			errorOverlayColor);
		glDisable(GL_BLEND);
	}

	auto memMarker = memStackMark(appState.scratchMem);

	auto projectErrorLocations = memStackPushArray(
		appState.scratchMem, PackedString, appState.projectErrors.count);
	for (u32 i = 0; i < appState.projectErrors.count; ++i)
	{
		auto pStringLength = memStackPushType(appState.scratchMem, size_t);
		auto pStringBegin = appState.scratchMem.top;

		auto error = appState.projectErrors.ptr[i];

		{
			auto str = stringSliceFromCString("Line ");
			auto strLength = stringSliceLength(str);
			auto ptr = memStackPushArray(appState.scratchMem, char, strLength);
			memcpy(ptr, str.begin, strLength);
		}

		{
			char *unused1;
			u32 unused2;
			u32ToString(appState.scratchMem, error.lineNumber, unused1, unused2);
		}

		{
			auto str = stringSliceFromCString(", char ");
			auto strLength = stringSliceLength(str);
			auto ptr = memStackPushArray(appState.scratchMem, char, strLength);
			memcpy(ptr, str.begin, strLength);
		}

		{
			char *unused1;
			u32 unused2;
			u32ToString(appState.scratchMem, error.charNumber, unused1, unused2);
		}

		auto pStringEnd = appState.scratchMem.top;
		*pStringLength = pStringEnd - pStringBegin;

		projectErrorLocations[i] = PackedString{pStringLength};
	}

	auto textLinesBegin = (TextLine*) appState.scratchMem.top;
	{
		auto commandLineText = memStackPushType(appState.scratchMem, TextLine);
		commandLineText->leftEdge = 5;
		commandLineText->baseline = windowHeight - 20;
		commandLineText->text.begin = appState.commandLine;
		commandLineText->text.end = appState.commandLine + appState.commandLineLength;
	}
	{
		auto infoLogTextLinesBegin = (TextLine*) appState.scratchMem.top;

		if (appState.readProjectFileError.begin != nullptr)
		{
			pushSingleTextLine(appState.scratchMem, stringSliceFromCString("Unable to read project file:"));
			pushMultiTextLine(appState.scratchMem, appState.readProjectFileError);
		}

		if (appState.projectErrors.count > 0)
		{
			pushSingleTextLine(appState.scratchMem, stringSliceFromCString("Errors in project file:"));
			for (u32 i = 0; i < appState.projectErrors.count; ++i)
			{
				pushSingleTextLine(appState.scratchMem, unpackString(projectErrorLocations[i]));
				auto error = appState.projectErrors.ptr[i];
				auto errorMessage = projectErrorTypeToString(error.type);
				pushSingleTextLine(appState.scratchMem, stringSliceFromCString(errorMessage));
				pushSingleTextLine(appState.scratchMem, stringSliceFromCString(">>>>>"));
				pushMultiTextLine(appState.scratchMem, unpackString(error.context));
				pushSingleTextLine(appState.scratchMem, stringSliceFromCString(">>>>>"));
				StringSlice emptyString = {};
				pushSingleTextLine(appState.scratchMem, emptyString);
			}
		}

		if (appState.vertShaderErrors.begin != nullptr)
		{
			pushSingleTextLine(appState.scratchMem, stringSliceFromCString("Errors in vertex shader:"));
			pushMultiTextLine(appState.scratchMem, appState.vertShaderErrors);
		}

		if (appState.fragShaderErrors.begin != nullptr)
		{
			pushSingleTextLine(appState.scratchMem, stringSliceFromCString("Errors in fragment shader:"));
			pushMultiTextLine(appState.scratchMem, appState.fragShaderErrors);
		}

		if (appState.programErrors.begin != nullptr)
		{
			pushSingleTextLine(appState.scratchMem, stringSliceFromCString("Errors in program:"));
			pushMultiTextLine(appState.scratchMem, appState.programErrors);
		}

		auto infoLogTextLinesEnd = (TextLine*) appState.scratchMem.top;

		auto textLeftEdge = errorOverlayArea.min.x + 5;
		auto textBaseline = errorOverlayArea.max.y - 20;
		auto pTextLine = infoLogTextLinesBegin;
		while (pTextLine != infoLogTextLinesEnd)
		{
			pTextLine->leftEdge = textLeftEdge;
			pTextLine->baseline = textBaseline;
			textBaseline -= appState.font.advanceY;
			++pTextLine;
		}
	}
	auto textLinesEnd = (TextLine*) appState.scratchMem.top;

	drawText(
		appState.textRenderConfig,
		appState.font,
		appState.windowWidth,
		appState.windowHeight,
		textLinesBegin,
		textLinesEnd);

	memStackPop(appState.scratchMem, memMarker);

	assert(appState.scratchMem.top == appState.scratchMem.begin);
	memStackClear(appState.scratchMem);
}

