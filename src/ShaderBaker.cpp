#include <gl/gl.h>
#include "../include/glcorearb.h"
#include "generated/glFunctions.cpp"

struct ApplicationState
{
	GLuint program;
	GLuint vao;
};

static const char* VS_SOURCE = R"(
#version 330

void main()
{
	gl_Position = vec4(0.0, 0.0, 0.0, 1.0);
}
)";

static const char* FS_SOURCE = R"(
#version 330

out vec4 color;

void main()
{
	color = vec4(0.8, 0.0, 0.0, 1.0);
}
)";

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

	glGetShaderInfoLog(
		shader, maxLogLength, &logLength, log);

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

	glGetProgramInfoLog(
		program, maxLogLength, &logLength, log);

	return false;
}

bool initApplication(ApplicationState& appState)
{
	// cornflower blue
	glClearColor(
		0.3921568627451f, 0.5843137254902f, 0.9294117647059f, 1.0f);
	glPointSize(10.0f);

	GLsizei maxLogLength = 1024;
	GLchar infoLog[1024];
	GLsizei logLength;

	auto vs = glCreateShader(GL_VERTEX_SHADER);
	auto fs = glCreateShader(GL_FRAGMENT_SHADER);

	if (!compileShaderChecked(vs, VS_SOURCE, maxLogLength, infoLog, logLength))
	{
		return false;
	}
	if (!compileShaderChecked(fs, FS_SOURCE, maxLogLength, infoLog, logLength))
	{
		return false;
	}

	auto program = glCreateProgram();
	glAttachShader(program, vs);
	glAttachShader(program, fs);
	if (!linkProgramChecked(program, maxLogLength, infoLog, logLength))
	{
		return false;
	}
	glDetachShader(program, vs);
	glDetachShader(program, fs);

	glDeleteShader(vs);
	glDeleteShader(fs);

	GLuint vao;
	glGenVertexArrays(1, &vao);

	appState.program = program;
	appState.vao = vao;

	return true;
}

void updateApplication(ApplicationState& appState)
{
	glBindVertexArray(appState.vao);
	glUseProgram(appState.program);
	glClear(GL_COLOR_BUFFER_BIT);
	glDrawArrays(GL_POINTS, 0, 1);
}

void destroyApplication(ApplicationState& appState)
{
	glDeleteProgram(appState.program);
	glDeleteVertexArrays(1, &appState.vao);
}

