# 维修站游戏界面

游戏视图读取 hud.yaml。位置和字号按 1920 × 1080 写，画面缩小时等比缩放。Text 指定当前任务或状态的字段，Sprite 指定导入的 PNG。场景属性中的「游戏界面」组件决定使用哪个布局。

任务工单、生命值、队友名字和按键说明直接显示在场景上。浅色文字下有一道偏移 1.5 像素的深色字影，这个距离按 1080p 设定，跟随视图缩放。右下角常显队友通讯和帮助键，按住 F1 才展开完整按键。普通项目也可以在布局里用自己的文字字段、图片和进度条，省略 Nameplates 就可做单纯的仪器界面。[单机游戏界面参考](../../../../docs/Graphics-References.md#单机游戏的界面设计)

生命条使用 Kenney 的 UI Pack – Sci-Fi 2.0，准星使用 Crosshair Pack。Bar 来自 bar_square_small，Crosshair 来自 Outline/crosshair-019，均以 CC0 1.0 发布。资源目录另保留原包的 panel_rectangle 和 panel_glass_notch_br，可供其他布局选用。[UI Pack – Sci-Fi](https://kenney.nl/assets/ui-pack-sci-fi) · [Crosshair Pack](https://kenney.nl/assets/crosshair-pack)

维修工单的图标由 Codex 内置 ImageGen 生成。原图去掉透明边后缩小，最终保存成 128 × 128 PNG。源文件校验值、实际提示词和 ImageMagick 操作记录在图片旁边。[工单图标来源](ServiceTicket.png.rfsource.yaml)
