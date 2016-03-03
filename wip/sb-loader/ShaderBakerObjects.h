enum struct ShaderType
{
	Vertex,
	TessControl,
	TessEval,
	Geometry,
	Fragment,
	Compute,
};

struct Shader
{
	StringReference name;
	ShaderType type;
	StringReference path;
};

struct Program
{
	StringReference name;
	Shader **attachedShadersBegin, **attachedShadersEnd;
};

enum struct DrawPrimitive
{
	Points,

	Lines,
	LineStrip,
	LineLoop,

	Triangles,
	TriangleStrip,
	TriangleFan,
};

struct RenderConfig
{
	StringReference name;
	Program *program;
	DrawPrimitive primitive;
	unsigned drawCount;
};

struct Shaders
{
	Shader *begin, *end;
};

struct AttachedShaders
{
	Shader **begin, **end;
};

struct Programs
{
	Program *begin, *end;
};

struct RenderConfigs
{
	RenderConfig *begin, *end;
};

struct ShaderBakerObjects
{
	void *memoryBlock;

	char* stringPool;
	Shaders shaders;
	Programs programs;
	AttachedShaders attachedShaders;
	RenderConfigs renderConfigs;
};

Shader* findShaderWithName(Shader *begin, Shader *end, StringSlice name)
{
	while (begin != end)
	{
		if (stringReferenceDeref(begin->name) == name)
		{
			return begin;
		}
		++begin;
	}
	return nullptr;
}

inline Shader* findShaderWithName(Shader *begin, Shader *end, StringReference name)
{
	return findShaderWithName(begin, end, stringReferenceDeref(name));
}

Program* findProgramWithName(Program *begin, Program *end, StringSlice name)
{
	while (begin != end)
	{
		if (stringReferenceDeref(begin->name) == name)
		{
			return begin;
		}
		++begin;
	}
	return nullptr;
}

inline Program* findProgramWithName(Program *begin, Program *end, StringReference name)
{
	return findProgramWithName(begin, end, stringReferenceDeref(name));
}

RenderConfig* findRenderConfigWithName(RenderConfig *begin, RenderConfig *end, StringSlice name)
{
	while (begin != end)
	{
		if (stringReferenceDeref(begin->name) == name)
		{
			return begin;
		}
		++begin;
	}
	return nullptr;
}

RenderConfig* findRenderConfigWithName(
	RenderConfig *begin, RenderConfig *end, StringReference name)
{
	return findRenderConfigWithName(begin, end, stringReferenceDeref(name));
}

