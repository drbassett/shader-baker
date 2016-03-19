#include <cstdio>
#include <gl/gl.h>
#include "../include/glcorearb.h"
#include "generated/glFunctions.cpp"

#define arrayLength(array) sizeof(array) / sizeof(array[0])

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

struct ApplicationState
{
	AsciiFont font;
	SimpleRenderConfig simpleRenderConfig;
	TextRenderConfig textRenderConfig;
	unsigned windowWidth, windowHeight;
};

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
	// cornflower blue
	glClearColor(0.3921568627451f, 0.5843137254902f, 0.9294117647059f, 1.0f);
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

	return true;

resultFail:
	destroyApplication(appState);
	return false;
}

void resizeApplication(ApplicationState& appState, int width, int height)
{
	appState.windowWidth = width;
	appState.windowHeight = height;
	glViewport(0, 0, width, height);
}

static void drawText(ApplicationState& appState)
{
	const char *textToRender = "Hello, world!\nHello, world!";
	size_t numCharsToRender = strlen(textToRender);
	auto charDataSize = numCharsToRender * sizeof(GLuint) * 3;
	glBindBuffer(GL_ARRAY_BUFFER, appState.textRenderConfig.charDataBuffer);
	glBufferData(GL_ARRAY_BUFFER, charDataSize, 0, GL_STREAM_DRAW);
	auto pCharData = (GLuint*) glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);
	auto pText = textToRender;
	GLuint leftEdge = 5;
	GLuint charX = leftEdge;
	GLuint baselineY = 100;
	while (*pText != '\0')
	{
		auto c = *pText;
		auto glyphMetrics = appState.font.glyphMetrics[c];

		pCharData[0] = charX + glyphMetrics.offsetLeft;
		pCharData[1] = baselineY - glyphMetrics.offsetTop;
		if (*pText == '\n')
		{
			pCharData[2] = ' ';
			charX = leftEdge;
			baselineY -= appState.font.advanceY;
		} else
		{
			pCharData[2] = c;
			charX += glyphMetrics.advanceX;
		}

		++pText;
		pCharData += 3;
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

	glDrawArrays(GL_POINTS, 0, (GLsizei) numCharsToRender);

	glDisable(GL_BLEND);
}

void updateApplication(ApplicationState& appState)
{
	glClear(GL_COLOR_BUFFER_BIT);

	{
		glBindVertexArray(appState.simpleRenderConfig.vao);
		glUseProgram(appState.simpleRenderConfig.program);
		glDrawArrays(GL_POINTS, 0, 1);
	}

	drawText(appState);
}

