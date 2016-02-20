//TODO Need to distinguish between soft and hard parsing errors. For example,
//     a type missing the start of block will likely make the rest of the file
//     impossible to parse, so we should stop parsing immediately (a hard error).
//     However, a type that is unrecognized by the parser can be easily ignored
//     by skipping its block. Currently, the parser returns simple boolean values
//     for failure conditions, which do not have information to distinguish
//     between these cases.

//TODO Clean up skipWhitespace() calls.

//TODO Don't forget, the parser leaves extra single quotes inside paths, so
//     the path parser needs to advance 2 characters when it encounters a
//     single quote

#include <cassert>
#include <cstdlib>

#include "loader.h"
#include "parser.h"

#define ArrayLength(a) sizeof(a) / sizeof(a[0])

struct SubString
{
	size_t begin, length;
};

struct ParseResult
{
	Version version;
	
	ShaderDefinition *shadersBegin;
	ShaderDefinition *shadersEnd;

	ProgramDefinition *programsBegin;
	ProgramDefinition *programsEnd;

	RenderConfig *renderConfigsBegin;
	RenderConfig *renderConfigsEnd;

	ParseError *errorsBegin;
	ParseError *errorsEnd;
};

inline size_t megabytes(size_t n)
{
	return 1024 * 1024 * n;
}

struct MemoryArena
{
	char *begin, *end;
	char *next;
	size_t growSize;
};

bool memoryArenaInit(MemoryArena& arena, size_t initialSize, size_t growSize)
{
	auto memory = (char*) malloc(initialSize);
	if (!memory)
	{
		return false;
	}

	arena.begin = memory;
	arena.end = memory + initialSize;
	arena.next = memory;
	arena.growSize = growSize;
	return true;
}

void memoryArenaDestroy(MemoryArena& arena)
{
	free(arena.begin);
}

void memoryArenaReallocate(MemoryArena& arena)
{
//TODO check for overflow in calculation of newSize
	auto newSize = arena.end - arena.begin + arena.growSize;
	auto memory = (char*) realloc(arena.begin, newSize);
	if (!memory)
	{
//TODO out-of-memory. How should this case be handled? Crash the program?
		memoryArenaDestroy(arena);
		assert(false);
	}

	arena.next = arena.next - arena.begin + memory;
	arena.begin = memory;
	arena.end = memory + newSize;
}

void* memoryArenaAllocate(MemoryArena& arena, size_t size)
{
//TODO check for overflow in calculation of requiredSize
	size_t requiredSize = arena.next - arena.begin + size;

	// Use a while loop in case the allocation is very large. This case should rarely,
	// if ever - the arena's reallocation size should be tuned to prevent this.
	while ((size_t) (arena.end - arena.begin) < requiredSize)
	{
		memoryArenaReallocate(arena);
	}

	auto result = arena.next;
	arena.next += size;

	return result;
}

void memoryArenaFree(MemoryArena& arena, size_t size)
{
	assert((size_t) arena.next - (size_t) arena.begin >= size);
	arena.next -= size;
}

size_t memoryArenaIndex(MemoryArena& arena)
{
	return arena.next - arena.begin;
}



#include <cstdio>

#define ArrayEnd(a) (a) + sizeof(a) / sizeof(a[0])

struct ApplicationState
{
	MemoryArena transientArena;

	ShaderDefinition shaders[1024];
	ProgramDefinition programs[1024];

	// storage for the names of shaders attached to a program
	StringSlice attachedShaderNames[4096];

	RenderConfig renderConfigs[1024];

	// 64 errors should be plenty for a person to deal with at once
	ParseError parseErrors[64];
};

char* parseErrorTypeToStr(ParseErrorType errorType)
{
	switch (errorType)
	{
	case ParseErrorType::MissingVersionStatement:
		return "The Version statement must be the first one";
	case ParseErrorType::MissingMajorVersion:
		return "Missing major version";
	case ParseErrorType::MissingMinorVersion:
		return "Missing minor version";
	case ParseErrorType::VersionMissingDot:
		return "Expected '.' character after major version";
	case ParseErrorType::UnsupportedVersion:
		return "Version is unsupported by this version of Shader Baker";
	case ParseErrorType::MissingBlockBegin:
		return "Missing start of block";
	case ParseErrorType::UnclosedBlock:
		return "Unclosed block";
	case ParseErrorType::MissingPathBegin:
		return "Missing start of path";
	case ParseErrorType::UnclosedPath:
		return "Unclosed path";
	case ParseErrorType::MissingIdentifier:
		return "Statement must begin with an identifier";
	case ParseErrorType::MissingBlockType:
		return "Statement type must follow identifier";
	case ParseErrorType::EmptyTupleWord:
		return "Tuple contains an empty word";
	case ParseErrorType::InvalidWordCharacter:
		return "Invalid character in word";
	case ParseErrorType::RenderConfigMissingProgram:
		return "Missing Program block in a RenderConfig block";
	case ParseErrorType::RenderConfigMultiplePrograms:
		return "Multiple Program blocks declared in a RenderConfig block";
	case ParseErrorType::RenderConfigMissingPrimitive:
		return "Missing Primitive block in a RenderConfig block";
	case ParseErrorType::RenderConfigMultiplePrimitives:
		return "Multiple Primitive blocks declared in a RenderConfig block";
	case ParseErrorType::RenderConfigMissingCount:
		return "Missing Count block in a RenderConfig block";
	case ParseErrorType::RenderConfigMultipleCounts:
		return "Multiple Count blocks declared in a RenderConfig block";
	case ParseErrorType::RenderConfigEmptyProgramName:
		return "Program name is empty";
	case ParseErrorType::RenderConfigEmptyOrInvalidCount:
		return "Count value is empty or invalid";
	case ParseErrorType::UnexpectedBlockType:
		return "Unexpected block type";
	case ParseErrorType::ExceededMaxShaderCount:
		return "Too many shaders!";
	case ParseErrorType::ExceededMaxProgramCount:
		return "Too many programs!";
	case ParseErrorType::ExceededMaxAttachedShaderCount:
		return "Too many attached shaders!";
	case ParseErrorType::ExceededMaxRenderConfigCount:
		return "Too many rendering configurations!";
	default:
		unreachable();
		return "";
	}
}

void printParseErrors(ParseError* begin, ParseError* end)
{
	while (begin != end)
	{
		printf(
			"ERROR %d (line %d, character %d): %s\n",
			(unsigned) begin->type,
			begin->lineNumber,
			begin->charNumber,
			parseErrorTypeToStr(begin->type));
		++begin;
	}
}

char* shaderTypeToStr(ShaderType type)
{
	switch (type)
	{
	case ShaderType::Vertex:
		return "Vertex";
	case ShaderType::TessControl:
		return "Tessalation Control";
	case ShaderType::TessEval:
		return "Tesselation Evaluation";
	case ShaderType::Geometry:
		return "Geometry";
	case ShaderType::Fragment:
		return "Fragment";
	case ShaderType::Compute:
		return "Compute";
	default:
		unreachable();
		return "";
	}
}

void printStringSlice(StringSlice str)
{
	char *c = str.begin;
	while (c != str.end)
	{
		putchar(*c);
		++c;
	}
}

void printParseResult(ParseResult& result)
{
	bool hasErrors = result.errorsBegin != result.errorsEnd;
	if (hasErrors)
	{
		printParseErrors(result.errorsBegin, result.errorsEnd);
	} else
	{
		printf("Parse Successful\n\n");
		auto version = result.version;
		printf("Version %d.%d\n", version.major, version.minor);

		printf("\n\nSHADERS:\n");
		{
			auto shader = result.shadersBegin;
			while (shader != result.shadersEnd)
			{
				printStringSlice(shader->name);
				printf(": %s shader at path \"", shaderTypeToStr(shader->type));
				printStringSlice(shader->path);
				fputs("\"\n", stdout);
				++shader;
			}
		}

		printf("\n\nPROGRAMS:\n");
		{
			auto program = result.programsBegin;
			while (program != result.programsEnd)
			{
				printStringSlice(program->name);
				fputs(": ", stdout);
				auto attachedShader = program->shadersBegin;
				while (attachedShader != program->shadersEnd)
				{
					printStringSlice(*attachedShader);
					fputs(", ", stdout);
					++attachedShader;
				}
				putchar('\n');
				++program;
			}
		}

		printf("\n\nRENDERING CONFIGURATIONS:\n");
		{
			auto config = result.renderConfigsBegin;
			while (config != result.renderConfigsEnd)
			{
				printStringSlice(config->name);
				printf(": renders count=%d ", config->drawCount);
				printStringSlice(config->primitive);
				fputs(" with program '", stdout);
				printStringSlice(config->programName);
				fputs("'\n", stdout);
				++config;
			}
		}
	}
}

static bool readProjectFile(MemoryArena& arena, char *fileName, SubString& result)
{
	auto file = fopen(fileName, "rb");
	if (!file)
	{
		perror("Failed to open project file\n");
		return false;
	}

	result.begin = memoryArenaIndex(arena);
	size_t blockSize = 4096;
	for (;;)
	{
		auto readAddress = memoryArenaAllocate(arena, blockSize);
		auto readSize = fread(readAddress, sizeof(char), blockSize, file);
		if (readSize < blockSize)
		{
			auto extraBytes = blockSize - readSize;
			memoryArenaFree(arena, extraBytes);
			break;
		}
	}
	result.length = memoryArenaIndex(arena) - result.begin;

	auto readFileError = ferror(file);
	if (readFileError)
	{
		perror("Unable to read project file\n");
	} else 
	{
		readFileError = !feof(file);
		if (readFileError)
		{
			puts("Failed to read the entire project file\n");
		}
	}

	if (fclose(file) != 0)
	{
		puts("WARNING: ");
		perror("failed to close project file\n");
	}

	return readFileError == 0;
}

void initParser(ApplicationState& appState, Parser& parser, StringSlice input)
{
	parser.cursor = input.begin;
	parser.end = input.end;
	parser.nextShaderSlot = appState.shaders;
	parser.shadersEnd = ArrayEnd(appState.shaders);
	parser.nextProgramSlot = appState.programs;
	parser.programsEnd = ArrayEnd(appState.programs);
	parser.nextAttachedShaderSlot = appState.attachedShaderNames;
	parser.attachedShadersEnd = ArrayEnd(appState.attachedShaderNames);
	parser.nextRenderConfigSlot = appState.renderConfigs;
	parser.renderConfigsEnd = ArrayEnd(appState.renderConfigs);
	parser.nextErrorSlot = appState.parseErrors;
	parser.errorsEnd = ArrayEnd(appState.parseErrors);
	parser.lineNumber = 1;
	parser.lineBegin = parser.cursor;
}

int main(int argc, char **argv)
{
	if (argc != 2)
	{
		printf("Usage: sb-load projectFile\n");
		return 0;
	}
	auto projectFileName = argv[1];

	auto appState = (ApplicationState*) malloc(sizeof(ApplicationState));
	memoryArenaInit(appState->transientArena, megabytes(1), megabytes(1));

	int result = 0;
	SubString projectFileContents;
	if (!readProjectFile(appState->transientArena, projectFileName, projectFileContents))
	{
		result = 1;
		goto cleanup;
	}

	{
		Parser parser = {};
		StringSlice input = {};
		input.begin = appState->transientArena.begin + projectFileContents.begin;
		input.end = input.begin + projectFileContents.length;
		initParser(*appState, parser, input);
		parse(parser);

		ParseResult parseResult = {};
		parseResult.shadersBegin = appState->shaders;
		parseResult.shadersEnd = parser.nextShaderSlot;
		parseResult.programsBegin = appState->programs;
		parseResult.programsEnd = parser.nextProgramSlot;
		parseResult.renderConfigsBegin = appState->renderConfigs;
		parseResult.renderConfigsEnd = parser.nextRenderConfigSlot;
		parseResult.errorsBegin = appState->parseErrors;
		parseResult.errorsEnd = parser.nextErrorSlot;
		printParseResult(parseResult);
	}

cleanup:
	memoryArenaDestroy(appState->transientArena);
	free(appState);
	return result;
}
