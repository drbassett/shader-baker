#include "ShaderBaker.h"

//TODO remove dependency on cstdio
#include <cstdio>

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

	glDeleteVertexArrays(1, &appState.previewRenderConfig.vao);
	glDeleteProgram(appState.previewRenderConfig.program);
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

	glGenVertexArrays(1, &appState.previewRenderConfig.vao);
	appState.previewRenderConfig.program = glCreateProgram();

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

static char* projectErrorTypeToString(ProjectErrorType errorType)
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

static void stringifyProjectErrors(
	ApplicationState& app, StringSlice projectText, ProjectErrors const& errors)
{
	auto memMarker = memStackMark(app.scratchMem);

	// scan through the project text to find line boundaries
	u32 lineCount = 0;
	char extraLineCharacter;
	auto lines = (StringSlice*) app.scratchMem.top;
	{
		auto lineBegin = projectText.begin;
		auto cursor = projectText.begin;
		while (cursor != projectText.end)
		{
			switch (*cursor)
			{
			case '\n':
				extraLineCharacter = '\r';
				break;
			case '\r':
				extraLineCharacter = '\n';
				break;
			default:
				++cursor;
				continue;
			}

			auto lineBounds = memStackPushType(app.scratchMem, StringSlice);
			lineBounds->begin = lineBegin;
			lineBounds->end = cursor;
			++lineCount;
			++cursor;
			if (cursor == projectText.end)
			{
				break;
			}
			if (*cursor == extraLineCharacter)
			{
				++cursor;
			}
			lineBegin = cursor;
		}
	}

	app.projectErrorStrings = app.permMem.top;
	app.projectErrorStringCount = 0;

	char *unused1;
	u32 unused2;
	for (u32 errorIdx = 0; errorIdx < errors.count; ++errorIdx)
	{
		auto error = errors.ptr[errorIdx];

		// the number of lines above/below the error to display for context
		u32 contextLineCount = 2;

		u32 firstContextLineIdx;
		firstContextLineIdx = error.location.lineNumber;
		if (error.location.lineNumber > contextLineCount)
		{
			firstContextLineIdx = error.location.lineNumber - contextLineCount;
		}
		--firstContextLineIdx;

		u32 lastContextLineIdx = error.location.lineNumber + contextLineCount;
		if (lastContextLineIdx > lineCount)
		{
			lastContextLineIdx = lineCount;
		}

		{
			auto stringBuilder = beginPackedString(app.permMem);
			memStackPushCString(app.permMem, "Line ");
			u32ToString(app.permMem, error.location.lineNumber, unused1, unused2);
			memStackPushCString(app.permMem, ", char ");
			u32ToString(app.permMem, error.location.charNumber, unused1, unused2);
			endPackedString(app.permMem, stringBuilder);
		}
		++app.projectErrorStringCount;

		packCString(app.permMem, projectErrorTypeToString(error.type));
		++app.projectErrorStringCount;

		packCString(app.permMem, ">>>>>");
		++app.projectErrorStringCount;

		for (u32 i = firstContextLineIdx; i < lastContextLineIdx; ++i)
		{
			auto lineBounds = lines[i];

			{
				auto stringBuilder = beginPackedString(app.permMem);
				u32ToString(app.permMem, i + 1, unused1, unused2);
				memStackPushCString(app.permMem, " | ");
				memStackPushString(app.permMem, lineBounds);
				endPackedString(app.permMem, stringBuilder);
			}
			++app.projectErrorStringCount;
		}

		packCString(app.permMem, ">>>>>");
		++app.projectErrorStringCount;

		packCString(app.permMem, "");
		++app.projectErrorStringCount;
	}

	memStackPop(app.scratchMem, memMarker);
}

GLuint glShaderType(ShaderType type)
{
	switch (type)
	{
	case ShaderType::Vertex: return GL_VERTEX_SHADER;
	case ShaderType::TessControl: return GL_TESS_CONTROL_SHADER;
	case ShaderType::TessEvaluation: return GL_TESS_EVALUATION_SHADER;
	case ShaderType::Geometry: return GL_GEOMETRY_SHADER;
	case ShaderType::Fragment: return GL_FRAGMENT_SHADER;
	case ShaderType::Compute: return GL_COMPUTE_SHADER;
	}

	unreachable();
	return GL_VERTEX_SHADER;
}

void initPreviewProgram(ApplicationState& app)
{
}

void loadProject(ApplicationState& app)
{
	memStackClear(app.permMem);
	app.readProjectFileError = {};
	app.projectErrorStrings = nullptr;
	app.projectErrorStringCount = 0;
	app.previewProgramErrors = {};

	auto memMarker = memStackMark(app.scratchMem);

	ReadFileError readError;
	u8 *fileContents;
	size_t fileSize;
	PLATFORM_readWholeFile(app.scratchMem, app.projectPath, readError, fileContents, fileSize);
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
		app.readProjectFileError.begin = memStackPushArray(app.permMem, char, errorStringLength);
		app.readProjectFileError.end = app.readProjectFileError.begin + errorStringLength;
		memcpy(app.readProjectFileError.begin, errorString, errorStringLength);
		goto exit1;
	}

	{
		StringSlice projectText{(char*) fileContents, (char*) fileContents + fileSize}; 
		ProjectErrors projectErrors = {};
		app.project = parseProject(app.scratchMem, app.permMem, projectText, projectErrors);
		if (projectErrors.count != 0)
		{
			stringifyProjectErrors(app, projectText, projectErrors);
			goto exit1;
		}
	}

	if (stringSliceLength(app.previewProgramName) == 0)
	{
		goto exit1;
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
		goto exit1;
	}

	bool shaderCompilesSuccessful = true;
	auto shaderCount = previewProgram->attachedShaderCount;
	auto shaders = memStackPushArray(app.scratchMem, GLint, shaderCount);
	auto errorStringBuilder = beginPackedString(app.permMem);
	for (u32 i = 0; i < shaderCount; ++i)
	{
		auto shader = previewProgram->attachedShaders[i];
		auto glShader = glCreateShader(glShaderType(shader->type));
		shaders[i] = glShader;

		auto shaderSource = unpackString(shader->source);
//TODO there is no guarantee that the shader source will fit in a GLint - bulletproof this
		auto shaderSourceLength = (GLint) stringSliceLength(shaderSource);
		glShaderSource(glShader, 1, (GLchar**) &shaderSource.begin, &shaderSourceLength);
		glCompileShader(glShader);
		if (!shaderCompileSuccessful(glShader))
		{
			memStackPushCString(app.permMem, "Compile errors in shader '");
			memStackPushString(app.permMem, unpackString(shader->name));
			memStackPushCString(app.permMem, "':\n");
			readShaderLog(app.permMem, glShader);
			memStackPushCString(app.permMem, "\n");
			shaderCompilesSuccessful = false;
		}

		glAttachShader(app.previewRenderConfig.program, glShader);
	}

	if (!shaderCompilesSuccessful)
	{
		app.previewProgramErrors = endPackedString(app.permMem, errorStringBuilder);
		goto exit2;
	}
	
	glLinkProgram(app.previewRenderConfig.program);
	if (!programLinkSuccessful(app.previewRenderConfig.program))
	{
		memStackPushCString(app.permMem, "Program link failed:\n");
		readProgramLog(app.permMem, app.previewRenderConfig.program);
		app.previewProgramErrors = endPackedString(app.permMem, errorStringBuilder);
		goto exit2;
	}

	endPackedString(app.permMem, errorStringBuilder);
	app.previewProgramErrors = PackedString{nullptr};

exit2:
	for (u32 i = 0; i < shaderCount; ++i)
	{
		auto shader = shaders[i];
		glDetachShader(app.previewRenderConfig.program, shader);
		glDeleteShader(shader);
	}

exit1:
	memStackPop(app.scratchMem, memMarker);
}

static inline void processCommand(ApplicationState& app)
{
	auto command = StringSlice{
		app.commandLine,
		app.commandLine + app.commandLineLength};
	app.commandLineLength = 0;

	auto memMarker = memStackMark(app.scratchMem);

	auto args = (StringSlice*) app.scratchMem.top;
	u32 argCount = 0;
	auto p = command.begin;
	while (p != command.end)
	{
		auto argBegin = p;
		switch (*p)
		{
		case ' ':
		case '\t':
			++p;
			break;
		default:
			for (;;)
			{
				bool pushArg;
				if (p == command.end)
				{
					pushArg = true;
				} else
				{
					switch (*p)
					{
					case ' ':
					case '\t':
						pushArg = true;
						break;
					default:
						pushArg = false;
					}
				}

				if (pushArg)
				{
					assert(argBegin != p);
					auto arg = memStackPushType(app.scratchMem, StringSlice);
					arg->begin = argBegin;
					arg->end = p;
					++argCount;
					break;
				}

				++p;
			}
		}
	}

	if (argCount == 0)
	{
		return;
	}
	
//TODO should extra argments be ignored, or reported as errors?
	auto firstArg = args[0];
	if (firstArg == "load-project")
	{
//TODO paths with spaces?
//TODO relative paths?
		if (argCount >= 2)
		{
			auto fileNameArg = args[1];
			auto fileNameLength = stringSliceLength(fileNameArg);
			assert(fileNameLength <= sizeof(app.projectPathStorage));
			memcpy(app.projectPathStorage, fileNameArg.begin, fileNameLength);
			app.projectPath.path.begin = app.projectPathStorage;
			app.projectPath.path.end = app.projectPathStorage + fileNameLength;
			loadProject(app);
		} else
		{
//TODO handle missing file path argument
		}
	} else if (firstArg == "preview-program")
	{
		if (argCount >= 2)
		{
			auto programNameArg = args[1];
			auto nameLength = stringSliceLength(programNameArg);
			assert(nameLength <= sizeof(app.previewProgramNameStorage));
			memcpy(app.previewProgramNameStorage, programNameArg.begin, nameLength);
			app.previewProgramName.begin = app.previewProgramNameStorage;
			app.previewProgramName.end = app.previewProgramNameStorage + nameLength;
//TODO reloading the whole project works, but it is overkill.
// Add a procedure to just set the preview program
			loadProject(app);
		} else
		{
//TODO handle missing argument
		}
	} else
	{
//TODO handle unknown command
	}

	memStackPop(app.scratchMem, memMarker);
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

void updateApplication(ApplicationState& appState)
{
	processKeyBuffer(appState);
	if (appState.loadProject)
	{
		loadProject(appState);
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

//TODO make sure the preview shader is valid before drawing it
	glViewport(
		previewArea.min.x,
		previewArea.min.y,
		rectWidth(previewArea),
		rectHeight(previewArea));
	glBindVertexArray(appState.previewRenderConfig.vao);
	glUseProgram(appState.previewRenderConfig.program);
	glDrawArrays(GL_TRIANGLES, 0, 3);

	glViewport(0, 0, windowWidth, windowHeight);

	if (appState.readProjectFileError.begin != nullptr
		|| appState.projectErrorStringCount > 0
		|| appState.previewProgramErrors.ptr != nullptr)
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
		auto infoLogTextLinesBegin = (TextLine*) appState.scratchMem.top;

		if (appState.readProjectFileError.begin != nullptr)
		{
			pushSingleTextLine(appState.scratchMem, stringSliceFromCString("Unable to read project file:"));
			pushMultiTextLine(appState.scratchMem, appState.readProjectFileError);
		}

		if (appState.projectErrorStringCount > 0)
		{
			pushSingleTextLine(appState.scratchMem, stringSliceFromCString("Errors in project file:"));
			auto ptr = (u8*) appState.projectErrorStrings;
			for (u32 i = 0; i < appState.projectErrorStringCount; ++i)
			{
				auto line = unpackString(PackedString{ptr});
				pushSingleTextLine(appState.scratchMem, line);
				ptr += sizeof(size_t) + stringSliceLength(line);
			}
		}

		if (appState.previewProgramErrors.ptr != nullptr)
		{
			pushMultiTextLine(appState.scratchMem, unpackString(appState.previewProgramErrors));
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

