#include "Redflag/Assets/AssetManager.h"
#include <cassert>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <random>
#include <stdexcept>

namespace fs = std::filesystem;
using namespace Redflag;

struct TestAsset
{
	inline static int Loads = 0;
	fs::path Path;

	static Ref<TestAsset> Load(const std::string& path)
	{
		++Loads;
		if (!fs::is_regular_file(path))
			return nullptr;
		auto asset = CreateRef<TestAsset>();
		asset->Path = path;
		return asset;
	}
};

struct FailedAsset
{
	inline static int Attempts = 0;
	static Ref<FailedAsset> Load(const std::string&)
	{
		++Attempts;
		return nullptr;
	}
};

namespace Redflag
{
	template<> struct AssetTraits<TestAsset>
	{
		static constexpr AssetType Type = AssetType::Model;
	};
	template<> struct AssetTraits<FailedAsset>
	{
		static constexpr AssetType Type = AssetType::Texture;
	};
}

// 测试只在本次创建的临时目录读写，不修改项目资产。
struct TestFiles
{
	fs::path Root = fs::current_path() / "bin-int/Tests" / ("assets-" + std::to_string(std::random_device{}()));

	TestFiles() { assert(fs::create_directory(Root)); }
	~TestFiles() { fs::remove_all(Root); }

	fs::path Write(const std::string& name, const std::string& content = "test")
	{
		auto path = Root / name;
		std::ofstream out(path);
		out << content;
		out.close();
		assert(out);
		return path;
	}
};

template<typename F>
void ExpectError(F action)
{
	bool failed = false;
	try { action(); }
	catch (const std::runtime_error&) { failed = true; }
	assert(failed);
}

int main()
{
	TestFiles files;
	auto source = files.Write("model.asset");
	auto& library = AssetManager::GetLibrary();

	// 路径别名与重新扫描都指向同一个持久 ID。
	auto id = AssetManager::Add<TestAsset>(source.string());
	assert(id != 0 && fs::exists(source.string() + ".rfmeta"));
	assert(AssetManager::Add<TestAsset>(fs::relative(source).string()) == id);
	assert(AssetManager::Add<TestAsset>((files.Root / "." / "model.asset").string()) == id);
	auto upperPath = source.string();
	std::transform(upperPath.begin(), upperPath.end(), upperPath.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
	assert(AssetManager::Add<TestAsset>(upperPath) == id);
	auto first = AssetManager::Load<TestAsset>(id);
	auto second = AssetManager::Load<TestAsset>(source.string());
	assert(first == second && TestAsset::Loads == 1);
	library.Clear();
	library.Scan(files.Root);
	assert(library.Get(id)->Path == source);
	assert(AssetManager::Load<TestAsset>(id) == first);

	// 卸载只移除缓存引用。
	AssetManager::Unload<TestAsset>(id);
	assert(!AssetManager::IsLoaded<TestAsset>(id));
	assert(first.use_count() == 2);
	auto third = AssetManager::Load<TestAsset>(id);
	assert(third != first && TestAsset::Loads == 2);
	AssetManager::ClearAll();
	assert(!AssetManager::IsLoaded<TestAsset>(id));
	assert(first && second && third);

	// 文件与元数据一起移动，重新扫描后仍通过原 ID 加载。
	fs::create_directory(files.Root / "moved");
	auto moved = files.Root / "moved" / "renamed.asset";
	fs::rename(source, moved);
	fs::rename(source.string() + ".rfmeta", moved.string() + ".rfmeta");
	library.Scan(files.Root);
	assert(library.Get(id)->Path == moved);
	assert(AssetManager::Add<TestAsset>(moved.string()) == id);
	assert(AssetManager::Load<TestAsset>(id)->Path == moved);

	// 外部元数据错误不能污染已经有效的资源库。
	auto broken = files.Write("broken.asset");
	files.Write("broken.asset.rfmeta", "not an asset");
	ExpectError([&] { library.Scan(files.Root); });
	assert(library.Get(id)->Path == moved);
	ExpectError([&] { AssetManager::Add<TestAsset>(broken.string()); });
	fs::remove(broken.string() + ".rfmeta");

	auto duplicate = files.Write("duplicate.asset");
	fs::copy_file(moved.string() + ".rfmeta", duplicate.string() + ".rfmeta");
	ExpectError([&] { library.Scan(files.Root); });
	assert(library.Get(id)->Path == moved);
	ExpectError([&] { AssetManager::Add<TestAsset>(duplicate.string()); });
	fs::remove(duplicate.string() + ".rfmeta");

	// 类型不匹配不调用加载器。加载失败不写缓存，可以再次尝试。
	assert(!AssetManager::Load<FailedAsset>(id));
	assert(FailedAsset::Attempts == 0);
	ExpectError([&] { AssetManager::Add<FailedAsset>(moved.string()); });
	auto texture = files.Write("texture.asset");
	auto failedID = AssetManager::Add<FailedAsset>(texture.string());
	assert(!AssetManager::Load<FailedAsset>(failedID));
	assert(!AssetManager::Load<FailedAsset>(failedID));
	assert(FailedAsset::Attempts == 2);
	assert(!AssetManager::IsLoaded<FailedAsset>(failedID));

	assert(!AssetManager::Load<TestAsset>(AssetID{0}));
	assert(!AssetManager::Load<TestAsset>(""));
	assert(!AssetManager::Load<TestAsset>((files.Root / "missing.asset").string()));
	assert(!fs::exists(files.Root / "missing.asset.rfmeta"));
	AssetManager::ClearAll();
	fs::remove(moved);
	library.Scan(files.Root);
	assert(!library.Get(id));
	assert(!AssetManager::Load<TestAsset>(id));
	library.Clear();
	std::cout << "Asset tests passed: identity, moves, metadata errors, types and cache ownership\n";
}
