enum struct ElementType
{
	Shader,
	Program,
	RenderConfig
};

struct TextDocumentLocation
{
	unsigned lineNumber;
	unsigned charNumber;
};

struct StringToken
{
	StringSlice value;
	TextDocumentLocation location;
};

struct ShaderElement
{
	StringToken nameToken;
	ShaderType type;
	StringToken pathToken;
};

struct ProgramElement
{
	StringToken nameToken;
	size_t attachedShaderCount;
};

struct RenderConfigElement
{
	StringToken nameToken;
	StringToken programNameToken;
	DrawPrimitive primitive;
	unsigned drawCount;
};

enum struct LoaderErrorType
{
	OutOfElementSpace,
	MissingVersionStatement,
	MissingMajorVersion,
	MissingMinorVersion,
	VersionMissingDot,
	UnsupportedVersion,
	MissingBlockBegin,
	UnclosedBlock,
	MissingPathBegin,
	UnclosedPath,
	MissingIdentifier,
	MissingBlockType,
	EmptyTupleWord,
	InvalidWordCharacter,
	UnexpectedBlockType,
	RenderConfigMissingProgram,
	RenderConfigMultiplePrograms,
	RenderConfigMissingPrimitive,
	UnknownDrawPrimitive,
	RenderConfigMultiplePrimitives,
	RenderConfigMissingCount,
	RenderConfigMultipleCounts,
	RenderConfigEmptyProgramName,
	RenderConfigEmptyOrInvalidCount,
	DuplicateShaderName,
	DuplicateProgramName,
	ProgramUnresolvedAttachedShaderName,
	DuplicateRenderConfigName,
	RenderConfigUnresolvedProgramName,
};

struct LoaderError
{
	LoaderErrorType type;
	TextDocumentLocation location;
};

struct LoaderErrorCollector
{
	LoaderError *next, *end;
};

struct ElementValue
{
	ElementType elementType;

	union
	{
		ShaderElement *shader;

		struct
		{
			ProgramElement *ptr;
			StringToken *attachedShadersBegin;
		} program;

		RenderConfigElement *renderConfig;
	} element;
};

struct StringAllocator
{
	char* next;
};

struct ParseElements
{
	char *begin, *end;
};

struct ParseElementCounts
{
	unsigned shaderCount;
	unsigned programCount;
	unsigned renderConfigCount;
	unsigned attachedShaderCount;
	unsigned stringCount;

	size_t stringPoolSize;
};

inline void unreachable()
{
	assert(false);
}

void addLoaderError(LoaderErrorCollector& errorCollector, LoaderError error)
{
	if (errorCollector.next != errorCollector.end)
	{
		*errorCollector.next = error;
		++errorCollector.next;
	}
}

static StringReference copyStringSlice(StringAllocator& allocator, StringSlice string)
{
	StringReference result{allocator.next};

	auto pStringLength = (size_t*) allocator.next;
	*pStringLength = stringSliceLength(string);
	allocator.next += sizeof(size_t);

	while (string.begin != string.end)
	{
		*allocator.next = *string.begin;
		++string.begin;
		++allocator.next;
	}

	return result;
}

// Paths escape a single quote by including two single quotes in a
// row. This function copies a string, skipping extra single quotes
// in the process.
static StringReference copyRawPath(StringAllocator& allocator, StringSlice string)
{
	StringReference result{allocator.next};

	auto pStringLength = (size_t*) allocator.next;
	allocator.next += sizeof(size_t);

	auto stringBegin = allocator.next;
	while (string.begin != string.end)
	{
		*allocator.next = *string.begin;
		if (*string.begin == '\'')
		{
			++string.begin;
			assert(*string.begin == '\'');
		}
		++string.begin;
		++allocator.next;
	}

	*pStringLength = allocator.next - stringBegin;

	return result;
}

static char* readElementValue(char *pElement, ElementValue &result)
{
	result = {};
	auto pElementType = (ElementType*) pElement;
	result.elementType = *pElementType;
	auto pElementValue = pElementType + 1;
	char *next = nullptr;
	switch (result.elementType)
	{
	case ElementType::Shader:
	{
		auto pShader = (ShaderElement*) pElementValue;
		result.element.shader = pShader;
		next = (char*) (pShader + 1);
	} break;
	case ElementType::Program:
	{
		auto pProgram = (ProgramElement*) pElementValue;
		auto attachedShadersBegin = (StringToken*) (pProgram + 1);
		result.element.program = {pProgram, attachedShadersBegin};
		next = (char*) (attachedShadersBegin + pProgram->attachedShaderCount);
	} break;
	case ElementType::RenderConfig:
	{
		auto pRenderConfig = (RenderConfigElement*) pElementValue;
		result.element.renderConfig = pRenderConfig;
		next = (char*) (pRenderConfig + 1);
	} break;
	default:
	{
		unreachable();
	} break;
	}

	return next;
}

static ParseElementCounts countParseElements(ParseElements const& elements)
{
	ParseElementCounts result = {};

	auto pElements = elements.begin;
	while (pElements != elements.end)
	{
		ElementValue elementValue;
		pElements = readElementValue(pElements, elementValue);

		switch (elementValue.elementType)
		{
		case ElementType::Shader:
		{
			auto pShader = elementValue.element.shader;
			++result.shaderCount;
			result.stringCount += 2;
			result.stringPoolSize += stringSliceLength(pShader->nameToken.value);
			result.stringPoolSize += stringSliceLength(pShader->pathToken.value);
		} break;
		case ElementType::Program:
		{
			auto pProgram = elementValue.element.program.ptr;
			auto attachedShadersBegin = elementValue.element.program.attachedShadersBegin;
			auto attachedShadersEnd = attachedShadersBegin + pProgram->attachedShaderCount;
			auto attachedShaderCount = (unsigned) (attachedShadersEnd - attachedShadersBegin);

			++result.programCount;
			result.attachedShaderCount += attachedShaderCount;
			++result.stringCount;
			result.stringPoolSize += stringSliceLength(pProgram->nameToken.value);
		} break;
		case ElementType::RenderConfig:
		{
			auto pRenderConfig = elementValue.element.renderConfig;
			++result.renderConfigCount;
			++result.stringCount;
			result.stringPoolSize += stringSliceLength(pRenderConfig->nameToken.value);
		} break;
		default:
			unreachable();
			break;
		}
	}

	return result;
}

static void processShaders(
	ParseElements const& elements,
	LoaderErrorCollector& errorCollector,
	StringAllocator& stringAllocator,
	Shader *shaderStorage)
{
	auto pElements = elements.begin;
	auto nextShader = shaderStorage;
	while (pElements != elements.end)
	{
		ElementValue elementValue;
		pElements = readElementValue(pElements, elementValue);

		switch (elementValue.elementType)
		{
		case ElementType::Shader:
		{
			auto pShader = elementValue.element.shader;

			auto shaderName = copyStringSlice(stringAllocator, pShader->nameToken.value);
			auto shaderPath = copyRawPath(stringAllocator, pShader->pathToken.value);

			if (findShaderWithName(shaderStorage, nextShader, shaderName) != nullptr)
			{
				addLoaderError(
					errorCollector,
					LoaderError{LoaderErrorType::DuplicateShaderName, pShader->nameToken.location});
			}

			*nextShader = {};
			nextShader->name = shaderName;
			nextShader->type = pShader->type;
			nextShader->path = shaderPath;
			++nextShader;
		} break;
		}
	}
}

static void processPrograms(
	ParseElements const& elements,
	LoaderErrorCollector& errorCollector,
	StringAllocator& stringAllocator,
	Shaders shaders,
	Program *programStorage,
	Shader **attachedShaderStorage)
{
	auto pElements = elements.begin;
	auto nextProgram = programStorage;
	auto nextAttachedShader = attachedShaderStorage;
	while (pElements != elements.end)
	{
		ElementValue elementValue;
		pElements = readElementValue(pElements, elementValue);

		switch (elementValue.elementType)
		{
		case ElementType::Program:
		{
			auto pProgram = elementValue.element.program.ptr;
			auto attachedShaderNameTokensBegin = elementValue.element.program.attachedShadersBegin;
			auto attachedShaderNameTokensEnd = attachedShaderNameTokensBegin + pProgram->attachedShaderCount;

			auto programName = copyStringSlice(stringAllocator, pProgram->nameToken.value);
			if (findProgramWithName(programStorage, nextProgram, programName) != nullptr)
			{
				addLoaderError(
					errorCollector,
					LoaderError{LoaderErrorType::DuplicateProgramName, pProgram->nameToken.location});
			}

			auto attachedShadersBegin = nextAttachedShader;
			while (attachedShaderNameTokensBegin != attachedShaderNameTokensEnd)
			{
				auto attachedShader = findShaderWithName(
					shaders.begin, shaders.end, attachedShaderNameTokensBegin->value);
				if (attachedShader == nullptr)
				{
					addLoaderError(
						errorCollector,
						LoaderError{
							LoaderErrorType::ProgramUnresolvedAttachedShaderName,
							attachedShaderNameTokensBegin->location});
					*nextAttachedShader = nullptr;
				} else
				{
					*nextAttachedShader = attachedShader;
				}

				++attachedShaderNameTokensBegin;
				++nextAttachedShader;
			}

//TODO Check that only one of each shader type (vertex, fragment, etc) is attached

			*nextProgram = {};
			nextProgram->name = programName;
			nextProgram->attachedShadersBegin = attachedShadersBegin;
			nextProgram->attachedShadersEnd = nextAttachedShader;
			++nextProgram;
		} break;
		}
	}
}

static void processRenderConfigs(
	ParseElements const& elements,
	LoaderErrorCollector& errorCollector,
	StringAllocator& stringAllocator,
	Programs programs,
	RenderConfig *renderConfigStorage)
{
	auto pElements = elements.begin;
	auto nextRenderConfig = renderConfigStorage;
	while (pElements != elements.end)
	{
		ElementValue elementValue;
		pElements = readElementValue(pElements, elementValue);

		switch (elementValue.elementType)
		{
		case ElementType::RenderConfig:
		{
			auto pRenderConfig = elementValue.element.renderConfig;

			auto renderConfigName = copyStringSlice(stringAllocator, pRenderConfig->nameToken.value);
			if (findRenderConfigWithName(renderConfigStorage, nextRenderConfig, renderConfigName) != nullptr)
			{
				addLoaderError(
					errorCollector,
					LoaderError{
						LoaderErrorType::DuplicateRenderConfigName,
						pRenderConfig->nameToken.location});
			}


			auto program = findProgramWithName(
				programs.begin, programs.end, pRenderConfig->programNameToken.value);
			if (program == nullptr)
			{
				addLoaderError(
					errorCollector,
					LoaderError{
						LoaderErrorType::RenderConfigUnresolvedProgramName,
						pRenderConfig->programNameToken.location});
			}

			*nextRenderConfig = {};
			nextRenderConfig->name = renderConfigName;
			nextRenderConfig->program = program;
			nextRenderConfig->primitive = pRenderConfig->primitive;
			nextRenderConfig->drawCount = pRenderConfig->drawCount;
			++nextRenderConfig;
		} break;
		}
	}
}

ShaderBakerObjects processParseElements(
	ParseElements const& elements, LoaderErrorCollector& errorCollector)
{
	auto counts = countParseElements(elements);

	auto stringPoolSize = counts.stringCount * sizeof(size_t) + counts.stringPoolSize;
	auto memoryBlockSize =
		+ counts.shaderCount * sizeof(Shader)
		+ counts.programCount * sizeof(Program)
		+ counts.attachedShaderCount * sizeof(Shader*)
		+ counts.renderConfigCount * sizeof(RenderConfig)
		+ stringPoolSize;
	auto memoryBlock = malloc(memoryBlockSize);

	auto shaderStorage = (Shader*) memoryBlock;
	auto programStorage = (Program*) (shaderStorage + counts.shaderCount);
	auto attachedShaderStorage = (Shader**) (programStorage + counts.programCount);
	auto renderConfigStorage = (RenderConfig*) (attachedShaderStorage + counts.attachedShaderCount);
	auto stringPoolBegin = (char*) (renderConfigStorage + counts.renderConfigCount);

	ShaderBakerObjects result = {};
	result.memoryBlock = memoryBlock;
	result.stringPool = stringPoolBegin;
	result.shaders = {shaderStorage, shaderStorage + counts.shaderCount};
	result.programs = {programStorage, programStorage + counts.programCount};
	result.attachedShaders = {attachedShaderStorage, attachedShaderStorage + counts.attachedShaderCount};
	result.renderConfigs = {renderConfigStorage, renderConfigStorage + counts.renderConfigCount};

	StringAllocator stringAllocator{stringPoolBegin};
	processShaders(elements, errorCollector, stringAllocator, shaderStorage);
	processPrograms(
		elements,
		errorCollector,
		stringAllocator,
		result.shaders,
		programStorage,
		attachedShaderStorage);
	processRenderConfigs(elements, errorCollector, stringAllocator, result.programs, renderConfigStorage);

	return result;
}

