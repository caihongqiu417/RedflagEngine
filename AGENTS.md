# Redflag 项目记忆

## 用户偏好

- 用户补充要求：代码与命名保持简洁，注释清晰，职责分类清晰；不要防御性编程或冗余代码。优先直接实现明确约定，不添加猜测性的兜底分支、空接口、重复状态、一次性抽象或宏别名。文件解析等外部输入只做必要校验。
- 架构文档使用 LaTeX，架构图使用 TikZ，提交可编辑源码与编译后的 PDF。
- 命名与封装优先参考 TheCherno/Hazel 已核实的公开源码，不凭印象套用商业版本或其他引擎术语。资源集合参考 `ShaderLibrary` 的 `Add / Load / Get / Exists`；资源对象参考 `Texture2D::Create` 与 `Ref<T>`；接口与 OpenGL 实现分开，非模板实现放 cpp。
- 参考源码：https://github.com/TheCherno/Hazel/blob/master/Hazel/src/Hazel/Renderer/Shader.h 、同目录 Shader.cpp 和 Texture.h。Redflag 扩展的稳定资源 ID 不应描述为 Hazel 公开版已有设计。资源登记类使用易懂的 `AssetLibrary`，不另引入 `AssetRegistry` 同义层。

- 用户明确要求沿用早期代码的个人风格。以 Core、Scene 等原有接口为参考：类和函数使用 PascalCase，成员使用 `m_` 前缀，静态状态使用 `s_` 前缀。
- 注释以简短中文为主，按初始化、资源、状态、更新、销毁等职责分组。字段旁只解释用途、单位或约束，不逐句复述代码。
- 复杂算法保留必要的公式、坐标空间、输入输出和生命周期说明；大段架构讨论放在文档，不在每个函数前重复。
- 尊重文件原有缩进和编码，不为统一格式重写整个旧文件。发现乱码应先确认源编码，不批量替换。
- 仅删除经过调用点检查确认无用的代码，不清理用户工作文件、第三方库或未提交改动。

## 架构方向

- 保留 Hazel 风格的轻量 Application、Layer、Scene、Entity 框架。
- 资产逐步分为稳定 ID、AssetLibrary、编辑器导入工具、运行时 AssetManager。旧场景路径需要兼容迁移。
- 渲染逐步分为场景数据提取、每视口资源、显式 Pass、图形后端。先固定 Pass 顺序，再考虑 RenderGraph。
- 贴图、模型等资产与 ShadowMap、历史缓冲等渲染资源分开管理。
- GPU 资源必须在有效图形上下文内释放；场景和 Layer 应先停止，再清理脚本、缓存和渲染器，最后销毁窗口。
- 按可编译、可验证的小步骤迁移；未完成的目标明确记录，不将接口占位视作实现。

## 图形学教材与讲解

- 用户指定长期参考资料：LearnOpenGL、Physically Based Rendering（PBRT）、Real-Time Rendering 第四版（RTR4）、Khronos Vulkan Tutorial、Fundamentals of Computer Graphics。入口与使用约定见 `docs/Graphics-References.md`，讲解前按主题查阅对应原文。
- 概念与公式讲解优先使用这些教材：先解释要解决的问题和直观图示，再定义符号、单位、坐标空间与假设，逐步推导，最后对应到 Redflag 的实际代码。区分严格公式、近似和工程实现。
- 用户允许从这些教材选取配图。引用时注明书名或站点、版次、章节、图号或页码及原始链接；先核实原图与使用条件。重绘标注“据某来源重绘”，自行推导或原创示意图不冒称原书内容。
- 公式使用 LaTeX；不同教材的方向、坐标系、PDF（概率密度）、BRDF 和粗糙度等约定不可直接混用。比较时明确符号对应关系。
- RTR 固定第四版；PBRT 和 Fundamentals of Computer Graphics 的用户版次尚未指定。PBRT 可优先查公开第四版并标明版次，不能混用不同版次的页码。Vulkan 的 latest 页面会变化，使用前核对 API、语言与 SDK 版本。
- 资料库目前是来源索引和讲解约定，不表示已下载或全文阅读所有书籍；未拿到原文时不编造引用、页码或图号。
