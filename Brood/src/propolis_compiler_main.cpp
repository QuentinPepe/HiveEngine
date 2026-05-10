#include <propolis/compiler/codegen.h>
#include <propolis/compiler/graph_validator.h>
#include <propolis/graph/graph.h>
#include <propolis/nodes/node_descriptor.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>

static void PrintUsage(const char* prog)
{
    std::fprintf(stderr, "Usage: %s <input.propolis> <output.cpp> [--name Name] [--namespace ns]\n", prog);
}

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        PrintUsage(argv[0]);
        return 1;
    }

    const char* inputPath = argv[1];
    const char* outputPath = argv[2];

    propolis::CodegenOptions opts;

    for (int i = 3; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--name") == 0 && i + 1 < argc)
        {
            opts.m_systemName = argv[++i];
        }
        else if (std::strcmp(argv[i], "--namespace") == 0 && i + 1 < argc)
        {
            opts.m_namespaceName = argv[++i];
        }
    }

    std::ifstream inFile{inputPath};
    if (!inFile.is_open())
    {
        std::fprintf(stderr, "Error: cannot open input file: %s\n", inputPath);
        return 1;
    }

    std::string json{std::istreambuf_iterator<char>{inFile}, std::istreambuf_iterator<char>{}};
    inFile.close();

    propolis::Graph graph;
    if (!graph.DeserializeFromJson(json.c_str()))
    {
        std::fprintf(stderr, "Error: failed to parse %s\n", inputPath);
        return 1;
    }

    if (opts.m_systemName == "generated_system" && graph.Name().Size() > 0)
    {
        opts.m_systemName = graph.Name();
    }

    propolis::NodeRegistry registry;
    propolis::GraphValidator validator;
    auto validation = validator.Validate(graph, registry);

    if (!validation.m_ok)
    {
        std::fprintf(stderr, "Error: validation failed for %s:\n", inputPath);
        for (size_t i = 0; i < validation.m_errors.Size(); ++i)
        {
            std::fprintf(stderr, "  - %s\n", validation.m_errors[i].m_message.CStr());
        }
        return 1;
    }

    propolis::Codegen codegen;
    wax::String code = codegen.Generate(graph, validation, registry, opts);

    std::ofstream outFile{outputPath};
    if (!outFile.is_open())
    {
        std::fprintf(stderr, "Error: cannot write output file: %s\n", outputPath);
        return 1;
    }

    outFile.write(code.CStr(), static_cast<std::streamsize>(code.Size()));
    outFile.close();

    std::fprintf(stdout, "Compiled %s -> %s\n", inputPath, outputPath);
    return 0;
}
