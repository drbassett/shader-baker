#include <cassert>
#include <cstdio>
#include <gl/gl.h>
#include "../include/glcorearb.h"
#include "generated/glFunctions.cpp"
#include "Types.h"
#include "ShaderBaker.h"
#include "Platform.h"

#define arrayLength(array) sizeof(array) / sizeof(array[0])
#define scratchMemPushType(mem, type) (type*) scratchMemoryPush(mem, sizeof(type))

static inline size_t megabytes(size_t value)
{
	return value * 1024 * 1024;
}

static inline size_t permMemoryBlockSize()
{
	//return megabytes(64);
	return 1500;
}

bool memoryStackInit(MemoryStack& stack, size_t initialSize)
{
	assert(initialSize > 0);
	
	auto memory = (u8*) PLATFORM_alloc(initialSize);
	if (memory == nullptr)
	{
		return false;
	}

	stack.begin = memory;
	stack.end = memory + initialSize;
	stack.top = memory;
	return true;
}

static void memoryStackExtend(LinkedMemoryStack& stack, size_t requiredSpace)
{
	size_t currentSize = stack.stack.end - stack.stack.begin;
//TODO handle overflow
	size_t extensionSize = (currentSize << 1);
	requiredSpace += sizeof(LinkedMemoryStack);
	if (requiredSpace > extensionSize)
	{
		extensionSize = requiredSpace;
	}

	LinkedMemoryStack extension = {};
	memoryStackInit(extension.stack, extensionSize);
	extension.next = (LinkedMemoryStack*) extension.stack.begin;
	extension.stack.top += sizeof(LinkedMemoryStack);
	*extension.next = stack;
	stack = extension;
}

inline void* permMemoryPush(MemoryStack& stack, size_t size)
{
	assert(size < permMemoryBlockSize());

	size_t remainingSize = stack.end - stack.top;
	if (remainingSize < size)
	{
		// Yes, reinitializing the memory "leaks" it, in that we never give it back to the
		// heap while this application is alive. But this is permanent memory. We want to
		// keep it around until the application is closed; and after it closes, this OS will
		// clean up all our memory in one fell swoop anyways.
		memoryStackInit(stack, permMemoryBlockSize());
	}

	auto result = stack.top;
	stack.top += size;
	return result;
}

//TODO memory alignment
inline void* scratchMemoryPush(LinkedMemoryStack& stack, size_t size)
{
	size_t remainingSize = stack.stack.end - stack.stack.top;
	if (size > remainingSize)
	{
		memoryStackExtend(stack, size);
	}

	auto result = stack.stack.top;
	stack.stack.top += size;
	return result;
}

inline void memoryStackClear(LinkedMemoryStack& stack)
{
	stack.stack.top = stack.stack.begin;
}

static void coalesceLinkedMemoryStacks(LinkedMemoryStack& head)
{
	if (head.next == nullptr)
	{
		memoryStackClear(head);
		return;
	}

//TODO handle overflow
	size_t totalSize = 0;
	{
		LinkedMemoryStack link = head;
		for (;;)
		{
			auto stackBegin = link.stack.begin;
			totalSize += link.stack.end - stackBegin;
			auto next = link.next;
			if (next == nullptr)
			{
				break;
			}
			link = *next;

			auto freed = PLATFORM_free(stackBegin);
			assert(freed);
		}
		auto freed = PLATFORM_free(link.stack.begin);
		assert(freed);
	}

	memoryStackInit(head.stack, totalSize);
	head.next = nullptr;
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

static inline bool readFontFile(ApplicationState& appState, const char *fileName)
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

bool initApplication(ApplicationState& appState)
{
	if (!memoryStackInit(appState.scratchMemory.stack, sizeof(LinkedMemoryStack)))
	{
		return false;
	}
	appState.permMemory = {};

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

	if (!initTextRenderingProgram(appState.textRenderConfig.program))
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

static inline void fillRectangle(RectI32 const& rect, float color[4])
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
	MemoryStack& permMemory,
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
			textChunk = (InfoLogTextChunk*) permMemoryPush(permMemory, sizeof(InfoLogTextChunk));
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
	MemoryStack& permMemory,
	LinkedMemoryStack& scratchMemory,
	GLint shader,
	InfoLogTextChunk*& result,
	InfoLogTextChunk*& freeList)
{
	GLint logLength;
	glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);

	auto log = (GLchar*) scratchMemoryPush(scratchMemory, (logLength + 1) * sizeof(GLchar));
	GLsizei readLogLength;
	glGetShaderInfoLog(shader, logLength, &readLogLength, log);

	copyLogToTextChunks(permMemory, result, freeList, log, logLength);
}

void readProgramLog(
	MemoryStack& permMemory,
	LinkedMemoryStack& scratchMemory,
	GLint program,
	InfoLogTextChunk*& result,
	InfoLogTextChunk*& freeList)
{
	GLint logLength;
	glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLength);

	auto log = (GLchar*) scratchMemoryPush(scratchMemory, (logLength + 1) * sizeof(GLchar));
	GLsizei readLogLength;
	glGetProgramInfoLog(program, logLength, &readLogLength, log);

	copyLogToTextChunks(permMemory, result, freeList, log, logLength);
}

void loadUserShader(
	MemoryStack& permMemory,
	LinkedMemoryStack& scratchMemory,
	GLint shader,
	FilePath shaderPath,
	InfoLogTextChunk*& errorLog,
	InfoLogTextChunk*& errorLogFreeList)
{
	freeInfoLogTextChunks(errorLog, errorLogFreeList);

	size_t fileSize;
	auto fileContents = PLATFORM_readWholeFile(scratchMemory, shaderPath, fileSize);
//TODO there is no guarantee that fileSize is big enough to fit in a GLint - bulletproof this
	auto fileSizeTruncated = (GLint) fileSize;
	if (fileContents == nullptr)
	{
		return;
	}

	glShaderSource(shader, 1, (GLchar**) &fileContents, &fileSizeTruncated);
	glCompileShader(shader);
	if (!shaderCompileSuccessful(shader))
	{
		readShaderLog(permMemory, scratchMemory, shader, errorLog, errorLogFreeList);
	}
}

void loadUserRenderConfig(
	MemoryStack& permMemory,
	LinkedMemoryStack& scratchMemory,
	FilePath vertShaderPath,
	FilePath fragShaderPath,
	UserRenderConfig const& renderConfig,
	InfoLogErrors& infoLogErrors)
{
	loadUserShader(
		permMemory,
		scratchMemory,
		renderConfig.vertShader,
		vertShaderPath,
		infoLogErrors.vertShaderErrors,
		infoLogErrors.freeList);
	loadUserShader(
		permMemory,
		scratchMemory,
		renderConfig.fragShader,
		fragShaderPath,
		infoLogErrors.fragShaderErrors,
		infoLogErrors.freeList);
	if (infoLogErrors.vertShaderErrors != nullptr
		|| infoLogErrors.fragShaderErrors != nullptr)
	{
		return;
	}
	
	glLinkProgram(renderConfig.program);
	if (!programLinkSuccessful(renderConfig.program))
	{
		freeInfoLogTextChunks(infoLogErrors.programErrors, infoLogErrors.freeList);
		readProgramLog(
			permMemory,
			scratchMemory,
			renderConfig.program,
			infoLogErrors.programErrors,
			infoLogErrors.freeList);
	}
}

void updateApplication(ApplicationState& appState)
{
	processKeyBuffer(appState);
	if (appState.loadUserRenderConfig)
	{
		loadUserRenderConfig(
			appState.permMemory,
			appState.scratchMemory,
			appState.userVertShaderPath,
			appState.userFragShaderPath,
			appState.userRenderConfig,
			appState.infoLogErrors);
		appState.loadUserRenderConfig = false;
	}

	auto windowWidth = (i32) appState.windowWidth;
	auto windowHeight = (i32) appState.windowHeight;

//TODO it is not safe to assume that multiple pushes are contiguous in memory
	auto textLinesBegin = (TextLine*) appState.scratchMemory.stack.top;
	{
		auto commandLineText = scratchMemPushType(appState.scratchMemory, TextLine);
		commandLineText->leftEdge = 5;
		commandLineText->baseline = windowHeight - 20;
		commandLineText->text.begin = appState.commandLine;
		commandLineText->text.end = appState.commandLine + appState.commandLineLength;
	}
	auto textLinesEnd = (TextLine*) appState.scratchMemory.stack.top;

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
	drawText(appState.textRenderConfig, appState.font, appState.windowWidth, appState.windowHeight, textLinesBegin, textLinesEnd);

	glViewport(
		previewArea.min.x,
		previewArea.min.y,
		rectWidth(previewArea),
		rectHeight(previewArea));
	glBindVertexArray(appState.userRenderConfig.vao);
	glUseProgram(appState.userRenderConfig.program);
	glDrawArrays(GL_TRIANGLES, 0, 3);

	coalesceLinkedMemoryStacks(appState.scratchMemory);
}

