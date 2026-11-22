# 顶点保存在外部文件的 GLB

这个模型来自项目的两个三角形材质导入测试。GLB 只保存 JSON。三角形的顶点保存在同目录的 `triangle.bin`，两个材质引用的白色贴图是 `white.png`。Khronos 明确允许省略 BIN 块，表 1 将它的出现次数列为零或一。[glTF 2.0 §4.4.3](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#chunks)

本次测试使用项目的 Windows Debug x64 版 Assimp。把相同顶点写进 GLB 自身的 BIN 块，或者把 JSON 保存为普通 `.gltf`，导入均正常，两个材质也各自保留了发光设置。只有 JSON 块的版本在读模型时退出，没得到可绘制的模型。旧版 Assimp 在 `glTF2Asset.inl` 的 `Asset::Load` 中先为 GLB 建立了内部缓冲区，之后才读取文件头，确认文件里是否真的有 BIN 块。这项兼容性问题单独保留，当前正式灯牌使用普通 glTF。
