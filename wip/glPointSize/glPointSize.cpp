#include <windows.h>
#include <gl/gl.h>

extern "C" __declspec(dllexport)
void setPointSize(GLfloat size) {
	glPointSize(size);
}