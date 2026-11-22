#pragma once

#include <future>
#include <yaml-cpp/yaml.h>

namespace Redflag
{
    /* 编辑器、命令行和 MCP 使用同一份源码与任务记录。查询在后台运行，勘误由开发者提交。 */
    class DevContextPanel
    {
      public:
        void DrawMenu();
        void Draw();

      private:
        void Start(const std::string& operation, const std::string& query = {});
        void Receive();
        void DrawCode();
        void DrawRecord();
        void Read(const std::string& kind, const std::string& file = {});
        void DrawDocument();
        void DrawProject();
        void DrawBrowse();

        bool m_Open = false, m_Attach = false, m_RecordPage = false, m_Startup = true;
        std::string m_Operation, m_Task, m_Error, m_Project, m_Kind, m_File;
        int m_First = 0;
        char m_Search[384]{}, m_Correction[2048]{};
        char m_ProjectInput[1024]{};
        YAML::Node m_Tasks, m_Code, m_Record, m_Document;
        std::future<YAML::Node> m_Job;
    };
}
