"""外部 Agent 按任务取需要的工具和项目笔记。参数与返回类型由 MCP 的 tools/list 提供。"""
from project import ENGINE


INSTRUCTIONS = (
    "先用 get_capabilities 查看当前游戏、已编译的工具和试玩配置，再用 get_workflow(topic) 选与任务相关的操作。"
    "用真实回执和观察结果决定下一步。操作权交给人后停止输入。"
)

# 每项只列出该任务常用的入口。skill 的全文由客户端按需读取。
WORKFLOWS = {
    "source": ("读源码、修改并编译",
               "先查公开接口和调用处。用 begin_dev_task 记录要改的文件，再用客户端的文件工具编辑。"
               "选择对应工程和检查，按 run_id 读取退出码及日志，最后核对源码差异。",
               "lookup_dev_context begin_dev_task build_project run_project_check read_build review_dev_task comment_dev_task", []),
    "scene": ("场景、渲染和组件",
              "从场景名查实体和引用的资源。调数值前读取原值、单位和范围，生成修改提案。"
              "保存后重新开局，用同一机位比较实际画面。",
              "list_scenes inspect_scene scene_dependencies inspect_environment get_render_settings inspect_scene_fields "
              "set_zone_description set_scene_exposure propose_asset_change apply_asset_change check_scene",
              ["redflag-scene", "redflag-frame-check", "redflag-material-bake"]),
    "character": ("角色、语音、行为树与动画",
                  "先从角色定义找行为树、动作和声音。若 npc_tool_exists 为假，用 run_project_check 构建 build_npc_tool。"
                  "调整动作前读原有过渡或蒙太奇事件，试玩时同时看骨骼、输入和事件。",
                  "list_npcs inspect_npc create_npc copy_npc update_npc_role set_npc_voice rewire_npc set_npc_animation "
                  "inspect_animation_transitions set_animation_transition inspect_animation_montage set_montage_event_time begin_playtest", []),
    "ui": ("界面布局与图片",
           "读取布局和当前使用的图片，导入有来源的素材或调用项目配置的生图服务。替换指定控件的图片后在游戏中检查。",
           "list_ui_layouts inspect_ui_layout import_ui_image generate_ui_image set_ui_sprite read_playtest_image", []),
    "playtest": ("用当前 Agent 自己的模型试玩",
                 "先读项目的试玩配置，取得动作按键和要观察的事件。开局后读状态和实机图，根据观察输入，"
                 "核对每个命令的回执与下一段位移、事件。由人接管后停止操作。结束后按相同规则评测，可传上一局作比较。"
                 "另有 explore_playtest、revise_playtest 和 iterate_playtest，可委托已配置的本地模型服务。",
                 "inspect_playtest_profile begin_playtest read_playtest read_playtest_image control_playtest evaluate_playtest "
                 "comment_playtest read_job_timeline explore_playtest revise_playtest iterate_playtest", ["redflag-async-performance"]),
    "review": ("比较、保存与撤销资产改动",
               "用资产名、字段、原值与新值提出修改，把依据留给作者。应用时工具重新核对原值。遇到冲突先重读。"
               "需要撤销时交回同一修改编号；Forge 可查看创作案例。",
               "propose_asset_change apply_asset_change list_creation_reviews get_creation_review publish_creation_review", []),
    "package": ("打包场景与本机试玩程序",
                "从已保存的场景追查脚本、模型、贴图等引用，准备完成后生成包。"
                "取回档案路径和清单；需要在本机启动时选择带 Debug 运行程序的包。",
                "list_scenes scene_dependencies package_scene", []),
}


def topics():
    return {key: value[0] for key, value in WORKFLOWS.items()}


def read(topic):
    if topic in WORKFLOWS:
        title, steps, tools, skills = WORKFLOWS[topic]
        return {"topic": topic, "title": title, "steps": steps, "tools": tools.split(), "skills": skills}
    if any(topic in skills for _, _, _, skills in WORKFLOWS.values()):
        path = ENGINE / ".agents/skills" / topic / "SKILL.md"
        return {"skill": topic, "source": path.relative_to(ENGINE).as_posix(), "text": path.read_text(encoding="utf-8")}
    raise ValueError("请从 get_capabilities.workflows 选择主题，或填写该主题返回的 skill 名。")
