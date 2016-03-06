#include <gl/gl.h>
#include "../include/glcorearb.h"
#include "generated/glFunctions.cpp"

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
	GLint attribLowerLeftCorner, attribCharacterIndex;
};

struct ApplicationState
{
	SimpleRenderConfig simpleRenderConfig;
	TextRenderConfig textRenderConfig;
	unsigned windowWidth, windowHeight;
	unsigned charWidth, charHeight;
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

		layout(location = 0) in uvec2 lowerLeftCorner;
		layout(location = 1) in uint characterIndex;

		flat out uint vsCharacter;

		void main()
		{
			vec2 pixelCenterOffset = vec2(0.5, 0.5);
			vsCharacter = characterIndex;
			gl_Position.xy = 2.0f * (lowerLeftCorner + 0.5f) / viewportSizePx - 1.0f;
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
			vec2 lowerLeftNdc = gl_in[0].gl_Position.xy;
			vec2 characterSizeNdc = 2.0 * characterSizePx / viewportSizePx;

			gsCharacter = vsCharacter[0];
			gl_Position.z = 0.0;
			gl_Position.w = 1.0;

			float minX = lowerLeftNdc.x;
			float maxX = minX + characterSizeNdc.x;
			float minY = lowerLeftNdc.y;
			float maxY = minY + characterSizeNdc.y;

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
			color = texture(characterSampler, vec3(texCoord, gsCharacter));
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
	appState.charWidth = 50;
	appState.charHeight = 100;

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

	appState.textRenderConfig.attribLowerLeftCorner = 0;
	appState.textRenderConfig.attribCharacterIndex = 1;
	glGenVertexArrays(1, &appState.textRenderConfig.vao);
	glBindVertexArray(appState.textRenderConfig.vao);
	auto sizeAttrib0 = sizeof(GLuint) * 2;
	auto sizeAttrib1 = sizeof(GLuint);
	auto stride = (GLsizei) (sizeAttrib0 + sizeAttrib1);
	glVertexAttribIPointer(
		appState.textRenderConfig.attribLowerLeftCorner,
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
	glEnableVertexAttribArray(appState.textRenderConfig.attribLowerLeftCorner);
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

	//NOTE glMapBuffer with GL_PIXEL_UNPACK_BUFFER can be used to updload textures. It probably
	// will not have any advantage in this situtation, however.
	unsigned numberChars = 256;
	unsigned imageSize = 4 * appState.charWidth * appState.charHeight;
	unsigned imageArraySize = imageSize * numberChars;
	auto textureMemory = (unsigned char*) malloc(imageArraySize);
	auto pCharacterTexture = textureMemory;
	for (unsigned index = 0; index < numberChars; ++index)
	{
		char brightness = (char) index;
		for (unsigned row = 0; row < appState.charHeight; ++row)
		{
			for (unsigned col = 0; col < appState.charWidth; ++col)
			{
				pCharacterTexture[0] = brightness;
				pCharacterTexture[1] = brightness;
				pCharacterTexture[2] = brightness;
				pCharacterTexture[3] = 255;
				pCharacterTexture += 4;
			}
		}
	}

	glBindTexture(GL_TEXTURE_2D_ARRAY, appState.textRenderConfig.texture);
	glTexStorage3D(GL_TEXTURE_2D_ARRAY, 1, GL_RGBA8, appState.charWidth, appState.charHeight, numberChars);
	glTexSubImage3D(
		GL_TEXTURE_2D_ARRAY,
		0,
		0, 0, 0,
		appState.charWidth, appState.charHeight, numberChars,
		GL_RGBA,
		GL_UNSIGNED_BYTE,
		textureMemory);
	free(textureMemory);

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
	const char *textToRender = "Hello, world!";
	size_t numCharsToRender = strlen(textToRender);
	auto charDataSize = numCharsToRender * sizeof(GLuint) * 3;
	glBindBuffer(GL_ARRAY_BUFFER, appState.textRenderConfig.charDataBuffer);
	glBufferData(GL_ARRAY_BUFFER, charDataSize, 0, GL_STREAM_DRAW);
	auto pCharData = (GLuint*) glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);
	auto pText = textToRender;
	GLuint charX = 50;
	while (*pText != '\0')
	{
		pCharData[0] = charX;
		pCharData[1] = 50;
		pCharData[2] = *pText;

		charX += appState.charWidth;
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
		(float) appState.charWidth,
		(float) appState.charHeight);

	glActiveTexture(GL_TEXTURE0 + appState.textRenderConfig.textureUnit);
	glBindTexture(GL_TEXTURE_2D_ARRAY, appState.textRenderConfig.texture);
	glBindSampler(
		appState.textRenderConfig.textureUnit,
		appState.textRenderConfig.textureSampler);
	glUniform1i(
		appState.textRenderConfig.unifCharacterSampler,
		appState.textRenderConfig.textureUnit);

	glDrawArrays(GL_POINTS, 0, (GLsizei) numCharsToRender);

//TODO enable blending, set the right blend mode
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

