# 阴影、环境光与后处理的教材配图

六张原图均出自 **© Joey de Vries（[@JoeyDeVriez](https://twitter.com/JoeyDeVriez)），LearnOpenGL**。作者在 [About](https://learnopengl.com/About) 页单独说明图片和视频适用 [Creative Commons Attribution 4.0 International](https://creativecommons.org/licenses/by/4.0/)。下表列出原始位置和下载文件的 SHA-256。PDF 在图旁标注作者、章节和许可，单击图片可打开原图。所有 PNG 保留下载时的原始字节，排版时等比缩放。原文未给图号，以下按章节和图意定位。

| 本地文件 | 所在章节与图意 | 原图与尺寸 | SHA-256 |
|---|---|---|---|
| effects-hard.png | [Shadow Mapping，PCF](https://learnopengl.com/Advanced-Lighting/Shadows/Shadow-Mapping)，单点深度比较留下的台阶 | [原图，256×201](https://learnopengl.com/img/advanced-lighting/shadow_mapping_zoom.png) | c7a20b0764bf25ea3830136929ca5a713159810fa63173fabbfc287b4a2b82e7 |
| effects-pcf.png | [Shadow Mapping，PCF](https://learnopengl.com/Advanced-Lighting/Shadows/Shadow-Mapping)，多点比较后影子的远景与近景 | [原图，800×313](https://learnopengl.com/img/advanced-lighting/shadow_mapping_soft_shadows.png) | d2583dfda8a92e20fb74b275e5217111cdb9746f68b1e3b0dc963ca257b8d178 |
| effects-ssao.png | [SSAO，Normal-oriented hemisphere](https://learnopengl.com/Advanced-Lighting/SSAO)，沿表面法线放置采样半球 | [原图，400×209](https://learnopengl.com/img/advanced-lighting/ssao_hemisphere.png) | 47dc2a6aa0a4726dff7c15550082f2bc7e3ffc64d1876f00f4248f6d83f30c33 |
| effects-irradiance.png | [Diffuse irradiance，Irradiance map convolution](https://learnopengl.com/PBR/IBL/Diffuse-irradiance)，收集半球内的环境光 | [原图，350×259](https://learnopengl.com/img/pbr/ibl_hemisphere_sample_normal.png) | e40d9b4d23d0752733440c5cf2bc9ab18203b8f4b262c20760fbf8ec598b92e9 |
| effects-specular.png | [Specular IBL](https://learnopengl.com/PBR/IBL/Specular-IBL)，表面越粗糙，镜面项涉及的方向范围越大 | [原图，800×141](https://learnopengl.com/img/pbr/ibl_specular_lobe.png) | c922350e7643f6ba2ff0cc944264931c41532c17c9dc02b261731daeac596468 |
| effects-bloom.png | [Bloom](https://learnopengl.com/Advanced-Lighting/Bloom)，亮部提取、模糊与合成 | [原图，700×306](https://learnopengl.com/img/advanced-lighting/bloom_steps.png) | 53452f0e14733cfb0b2709b7f55cb44c496944e6df4bfc92d386a0fe873cad99 |

同页的 Cornell Box 图来自本项目已有的 OpenGL 渲染结果，配置、模型与采集经过见[项目场景记录](../../../../output/scenes/README.md)。该项目截图单独署名，教材配图许可只适用于上表列出的六张图片。

## 材质球与环境贴图

下表四幅原图也由 Joey de Vries 以 CC BY 4.0 发布。文件保留原始字节，PDF 中左右并列的两幅材质球取自教程的不同阶段。[作者署名与图片许可](https://learnopengl.com/About)

| 项目文件 | 原图与主题 | SHA-256 |
|---|---|---|
| material-spheres.png | [四盏点光源下的粗糙度与金属度](https://learnopengl.com/img/pbr/lighting_result.png) | ec06de3bac3a0cc2de907a6db8acedf55b537a294e0f283db7616ef2ec8d051d |
| ibl-diffuse-spheres.png | [加入漫反射环境光](https://learnopengl.com/img/pbr/ibl_irradiance_result.png) | 7868d30e4f47abc1bb3d2204926ad1214c72189b6947593e76f652d6bd17f45e |
| ibl-specular-spheres.png | [加入镜面环境反射](https://learnopengl.com/img/pbr/ibl_specular_result.png) | f79a55f0c5c8052c2b12e68ff7e5dc7b62f4ecc35c8e0ab3e8df009564b040f3 |
| ibl-prefilter-map.png | [不同粗糙度下的环境反射图](https://learnopengl.com/img/pbr/ibl_prefilter_map.png) | a3a5d3cc3887956ecee9dd5ff408ba987956025285463796d550bb670deb8e78 |

卡片图和经纬网格图分别由 ibl-card.tex 与 ibl-sky.tex 生成。卡片例题可对读虎书第五版第 5 章图 5.2–5.3，以及 Marschner 的《Radiometry》第 6 页。两条接缝灰度带取自 Redflag 的 OpenGL 环境立方体，采样输入、像素位置与实际结果见项目检查记录。[作者讲义](https://www.cs.cornell.edu/courses/cs6630/2022fa/notes/radiometry-notes.pdf#page=6) · [环境光实测](../../../../output/validation/ibl.md)
