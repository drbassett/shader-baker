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

inline size_t cStringLength(char *c)
{
	auto end = c;
	while (*end != '\0')
	{
		++end;
	}
	return end - c;
}

inline size_t stringSliceLength(StringSlice str)
{
	return str.end - str.begin;
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

	appState.loadUserRenderConfig = false;
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

void freeInfoLogTextChunks(InfoLogTextChunk*& chunks, InfoLogTextChunk*& freeList)
{
	while (chunks != nullptr)
	{
		auto freed = chunks;
		chunks = chunks->next;
		freed->next = freeList;
		freeList = freed;
	}
}

static void copyLogToTextChunks(
	MemStack& permMem,
	InfoLogTextChunk*& result,
	InfoLogTextChunk*& freeList,
	GLchar* log,
	GLint logLength)
{
	result = nullptr;
	for (;;)
	{
		InfoLogTextChunk *textChunk;
		if (freeList == nullptr)
		{
			textChunk = memStackPushType(permMem, InfoLogTextChunk);
		} else
		{
			textChunk = freeList;
			freeList = freeList->next;
		}
		textChunk->next = result;
		result = textChunk;

		auto chunkSize = (u32) sizeof(result->text);
		if ((u32) logLength < chunkSize)
		{
			result->count = logLength;
			memcpy(result->text, log, logLength);
			return;
		} else
		{
			result->count = chunkSize;
			memcpy(result->text, log, chunkSize);
			logLength -= chunkSize;
			log += chunkSize;
		}
	}
}

void readShaderLog(
	MemStack& permMem,
	MemStack& scratchMem,
	GLint shader,
	InfoLogTextChunk*& result,
	InfoLogTextChunk*& freeList)
{
	GLint logLength;
	glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);

	auto log = memStackPushArray(scratchMem, GLchar, logLength);
	GLsizei readLogLength;
	glGetShaderInfoLog(shader, logLength, &readLogLength, log);

	copyLogToTextChunks(permMem, result, freeList, log, logLength - 1);
}

void readProgramLog(
	MemStack& permMem,
	MemStack& scratchMem,
	GLint program,
	InfoLogTextChunk*& result,
	InfoLogTextChunk*& freeList)
{
	GLint logLength;
	glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLength);

	auto log = memStackPushArray(scratchMem, GLchar, logLength);
	GLsizei readLogLength;
	glGetProgramInfoLog(program, logLength, &readLogLength, log);

	copyLogToTextChunks(permMem, result, freeList, log, logLength - 1);
}

void loadUserShader(
	MemStack& permMem,
	MemStack& scratchMem,
	GLint shader,
	FilePath shaderPath,
	InfoLogTextChunk*& errorLog,
	InfoLogTextChunk*& errorLogFreeList)
{
	freeInfoLogTextChunks(errorLog, errorLogFreeList);

	ReadFileError readError;
	u8* fileContents;
	size_t fileSize;
	PLATFORM_readWholeFile(scratchMem, shaderPath, readError, fileContents, fileSize);
	if (fileContents == nullptr)
	{
		char *errorString;
		switch (readError)
		{
		case ReadFileError::FileNotFound:
			errorString = "The shader file does not exist";
			break;
		case ReadFileError::FileInUse:
			errorString = "The shader file is in use by another process";
			break;
		case ReadFileError::AccessDenied:
			errorString =
				"The Operating System denied access to the shader file. You may have insufficient \
				permissions, or the file may be pending deletion.";
			break;
		case ReadFileError::Other:
			errorString = "The shader file could not be read";
			break;
		default:
			unreachable();
			errorString = "";
		}
		auto errorStringLength = (GLint) cStringLength(errorString);
		copyLogToTextChunks(permMem, errorLog, errorLogFreeList, errorString, errorStringLength);
		return;
	}

//TODO there is no guarantee that fileSize is big enough to fit in a GLint - bulletproof this
	auto fileSizeTruncated = (GLint) fileSize;
	glShaderSource(shader, 1, (GLchar**) &fileContents, &fileSizeTruncated);
	glCompileShader(shader);
	if (!shaderCompileSuccessful(shader))
	{
		readShaderLog(permMem, scratchMem, shader, errorLog, errorLogFreeList);
	}
}

void loadUserRenderConfig(
	MemStack& permMem,
	MemStack& scratchMem,
	FilePath vertShaderPath,
	FilePath fragShaderPath,
	UserRenderConfig const& renderConfig,
	InfoLogErrors& infoLogErrors)
{
	auto memMarker = memStackMark(scratchMem);

	loadUserShader(
		permMem,
		scratchMem,
		renderConfig.vertShader,
		vertShaderPath,
		infoLogErrors.vertShaderErrors,
		infoLogErrors.freeList);
	loadUserShader(
		permMem,
		scratchMem,
		renderConfig.fragShader,
		fragShaderPath,
		infoLogErrors.fragShaderErrors,
		infoLogErrors.freeList);
	if (infoLogErrors.vertShaderErrors != nullptr
		|| infoLogErrors.fragShaderErrors != nullptr)
	{
		goto cleanup;
	}
	
	glLinkProgram(renderConfig.program);
	if (!programLinkSuccessful(renderConfig.program))
	{
		freeInfoLogTextChunks(infoLogErrors.programErrors, infoLogErrors.freeList);
		readProgramLog(
			permMem,
			scratchMem,
			renderConfig.program,
			infoLogErrors.programErrors,
			infoLogErrors.freeList);
	}

cleanup:
	memStackPop(scratchMem, memMarker);
}

void infoLogToTextLines(
	MemStack& scratchMem,
	AsciiFont const& font,
	char *header,
	InfoLogTextChunk *textChunks,
	i32 leftEdge,
	i32& baseline)
{
	if (textChunks == nullptr)
	{
		return;
	}

	{
		auto textLine = memStackPushType(scratchMem, TextLine);
		textLine->leftEdge = leftEdge;
		textLine->baseline = baseline;
		textLine->text = stringSliceFromCString(header);
		baseline -= font.advanceY;
	}

	while (textChunks != nullptr)
	{
		auto pChar = textChunks->text;
		auto pCharEnd = pChar + textChunks->count;
		textChunks = textChunks->next;

		auto textLine = memStackPushType(scratchMem, TextLine);
		textLine->leftEdge = leftEdge;
		textLine->baseline = baseline;
		textLine->text.begin = pChar;

		while (pChar != pCharEnd)
		{
			if (*pChar == '\n')
			{
				textLine->text.end = pChar;
				baseline -= font.advanceY;

				textLine = memStackPushType(scratchMem, TextLine);
				textLine->leftEdge = leftEdge;
				textLine->baseline = baseline;
				textLine->text.begin = pChar + 1;
			}
			++pChar;
		}
		textLine->text.end = pCharEnd;
	}

	baseline -= font.advanceY;
}

void updateApplication(ApplicationState& appState)
{
	processKeyBuffer(appState);
	if (appState.loadUserRenderConfig)
	{
		loadUserRenderConfig(
			appState.permMem,
			appState.scratchMem,
			appState.userVertShaderPath,
			appState.userFragShaderPath,
			appState.userRenderConfig,
			appState.infoLogErrors);
		appState.loadUserRenderConfig = false;
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

	if (appState.infoLogErrors.vertShaderErrors != nullptr
		|| appState.infoLogErrors.fragShaderErrors != nullptr
		|| appState.infoLogErrors.programErrors != nullptr)
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

	auto textLinesBegin = (TextLine*) appState.scratchMem.top;
	{
		auto commandLineText = memStackPushType(appState.scratchMem, TextLine);
		commandLineText->leftEdge = 5;
		commandLineText->baseline = windowHeight - 20;
		commandLineText->text.begin = appState.commandLine;
		commandLineText->text.end = appState.commandLine + appState.commandLineLength;
	}
	{
		auto infoLogLeftEdge = errorOverlayArea.min.x + 5;
		auto infoLogBaseline = errorOverlayArea.max.y - 20;
		infoLogToTextLines(
			appState.scratchMem,
			appState.font,
			"Errors in vertex shader:",
			appState.infoLogErrors.vertShaderErrors,
			infoLogLeftEdge,
			infoLogBaseline);
		infoLogToTextLines(
			appState.scratchMem,
			appState.font,
			"Errors in fragment shader:",
			appState.infoLogErrors.fragShaderErrors,
			infoLogLeftEdge,
			infoLogBaseline);
		infoLogToTextLines(
			appState.scratchMem,
			appState.font,
			"Errors in program:",
			appState.infoLogErrors.programErrors,
			infoLogLeftEdge,
			infoLogBaseline);
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

