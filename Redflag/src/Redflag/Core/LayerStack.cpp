#include"rfpch.h"
#include"LayerStack.h"

namespace Redflag
{
	LayerStack::LayerStack()
	{
	}

	LayerStack::~LayerStack()
	{
		Clear();
	}

	void LayerStack::Clear()
	{
		for (Layer* layer : m_Layers)
		{
			layer->OnDetach();
			delete layer;
		}
		m_Layers.clear();
		m_LayerInsertIndex = 0;
	}

	void LayerStack::PushLayer(Layer* layer)
	{
		m_Layers.emplace(m_Layers.begin()+m_LayerInsertIndex, layer);
		m_LayerInsertIndex++;
	}

	// 推送覆盖层：直接添加到末尾，所以索引不变
	void LayerStack::PushOverlay(Layer* overlay)
	{
		m_Layers.emplace_back(overlay);
	}

	void LayerStack::PopLayer(Layer* layer)
	{
		auto end = m_Layers.begin() + m_LayerInsertIndex;
		auto it = std::find(m_Layers.begin(), end, layer);
		if (it != end)
		{
			layer->OnDetach();
			m_Layers.erase(it);
			m_LayerInsertIndex--;
		}
	}

	void LayerStack::PopOverlay(Layer* overlay)
	{
		auto begin = m_Layers.begin() + m_LayerInsertIndex;
		auto it = std::find(begin, m_Layers.end(), overlay);
		if (it != m_Layers.end())
		{
			overlay->OnDetach();
			m_Layers.erase(it);
		}
	}
}
