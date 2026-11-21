# RenderDoc 应用接口头

`renderdoc_app.h` 原样取自 RenderDoc v1.33，保留文件内 MIT 许可证。来源：

https://github.com/baldurk/renderdoc/blob/v1.33/renderdoc/api/app/renderdoc_app.h

它只供测试可选截帧使用，不链接或附带 RenderDoc DLL。默认运行测试不会加载 RenderDoc；显式使用 `--capture` 时才加载调用方指定的 DLL。
