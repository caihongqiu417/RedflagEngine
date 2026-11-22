#pragma once

#include "Redflag/Core/Core.h"
#include "Redflag/Core/JobSystem.h"
#include <optional>
#include <type_traits>

namespace Redflag
{
	/* 一个资源位置属于一个主线程所有者。后台准备新数据时，场景仍可以使用上一个可用版本。 */
	template<typename Asset>
	class AssetUpdate
	{
	public:
		AssetUpdate() = default;
		AssetUpdate(const AssetUpdate&) = delete;
		AssetUpdate& operator=(const AssetUpdate&) = delete;

		Ref<Asset> Get() const { return m_Slot->Value; }
		void Cancel() { ++m_Slot->Version; }

		/// <summary>read 在工作线程产生独立的 CPU 数据，publish 在主线程创建可供游戏使用的资源。</summary>
		template<typename Read, typename Publish>
		Job Request(JobSystem& jobs, JobSite readSite, JobSite publishSite, Read read, Publish publish)
		{
			using Prepared = std::invoke_result_t<Read&>;
			auto data = std::make_shared<std::optional<Prepared>>();
			const uint64_t version = ++m_Slot->Version;
			std::weak_ptr<Slot> owner = m_Slot;
			Job loaded = jobs.Submit(readSite, JobThread::Worker, [data, read=std::move(read)]() mutable {
				data->emplace(read());
			});

			return jobs.Submit(publishSite, JobThread::Main, [owner,version,data,publish=std::move(publish)]() mutable {
				const auto slot = owner.lock();
				if (slot && slot->Version == version)
					slot->Value = publish(std::move(data->value()));
			}, {loaded});
		}

	private:
		struct Slot
		{
			uint64_t Version = 0;
			Ref<Asset> Value;
		};
		std::shared_ptr<Slot> m_Slot = std::make_shared<Slot>();
	};
}
