#include "Redflag/Core/JobSystem.h"
#include "Redflag/Assets/AssetUpdate.h"
#include "Redflag/Terrain/Terrain.h"
#include "Redflag/Scene/Components.h"
#include <entt.hpp>
#include <atomic>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace Redflag;
using Clock = std::chrono::steady_clock;

static void Require(bool pass, const char* message)
{
	if (!pass) throw std::runtime_error(message);
}

static void CheckJobs(JobSystem& jobs)
{
	const auto main = std::this_thread::get_id();
	std::atomic<int> progress{0};
	Job first = jobs.Submit(RF_JOB("Read"), JobThread::Worker, [&] {
		Require(main != std::this_thread::get_id(), "worker affinity");
		progress = 1;
	});
	Job second = jobs.Submit(RF_JOB("Publish"), JobThread::Main, [&] {
		Require(main == std::this_thread::get_id() && progress == 1, "main affinity and dependency");
		progress = 2;
	}, {first});
	jobs.Wait(second);
	Require(progress == 2, "result visible");

	Job missing = jobs.Submit(RF_JOB("Missing asset"), JobThread::Worker, [] { throw std::runtime_error("asset missing"); });
	Job upload = jobs.Submit(RF_JOB("Skip upload"), JobThread::Main, [&] { progress = 3; }, {missing});
	bool failed = false;
	try { jobs.Wait(upload); }
	catch (const std::runtime_error& error) { failed = std::string(error.what()) == "asset missing"; }
	Require(failed && progress == 2, "preserve the load failure and skip upload");
}

static void CheckAssetUpdates(JobSystem& jobs)
{
	AssetUpdate<int> slot;
	int published = 0;
	auto publish = [&](int value) {
		++published;
		return CreateRef<int>(value);
	};
	auto request = [&](AssetUpdate<int>& target, int value) {
		return target.Request(jobs, RF_JOB("Read asset"), RF_JOB("Publish asset"),
			[value] { return value; }, publish);
	};

	request(slot, 1);
	jobs.Wait(request(slot, 2));
	jobs.Drain();
	Require(*slot.Get() == 2 && published == 1, "a superseded request cannot publish");

	request(slot, 3);
	slot.Cancel();
	{
		AssetUpdate<int> closed;
		request(closed, 4);
	}
	jobs.Drain();
	Require(*slot.Get() == 2 && published == 1, "cancel and close keep the last valid resource");

	Job failed = slot.Request(jobs, RF_JOB("Read missing asset"), RF_JOB("Skip publish"),
		[]() -> int { throw std::runtime_error("missing"); }, publish);
	try { jobs.Wait(failed); }
	catch (const std::runtime_error& e) {
		Require(std::string(e.what()) == "missing" && *slot.Get() == 2 && published == 1, "preserve previous asset on failure");
		return;
	}
	throw std::runtime_error("load error did not reach the caller");
}

static void Equal(const std::vector<TerrainChunk>& a, const std::vector<TerrainChunk>& b)
{
	Require(a.size() == b.size(), "chunk count");
	for (size_t i = 0; i < a.size(); ++i)
	{
		Require(a[i].Indices == b[i].Indices && a[i].Vertices.size() == b[i].Vertices.size(), "topology changed");
		Require(std::memcmp(a[i].Vertices.data(), b[i].Vertices.data(), a[i].Vertices.size()*sizeof(MeshVertex)) == 0, "vertex data changed");
	}
}

static void CheckSceneWork(const std::filesystem::path& transforms, const std::filesystem::path& height,
	const std::filesystem::path& out)
{
	entt::registry world;
	std::ifstream input(transforms);
	TransformComponent t;
	while (input >> t.Translation.x >> t.Translation.y >> t.Translation.z >> t.Rotation.x >> t.Rotation.y >> t.Rotation.z
	             >> t.Scale.x >> t.Scale.y >> t.Scale.z)
		world.emplace<TransformComponent>(world.create(),t);
	const auto view = world.view<const TransformComponent>();
	Require(view.size() > 0,"read saved scene transforms");
	std::vector<glm::mat4> serial(view.size()), parallel(view.size());
	std::ofstream csv(out/"ecs.csv");
	csv << "pair,entities,serial_us,parallel_us,load_sync_us,load_job_us\n";
	JobSystem jobs(4);
	auto measure = [](auto work) {
		const auto start = Clock::now();
		work();
		return std::chrono::duration<double,std::micro>(Clock::now()-start).count();
	};
	auto matrices = [&](bool scheduled) {
		return measure([&] {
			if (!scheduled)
			{
				size_t i = 0;
				for (auto entity : view) serial[i++] = view.get<TransformComponent>(entity).GetTransform();
				return;
			}
			// 主线程取得稳定的读取顺序，所有任务结束后才允许增删实体。快照与合并都算入时间。
			std::vector<const TransformComponent*> rows;
			rows.reserve(view.size());
			for (auto entity : view) rows.push_back(&view.get<TransformComponent>(entity));
			std::vector<Job> batch;
			for (unsigned i=0;i<jobs.Workers();++i)
			{
				size_t begin = rows.size()*i/jobs.Workers(), end = rows.size()*(i+1)/jobs.Workers();
				batch.push_back(jobs.Submit(RF_JOB("Scene transforms"),JobThread::Worker,[&,begin,end] {
					for (size_t j=begin;j<end;++j) parallel[j] = rows[j]->GetTransform();
				}));
			}
			jobs.Wait(jobs.Submit(RF_JOB("Matrices ready"),JobThread::Main,[]{},batch));
		});
	};
	auto load = [&](bool scheduled) {
		Ref<Terrain> data;
		double elapsed = measure([&] {
			if (!scheduled) { data=Terrain::Load(height); return; }
			Job read = jobs.Submit(RF_JOB("Decode heightmap"),JobThread::Worker,[&] { data=Terrain::Load(height); });
			jobs.Wait(read);
		});
		Require(data && data->GetWidth()==129,"heightmap load");
		return elapsed;
	};
	for (int i=0;i<62;++i)
	{
		double a,b,c,d;
		if (i%2) { a=matrices(false); b=matrices(true); c=load(false); d=load(true); }
		else { b=matrices(true); a=matrices(false); d=load(true); c=load(false); }
		Require(std::memcmp(serial.data(),parallel.data(),serial.size()*sizeof(glm::mat4))==0,"matrix data changed");
		if (i>=2) csv << i-2 << ',' << view.size() << ',' << a << ',' << b << ',' << c << ',' << d << '\n';
	}
}

static void CheckFirstTerrain(const std::filesystem::path& height, const std::filesystem::path& out)
{
	std::ofstream csv(out/"first-load.csv");
	csv << "pair,serial_us,parallel_us\n";
	auto time = [&](bool workers) {
		const auto start = Clock::now();
		// 模拟本局第一次需要地形。计入新建四个线程，终点为解码后的网格可交给渲染器。
		std::unique_ptr<JobSystem> jobs;
		if (workers) jobs=std::make_unique<JobSystem>(4);
		auto asset=Terrain::Load(height);
		auto chunks=asset->BuildChunks(7.2626f,695.0195f,32,1.f,jobs.get());
		return std::chrono::duration<double,std::micro>(Clock::now()-start).count();
	};
	for (int i=0;i<62;++i)
	{
		double serial,parallel;
		if (i%2) { serial=time(false); parallel=time(true); }
		else { parallel=time(true); serial=time(false); }
		if (i>=2) csv << i-2 << ',' << serial << ',' << parallel << '\n';
	}
}

int main(int argc, char** argv)
{
	if (argc == 2 && std::string(argv[1]) == "--verify")
	{
		JobSystem jobs(2);
		CheckJobs(jobs);
		CheckAssetUpdates(jobs);
		std::cout << "PASS task dependencies, update cancellation, owner lifetime and failure recovery\n";
		return 0;
	}
	if (argc != 4) return 2;
	const std::filesystem::path out = argv[2];
	std::filesystem::create_directories(out);
	std::ofstream csv(out/"terrain.csv");
	csv << "pair,chunk_cells,workers,serial_us,parallel_us,decode_us\n";
	const auto asset = Terrain::Load(argv[1]);
	Require(bool(asset), "read project heightmap");
	for (unsigned workers : {2u, 4u})
	{
		JobSystem jobs(workers);
		CheckJobs(jobs);
		CheckAssetUpdates(jobs);
		for (uint32_t cells : {8u, 32u, 128u})
		{
			auto build = [&](JobSystem* pool) { return asset->BuildChunks(7.2626f, 695.0195f, cells, 1.f, pool); };
			Equal(build(nullptr), build(&jobs));
			for (int pair = 0; pair < 62; ++pair)
			{
				auto time = [&](JobSystem* pool) {
					const auto start = Clock::now();
					const auto chunks = build(pool);
					return std::chrono::duration<double,std::micro>(Clock::now()-start).count();
				};
				double serial, parallel;
				if (pair%2) { parallel=time(&jobs); serial=time(nullptr); }
				else { serial=time(nullptr); parallel=time(&jobs); }
				const auto start = Clock::now();
				const auto loaded = Terrain::Load(argv[1]);
				const double decode = std::chrono::duration<double,std::micro>(Clock::now()-start).count();
				if (pair >= 2) csv << pair-2 << ',' << cells << ',' << workers << ',' << serial << ',' << parallel << ',' << decode << '\n';
			}
		}
		jobs.Trace(true);
		asset->BuildChunks(7.2626f,695.0195f,32,1.f,&jobs);
		const auto records = jobs.TakeTimings();
		Require(records.size()==workers+1 && records.back().Dependencies.size()==workers, "trace links all batches");
	}
	CheckSceneWork(argv[3],argv[1],out);
	CheckFirstTerrain(argv[1],out);
	std::cout << "PASS byte-identical terrain and matrices, dependency order, main-thread publication and failed load\n";
}
