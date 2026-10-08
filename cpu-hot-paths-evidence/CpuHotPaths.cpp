// Copyright Contributors to the MaterialX Project
// SPDX-License-Identifier: Apache-2.0
#include <MaterialXCore/Document.h>
#include <MaterialXFormat/Util.h>
#include <MaterialXGenGlsl/GlslShaderGenerator.h>
#include <MaterialXGenGlsl/GlslSyntax.h>
#include <MaterialXGenShader/GenContext.h>
#include <MaterialXGenShader/Shader.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace mx = MaterialX;
volatile size_t sink = 0;

template<class F> void measure(const char* label, size_t count, F fn)
{
    for (size_t i = 0; i < 3; ++i) sink += fn();
    const auto start = std::chrono::steady_clock::now();
    size_t sum = 0;
    for (size_t i = 0; i < count; ++i) sum += fn();
    const auto end = std::chrono::steady_clock::now();
    sink += sum;
    std::cout << label << "," << count << ","
              << std::chrono::duration<double, std::nano>(end - start).count() / count
              << "," << sum << std::endl;
}

int main(int argc, char** argv)
try
{
    if (argc < 3) throw std::runtime_error("Usage: CpuHotPaths <source root> <micro|generate|dump> [count|output dir]");
    mx::FileSearchPath search(argv[1]);
    search.append(mx::FilePath(argv[1]) / "libraries");
    auto lib = mx::createDocument();
    mx::loadLibraries({"libraries"}, mx::FileSearchPath(argv[1]), lib);
    const std::string mode = argv[2];
    if (mode == "micro")
    {
        const size_t n = argc > 3 ? std::stoull(argv[3]) : 200000;
        auto syntax = mx::GlslSyntax::create(mx::TypeSystem::create());
        measure("valid_name", n, [&] { std::string s = "ordinary_material_input"; syntax->makeValidName(s); return s.size(); });
        measure("reserved_name", n, [&] { std::string s = "float"; syntax->makeValidName(s); return s.size(); });
        const std::string single = "genglsl", other = "genosl", list = "genosl, genglsl, genmsl";
        measure("target_single_hit", n, [&] { return mx::targetStringsMatch(single, single); });
        measure("target_single_miss", n, [&] { return mx::targetStringsMatch(single, other); });
        measure("target_list", n, [&] { return mx::targetStringsMatch(list, single); });
        auto doc = mx::createDocument();
        doc->setDataLibrary(lib);
        auto node = doc->addNode("standard_surface", "surface", "surfaceshader");
        auto def = lib->getNodeDef("ND_standard_surface_surfaceshader");
        if (!def) throw std::runtime_error("Missing standard surface definition");
        measure("node_def", n, [&] { return node->getNodeDef("genglsl") != nullptr; });
        measure("implementation", n, [&] { return def->getImplementation("genglsl") != nullptr; });
        auto plain = doc->addNodeDef("ND_bench", "float", "bench");
        for (int i = 0; i < 16; ++i) plain->addInput("input" + std::to_string(i), "float");
        measure("active_inputs_16", n, [&] { return plain->getActiveInputs().size(); });
        measure("active_outputs_1", n, [&] { return plain->getActiveOutputs().size(); });
        return 0;
    }
    for (const std::string category : {"standard_surface", "open_pbr_surface"})
    {
        // Both library loading and its lazy lookup initialization are outside timing.
        lib->getMatchingNodeDefs(category);
        auto generator = mx::GlslShaderGenerator::create();
        auto generate = [&]()
        {
            auto doc = mx::createDocument();
            doc->setDataLibrary(lib);
            auto surface = doc->addNode(category, "surface", "surfaceshader");
            auto material = doc->addMaterialNode("material", surface);
            // Fresh context intentionally prevents reuse of generated implementation caches.
            mx::GenContext context(generator);
            context.registerSourceCodeSearchPath(search);
            generator->registerTypeDefs(doc);
            return generator->generate("bench", material, context);
        };
        if (mode == "dump")
        {
            if (argc < 4) throw std::runtime_error("dump requires output directory");
            std::filesystem::create_directories(argv[3]);
            auto shader = generate();
            for (const std::string stage : {mx::Stage::VERTEX, mx::Stage::PIXEL})
            {
                std::ofstream out(std::filesystem::path(argv[3]) / (category + "_" + stage + ".glsl"), std::ios::binary);
                out << shader->getSourceCode(stage);
                if (!out) throw std::runtime_error("Could not write shader");
            }
        }
        else if (mode == "generate")
        {
            size_t n = argc > 3 ? std::stoull(argv[3]) : 100;
            measure(category.c_str(), n, [&] {
                auto shader = generate();
                return shader->getSourceCode(mx::Stage::VERTEX).size() + shader->getSourceCode(mx::Stage::PIXEL).size();
            });
        }
        else throw std::runtime_error("Unknown mode");
    }
}
catch (const std::exception& e)
{
    std::cerr << e.what() << std::endl;
    return 1;
}
