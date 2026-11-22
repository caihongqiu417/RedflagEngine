#pragma once

#include <memory>

namespace Redflag
{
	struct JobState;

	/* 保存一次提交的身份。后续任务可以依赖它，主线程也可以等待它的结果。 */
	class Job
	{
		friend class JobSystem;
		std::shared_ptr<JobState> m_State;
	};
}
