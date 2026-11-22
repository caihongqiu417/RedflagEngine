#include "Redflag/Core/JobSystem.h"

#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace Redflag;

void Check(bool ok, const char* message)
{
	if (!ok)
		throw std::runtime_error(message);
}

#ifdef TEST_PUMP
void CheckContract()
{
	JobSystem jobs(1);
	void (JobSystem::*originalPump)() = &JobSystem::Pump;
	int calls = 0;

	jobs.Submit(RF_JOB("parent"), JobThread::Main, [&] {
		++calls;
		jobs.Submit(RF_JOB("child"), JobThread::Main, [&] { ++calls; });
	});
	jobs.Submit(RF_JOB("second"), JobThread::Main, [&] { ++calls; });

	jobs.Pump(0);
	Check(calls == 0, "zero budget must leave all jobs pending");
	jobs.Pump(1);
	Check(calls == 1, "one budget must execute exactly one ready job");
	(jobs.*originalPump)();
	Check(calls == 3, "the original no-argument Pump must finish jobs already ready");

	// 即使这一帧还有预算，执行回调时才加入的任务仍留给下一帧。
	jobs.Submit(RF_JOB("append"), JobThread::Main, [&] {
		++calls;
		jobs.Submit(RF_JOB("later"), JobThread::Main, [&] { ++calls; });
	});
	jobs.Pump(100);
	Check(calls == 4, "newly submitted callback belongs to the next batch");
	jobs.Pump(100);
	Check(calls == 5, "next pump must execute deferred callback");

	bool rejected = false;
	std::thread worker([&] {
		try { jobs.Pump(0); }
		catch (const std::logic_error&) { rejected = true; }
	});
	worker.join();
	Check(rejected, "even a zero-budget call must enforce main-thread ownership");
}
#else
void CheckContract()
{
	JobSystem jobs(1);
	jobs.Trace(true);
	Job old = jobs.Submit(RF_JOB("finished"), JobThread::Main, [] {});
	jobs.Wait(old);

	// 让任务停在中途，等主线程再次打开计时后才允许它结束。
	std::promise<void> entered, release;
	std::shared_future<void> gate = release.get_future();
	Job flying = jobs.Submit(RF_JOB("flying"), JobThread::Worker, [&] {
		entered.set_value();
		gate.wait();
	});
	entered.get_future().wait();
	jobs.Trace(true);
	release.set_value();
	jobs.Wait(flying);

	std::vector<JobTiming> first = jobs.TakeTimings();
	Check(first.size() == 2, "repeating Trace(true) lost a completed or in-flight job");
	Check(first[0].Name == "finished" && first[1].Name == "flying", "unexpected recorded jobs");

	// 真正关掉再打开，上一轮的任务即使现在才结束，也不应计入新一轮。
	std::promise<void> opened, finish;
	std::shared_future<void> last = finish.get_future();
	Job previous = jobs.Submit(RF_JOB("previous session"), JobThread::Worker, [&] {
		opened.set_value();
		last.wait();
	});
	opened.get_future().wait();
	jobs.Trace(false);
	jobs.Trace(false);
	jobs.Trace(true);
	finish.set_value();
	jobs.Wait(previous);

	Job recent = jobs.Submit(RF_JOB("new session"), JobThread::Main, [] {});
	jobs.Wait(recent);
	std::vector<JobTiming> second = jobs.TakeTimings();
	Check(second.size() == 1 && second[0].Name == "new session", "previous session leaked into the new session");
}
#endif

int main()
{
	try
	{
		CheckContract();
		std::cout << "All behavior checks passed.\n";
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
