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

#include "loader.h"
#include "parser.h"

#include <cstdio>
#include <cstdlib>

#define ArrayLength(a) sizeof(a) / sizeof(a[0])
#define ArrayEnd(a) (a) + sizeof(a) / sizeof(a[0])

struct ApplicationState
{
	char rawContents[1 << 16];
	size_t rawContentsLength;

	ShaderDefinition shaders[1024];
	ProgramDefinition programs[1024];

	// storage for the names of shaders attached to a program
	StringSlice attachedShaderNames[4096];

	RenderConfig renderConfigs[1024];

	// 64 errors should be plenty for a person to deal with at once
	ParseError parseErrors[64];
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

static bool readProjectFile(ApplicationState& appState, char *fileName)
{
	auto file = fopen(fileName, "rb");
	if (!file)
	{
		perror("Failed to open project file\n");
		return false;
	}
	auto fileSize = fread(
		appState.rawContents,
		sizeof(appState.rawContents[0]),
		sizeof(appState.rawContents),
		file);
	appState.rawContentsLength = fileSize;
	auto readFileError = ferror(file);
	if (readFileError)
	{
		perror("Unable to read project file\n");
	} else 
	{
		readFileError = !feof(file);
		if (readFileError)
		{
			perror("Not enough memory to read input file\n");
		}
	}
	if (fclose(file) != 0)
	{
		perror("WARNING: failed to close project file\n");
	}
	if (readFileError)
	{
		return false;
	}

	return true;
}

void initParser(ApplicationState& appState, Parser& parser)
{
	parser.cursor = appState.rawContents;
	parser.end = appState.rawContents + appState.rawContentsLength;
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
		printf("Usage: sbc projectFile\n");
		return 0;
	}
	auto projectFileName = argv[1];

	auto appState = (ApplicationState*) malloc(sizeof(ApplicationState));

	auto result = 0;
	if (!readProjectFile(*appState, projectFileName))
	{
		result = 1;
		goto cleanup;
	}

	{
		Parser parser = {};
		initParser(*appState, parser);
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
	free(appState);
	return result;
}
