#include "Redflag/Assets/AssetManager.h"
#include <iostream>

// 从标准输入逐行读取场景中的模型 ID，返回 AssetManager 查出的源文件。
int wmain(int argc, wchar_t** argv)
{
    using namespace Redflag;
    try
    {
        if (argc != 2 && !(argc == 4 && std::wstring_view(argv[2]) == L"--texture"))
            throw std::runtime_error("Usage: AssetIndex ASSET_DIRECTORY [--texture PNG]");
        AssetLibrary& library = AssetManager::GetLibrary();
        library.Scan(argv[1]);
        if (argc == 4)
        {
            const AssetID texture = library.Add(argv[3], AssetType::Texture);
            if (!texture) throw std::runtime_error("Texture file is missing");
            std::cout << texture << '\n';
            return 0;
        }
        AssetID id = 0;
        while (std::cin >> id)
        {
            const AssetInfo* found = library.Get(id);
            if (!found || found->Type != AssetType::Model)
                throw std::runtime_error("Unknown model ID " + std::to_string(id));
            std::cout << id << '\t' << found->Path.generic_u8string() << '\n';
        }
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
