#pragma once

#include "Job.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Redflag
{
	enum class JobThread { Worker, Main };

	struct JobSite
	{
		const char* Name;
		const char* File;
		int Line;
	};

	struct JobTiming
	{
		uint64_t ID, Thread, QueuedUs, StartUs, EndUs;
		std::string Name, File;
		int Line;
		JobThread Lane;
		bool Failed;
		std::vector<uint64_t> Dependencies;
	};

	/* 宿主在游戏线程创建和销毁调度器。后台只处理独立数据，写回场景或创建显卡资源的任务交给 Main。 */
	class JobSystem
	{
	public:
		explicit JobSystem(unsigned workers = 0);
		~JobSystem();
		JobSystem(const JobSystem&) = delete;
		JobSystem& operator=(const JobSystem&) = delete;

		/// <summary>全部前置任务成功后才执行 work。前置任务失败时，等待者可以取得原异常。</summary>
		Job Submit(JobSite site, JobThread thread, std::function<void()> work, const std::vector<Job>& dependencies = {});

		// 宿主每帧调用 Pump。Wait 用于调用者必须先拿到结果才可以继续的情况。
		void Pump();
		void Wait(const Job& job);
		void Drain();
		unsigned Workers() const;

		void Trace(bool enabled);
		std::vector<JobTiming> TakeTimings();

	private:
		struct Queue;
		std::unique_ptr<Queue> m_Queue;
		void Run(const std::shared_ptr<JobState>& state);
		void WaitFor(const JobState* job);
	};
}

#define RF_JOB(name) ::Redflag::JobSite{name, __FILE__, __LINE__}
