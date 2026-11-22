# 维修站游戏界面

游戏视图读取 hud.yaml。位置和字号按 1920 × 1080 写，画面缩小时等比缩放。Text 指定当前任务或状态的字段，Sprite 指定导入的 PNG。场景属性中的「游戏界面」组件决定使用哪个布局。

面板和血条使用 Kenney 的 UI Pack – Sci-Fi 2.0，准星使用 Kenney 的 Crosshair Pack。保留原始像素，游戏在绘制时加深底板。两个包均以 CC0 1.0 发布。Panel、Plate、Bar 和 Crosshair 分别来自 panel_rectangle、panel_glass_notch_br、bar_square_small 和 Outline/crosshair-019。[UI Pack – Sci-Fi](https://kenney.nl/assets/ui-pack-sci-fi) · [Crosshair Pack](https://kenney.nl/assets/crosshair-pack)

维修工单的图标由 Codex 内置 ImageGen 生成。原图去掉透明边后缩小，最终保存成 128 × 128 PNG。源文件校验值、实际提示词和 ImageMagick 操作记录在图片旁边。[工单图标来源](ServiceTicket.png.rfsource.yaml)
