#include "rfpch.h"
#include "JobSystem.h"
#include <condition_variable>
#include <deque>
#include <thread>

namespace Redflag
{
	namespace
	{
		thread_local JobSystem* s_Running = nullptr;
		uint64_t NowUs()
		{
			using namespace std::chrono;
			return duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
		}
	}

	struct JobState
	{
		JobSystem* Owner;
		JobThread Lane;
		uint64_t ID;
		size_t Remaining = 0;
		bool Done = false;
		uint64_t TraceGeneration = 0;
		std::function<void()> Work;
		std::exception_ptr Error;
		std::vector<std::shared_ptr<JobState>> Next;
		std::unique_ptr<JobTiming> Timing;
	};

	struct JobSystem::Queue
	{
		std::mutex Mutex;
		std::condition_variable Wake;
		std::deque<std::shared_ptr<JobState>> Ready[2];
		std::vector<std::thread> Threads;
		const std::thread::id Main = std::this_thread::get_id();
		uint64_t NextID = 0;
		size_t Active = 0;
		bool Stop = false, Trace = false;
		uint64_t TraceGeneration = 0;
		std::vector<JobTiming> Timings;

		std::shared_ptr<JobState> Pop(JobThread lane)
		{
			auto& queue = Ready[static_cast<size_t>(lane)];
			auto item = std::move(queue.front());
			queue.pop_front();
			return item;
		}

		void RequireMain() const
		{
			if (Main != std::this_thread::get_id())
				throw std::logic_error("Pump and Wait belong to the thread that created JobSystem");
		}
	};

	//========== 工作线程 ==========
	JobSystem::JobSystem(unsigned workers) : m_Queue(std::make_unique<Queue>())
	{
		// 渲染、音频及驱动也要用 CPU。宿主可按实测指定线程数，默认最多开四个工作线程。
		if (workers == 0) workers = std::clamp(std::thread::hardware_concurrency(), 1u, 4u);
		Queue& queue = *m_Queue;
		try
		{
			while (queue.Threads.size() < workers)
				queue.Threads.emplace_back([this, &queue] {
					for (;;)
					{
						std::unique_lock<std::mutex> lock(queue.Mutex);
						queue.Wake.wait(lock, [&] { return queue.Stop || !queue.Ready[0].empty(); });
						if (queue.Stop) return;
						auto job = queue.Pop(JobThread::Worker);
						lock.unlock();
						Run(job);
					}
				});
		}
		catch (...)
		{
			{ std::lock_guard<std::mutex> lock(queue.Mutex); queue.Stop = true; }
			queue.Wake.notify_all();
			for (std::thread& worker : queue.Threads) worker.join();
			throw;
		}
	}

	JobSystem::~JobSystem()
	{
		Drain();
		{ std::lock_guard<std::mutex> lock(m_Queue->Mutex); m_Queue->Stop = true; }
		m_Queue->Wake.notify_all();
		for (std::thread& worker : m_Queue->Threads) worker.join();
	}

	unsigned JobSystem::Workers() const { return static_cast<unsigned>(m_Queue->Threads.size()); }

	//========== 任务与依赖 ==========
	Job JobSystem::Submit(JobSite site, JobThread lane, std::function<void()> work, const std::vector<Job>& dependencies)
	{
		Queue& queue = *m_Queue;
		Job job;
		job.m_State = std::make_shared<JobState>();
		JobState& state = *job.m_State;
		state.Owner = this;
		state.Lane = lane;
		state.Work = std::move(work);

		std::lock_guard<std::mutex> lock(queue.Mutex);
		for (const Job& dep : dependencies)
			if (!dep.m_State || dep.m_State->Owner != this)
				throw std::invalid_argument("A dependency belongs to another JobSystem");
		state.ID = ++queue.NextID;
		if (queue.Trace)
		{
			state.TraceGeneration = queue.TraceGeneration;
			state.Timing = std::make_unique<JobTiming>();
			JobTiming& t = *state.Timing;
			t.ID = state.ID; t.QueuedUs = NowUs(); t.Lane = lane;
			t.Name = site.Name; t.File = site.File; t.Line = site.Line;
		}

		// 已完成的任务无需再等待。其余前置任务在结束时各自扣除一次 Remaining。
		for (const Job& dep : dependencies)
		{
			JobState& before = *dep.m_State;
			if (state.Timing) state.Timing->Dependencies.push_back(before.ID);
			if (before.Done)
			{
				if (before.Error) state.Error = before.Error;
			}
			else
			{
				before.Next.push_back(job.m_State);
				++state.Remaining;
			}
		}
		++queue.Active;
		if (state.Remaining == 0) queue.Ready[static_cast<size_t>(lane)].push_back(job.m_State);
		queue.Wake.notify_all();
		return job;
	}

	void JobSystem::Run(const std::shared_ptr<JobState>& job)
	{
		JobTiming* timing = job->Timing.get();
		if (timing)
		{
			timing->Thread = std::hash<std::thread::id>{}(std::this_thread::get_id());
			timing->StartUs = NowUs();
		}
		JobSystem* previous = s_Running;
		s_Running = this;
		try { if (!job->Error) job->Work(); }
		catch (...) { job->Error = std::current_exception(); }
		s_Running = previous;
		job->Work = {};

		std::lock_guard<std::mutex> lock(m_Queue->Mutex);
		if (timing && job->TraceGeneration == m_Queue->TraceGeneration)
		{
			timing->EndUs = NowUs();
			timing->Failed = bool(job->Error);
			m_Queue->Timings.push_back(std::move(*timing));
		}
		job->Done = true;
		for (const auto& next : job->Next)
		{
			if (job->Error) next->Error = job->Error;
			if (--next->Remaining == 0)
				m_Queue->Ready[static_cast<size_t>(next->Lane)].push_back(next);
		}
		job->Next.clear();
		--m_Queue->Active;
		m_Queue->Wake.notify_all();
	}

	//========== 主线程写回与同步等待 ==========
	void JobSystem::Pump()
	{
		Queue& queue = *m_Queue;
		queue.RequireMain();
		std::unique_lock<std::mutex> lock(queue.Mutex);
		size_t remaining = queue.Ready[1].size();
		while (remaining-- && !queue.Ready[1].empty())
		{
			auto job = queue.Pop(JobThread::Main);
			lock.unlock();
			Run(job);
			lock.lock();
		}
	}

	void JobSystem::WaitFor(const JobState* job)
	{
		Queue& queue = *m_Queue;
		queue.RequireMain();
		if (s_Running == this) throw std::logic_error("Submit dependencies instead of waiting inside a job");
		std::unique_lock<std::mutex> lock(queue.Mutex);
		while (job ? !job->Done : queue.Active != 0)
		{
			if (queue.Ready[1].empty())
			{
				queue.Wake.wait(lock);
				continue;
			}
			auto ready = queue.Pop(JobThread::Main);
			lock.unlock();
			Run(ready);
			lock.lock();
		}
		if (job && job->Error) std::rethrow_exception(job->Error);
	}

	void JobSystem::Wait(const Job& job)
	{
		if (!job.m_State || job.m_State->Owner != this) throw std::invalid_argument("Unknown job");
		WaitFor(job.m_State.get());
	}

	void JobSystem::Drain() { WaitFor(nullptr); }

	void JobSystem::Trace(bool enabled)
	{
		std::lock_guard<std::mutex> lock(m_Queue->Mutex);
		m_Queue->Trace = enabled;
		++m_Queue->TraceGeneration;
		m_Queue->Timings.clear();
	}

	std::vector<JobTiming> JobSystem::TakeTimings()
	{
		std::lock_guard<std::mutex> lock(m_Queue->Mutex);
		std::vector<JobTiming> ready;
		ready.swap(m_Queue->Timings);
		return ready;
	}
}
