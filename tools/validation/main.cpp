#include "talvisota/content/ContentRegistry.hpp"
#include "talvisota/content/MaterialDefinition.hpp"

#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace
{
void PrintUsage(std::ostream& output)
{
    output << "Talvisota content validator\n"
           << "Usage: talvisota_content_validate <manifest-path> <content-root>\n"
           << "       talvisota_content_validate --help\n";
}
}

int main(int argc, char** argv)
{
    if (argc == 2 && std::string_view(argv[1]) == "--help")
    {
        PrintUsage(std::cout);
        return 0;
    }
    if (argc != 3)
    {
        PrintUsage(std::cerr);
        return 2;
    }

    try
    {
        const std::filesystem::path manifestPath(argv[1]);
        const std::filesystem::path contentRoot(argv[2]);
        talvisota::content::ContentRegistry registry;
        std::vector<talvisota::content::ContentDiagnostic> diagnostics;
        if (!talvisota::content::LoadContentManifest(manifestPath, contentRoot, registry, diagnostics))
        {
            for (const auto& diagnostic : diagnostics)
            {
                std::cerr << diagnostic.file.generic_string();
                if (diagnostic.line != 0U)
                    std::cerr << ':' << diagnostic.line;
                if (!diagnostic.field.empty())
                    std::cerr << " [" << diagnostic.field << ']';
                std::cerr << ": " << diagnostic.message << '\n';
            }
            return 1;
        }

        std::cout << "Validated " << registry.Assets().size() << " content assets.\n";
        for (const std::size_t index : registry.LoadOrder())
            std::cout << "  " << registry.Assets()[index].id << '\n';
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Content validation failed: " << exception.what() << '\n';
        return 1;
    }
}
