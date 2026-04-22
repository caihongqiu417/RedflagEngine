# SSAO 实现文档

SSAO（Screen-Space Ambient Occlusion，屏幕空间环境遮蔽）的核心思路是：
**对屏幕上每个像素，看它周围有多少个方向被遮住了，遮得越多环境光越暗。**

---

## 一、整体流程

```
每帧渲染顺序
┌────────────────────────────┐
│  Shadow Pass（阴影贴图）    │
├────────────────────────────┤
│  Z-Prepass（深度 + 法线）   │ ← 把 view-space 法线写入主 FBO attachment2
├────────────────────────────┤
│  SSAO Pass                 │ ← 读法线 + 深度，输出每像素可见度（0~1）
├────────────────────────────┤
│  主 Pass（PBR 着色）        │ ← 把 SSAO 结果乘到 ambient 上
└────────────────────────────┘
```

SSAO 不走深度重建法线（误差大），而是单独跑一趟 Z-Prepass，直接把法线写出来。

---

## 二、Z-Prepass：获取 View-Space 法线

**文件：** `RedflagForge/assets/shaders/Depth_Prepass.glsl`

Z-Prepass 在主 Pass 之前跑一遍，目的是把每个像素的法线保存下来。

- 顶点着色器把顶点法线变换到 **相机空间（view space）**：
  ```glsl
  v_ViewNormal = mat3(u_View) * normalize(normalMatrix * a_Normal);
  ```
  "相机空间"意思是：以相机为原点，相机朝向为 -Z 轴的坐标系。
  法线转到这个空间里，之后 SSAO shader 不需要再做任何坐标系变换。

- 片元着色器把结果写入 `layout(location=2) out vec4 o_ViewNormal`，
  也就是主 FBO 的 **attachment2**（RGBA16F 格式）。

只有 Z-Prepass 的帧画布开了 attachment2 的写权限（`glColorMaski`），
主 Pass 不会覆盖它，所以 SSAO Pass 读到的是干净的法线。

---

## 三、SSAO Pass：计算遮蔽

**文件：** `RedflagForge/assets/shaders/SSAO.glsl`

SSAO Pass 用一个全屏三角形触发，每个像素跑一次 fragment shader。

### 第一步：从深度图反推相机空间位置

读深度图拿到 `depth`（0~1 的 NDC 深度），然后用逆投影矩阵还原出相机空间的 3D 位置：

```glsl
vec4 ndc   = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
vec4 viewH = u_InvProjection * ndc;
return viewH.xyz / viewH.w;   // 这就是 viewPos：相机空间的 3D 坐标
```

天空（没有几何的地方）深度接近 1.0，直接输出全亮跳过。

### 第二步：建立"以法线为 Z 轴"的局部坐标系（TBN 矩阵）

**为什么需要这一步？**

SSAO 的采样点必须散布在"法线朝向的那一侧半球"里。
如果直接在世界空间随机撒点，有一半会落到几何体内部，那些点永远被遮住，结果全黑。

所以要先建一个以法线 N 为 Z 轴的局部坐标系，让半球的"顶部"永远朝着法线方向。
这个坐标系由三个互相垂直的轴组成：T（切线）、B（副切线）、N（法线），合称 **TBN 矩阵**。

**怎么建？**

最直觉的做法是选一个固定轴（比如世界空间的 Y 轴 `(0,1,0)`），叉乘 N 得到 T：

```glsl
T = normalize(cross(vec3(0,1,0), N));
B = cross(N, T);
```

这在数学上完全正确，但有两个问题：

**问题一：当 N 和固定轴平行时，叉乘结果是零向量**
例如法线刚好朝上（`N = (0,1,0)`），`cross((0,1,0), N) = (0,0,0)`，TBN 矩阵退化。
通常的解法是加一个判断，N 接近 Y 轴时换用 X 轴——多一个分支，而且交界处会有突变。

**问题二（更重要）：所有法线相同的像素，TBN 矩阵完全一样**
同一张平面上所有像素的 N 相同，用固定轴叉乘得到的 T 也相同，TBN 矩阵完全相同。
64 个采样点对每个像素都指向完全相同的方向，最终屏幕上出现清晰的重复纹路（带状噪声）。

---

**实际做法：用噪声图提供随机向量，再 Gram-Schmidt 正交化**

不选固定轴，而是从 4×4 噪声纹理里取一个随机向量 `randomVec`，
每个像素的 `randomVec` 不同，TBN 也不同，64 个采样点的"朝向"就被随机旋转了一圈，
条纹变成均匀噪点。

问题是 `randomVec` 不一定与 N 垂直，不能直接用，需要先把它"投影到与 N 垂直的平面"上：

```glsl
// dot(randomVec, N) 算出 randomVec 在 N 方向上投影了多少
// 减掉这部分投影，剩下的就和 N 垂直了
T = normalize(randomVec - N * dot(randomVec, N));
```

这就是 Gram-Schmidt 正交化——听起来复杂，本质只是"去掉不垂直的分量"。
因为 `randomVec` 是随机的，它几乎不会和 N 平行，所以不会退化。

有了 T：
```glsl
B = cross(N, T);
mat3 TBN = mat3(T, B, N);
```

---

### 第三步：生成采样点（不是走固定距离）

**先理解"采样点"是什么**

SSAO 对每个像素要做的事，可以打个比方：
> 站在当前像素的位置，往周围随机"扔出"64 颗球，看哪些球被附近的几何体挡住了。
> 挡住的越多，说明这里越"闭塞"，环境光越暗。

每一颗"球"就是一个**采样点**，它是相机空间里的一个 3D 坐标。

---

**采样点怎么生成？两阶段：**

**第一阶段（CPU，启动时只跑一次）：生成 64 个"偏移向量"**

```cpp
for (int i = 0; i < 64; ++i)
{
    // 1. 在 Z 朝上的半球里随机取一个方向（x、y 全范围，z 只取正）
    glm::vec3 s(rnd(-1,1), rnd(-1,1), rnd(0,1));

    // 2. 归一化后再乘一个随机长度，让它落在半球内部（而不只是球面上）
    s = normalize(s) * rnd(0,1);

    // 3. 让靠近原点的样本更密集：越靠近表面的采样越精细，远处稀疏一些
    //    scale 从 0 增长到 1，scale*scale 让前期增长很慢（密集在近处）
    float scale = float(i) / 64.0f;
    s *= 0.1f + 0.9f * scale * scale;

    u_Kernel[i] = vec4(s, 0.0f);   // 存到数组里，上传 GPU
}
```

这 64 个向量本身不是采样点，它们是"相对于当前像素位置的偏移量"，
每个向量的方向不同、长度也不同（从很短到 1.0 之间随机分布）。

---

**第二阶段（GPU，每帧每个像素）：把偏移转成实际 3D 坐标**

```glsl
// TBN 把"Z 朝上的偏移"旋转到"当前法线方向"
// radius 整体缩放采样球大小
vec3 samplePos = viewPos + TBN * u_Kernel[i].xyz * radius;
```

分解来看：
- `u_Kernel[i].xyz`：一个偏移向量，在标准半球里（Z 朝上）。
- `TBN * u_Kernel[i].xyz`：旋转后，偏移变成"法线方向"的半球里。
- `* radius`：整体缩放，`radius=0.5` 表示采样半径是 0.5 个相机空间单位。
- `viewPos + ...`：加上当前像素的 3D 位置，得到空间中的一个实际坐标。

**关键点：距离是随机的，不是固定的。**
每个采样点到当前像素的距离由 Kernel 向量的长度决定，不同样本距离各不同，
而且靠近表面的地方样本更密集（因为近处遮蔽最准确，远处影响越来越小）。

### 第四步：深度比较（判断样本是否被遮蔽）

拿着 `samplePos`（一个相机空间的假想点），问：**屏幕上这个位置的真实表面比它更近吗？**

1. 把 `samplePos` 投影到屏幕 UV：
   ```glsl
   vec4 offset = u_Projection * vec4(samplePos, 1.0);
   offset.xy   = offset.xy / offset.w * 0.5 + 0.5;   // NDC → [0,1] UV
   ```

2. 在这个 UV 位置采一次深度图，还原真实表面的相机空间 Z 值：
   ```glsl
   float sceneZ = ReconstructViewPos(offset.xy, texture(u_Depth, offset.xy).r).z;
   ```

3. 比较：
   ```glsl
   occlusion += (sceneZ >= samplePos.z + bias ? 1.0 : 0.0) * rangeCheck;
   ```
   - 相机空间里 Z 是负值，**Z 越大越靠近相机**（越浅）。
   - `sceneZ >= samplePos.z`：真实表面 Z 比样本 Z 大 → 表面在样本的相机侧 → 样本被遮住了。
   - `bias` 是一个小偏移（默认 0.025），防止表面和自己比较时因为浮点误差把自己判断成遮蔽（自我遮蔽伪影）。
   - `rangeCheck = smoothstep(...)` 是一个距离衰减：如果真实表面和当前像素的 Z 差距远超 `radius`，说明这个遮蔽是错误的（远处墙面不应该遮住前方的地板），直接衰减到 0。

### 第五步：输出可见度

```glsl
occlusion = 1.0 - occlusion / float(kernelSize);   // 转为"可见度"：0=全遮，1=全亮
ao = pow(occlusion, u_Params.z);                   // power 参数增强对比度
o_Color = vec4(ao, ao, ao, 1.0);
```

---

## 四、主 Pass 如何使用 SSAO 结果

**文件：** `RedflagForge/assets/shaders/Renderer3D_Mesh.glsl`

主 Pass 在计算 PBR 光照时，读 SSAO 输出图（slot 12）并乘到 `ao` 上：

```glsl
vec2 vpSize = max(u_ShadowParams2.zw, vec2(1.0));
float ssao  = texture(u_SSAOMap, gl_FragCoord.xy / vpSize).r;
ao *= ssao;
// ... 之后 ambient = (kD * diffuseIBL + specularIBL) * ao;
```

`gl_FragCoord.xy / vpSize` 把像素坐标转成 [0,1] UV，直接去 SSAO 纹理上找对应位置。
SSAO 只影响 **环境光（ambient）部分**，不影响直接光照，这是物理正确的：
遮蔽只能挡住来自"各个方向的漫反射环境光"，不能挡住方向光/点光。

关闭 SSAO 时，SceneRenderer 绑一张 1×1 纯白纹理到 slot 12，`ssao = 1.0`，乘法无副作用。

---

## 五、可调参数

| 参数 | 默认值 | 含义 |
|------|--------|------|
| Radius | 0.5 | 采样球半径（相机空间单位）。越大遮蔽范围越广但容易产生 halo |
| Bias | 0.025 | 深度偏移，防止自我遮蔽。太小有条纹，太大遮蔽消失 |
| Power | 2.0 | 幂次调整对比度。越大暗部越暗、明暗分明 |
| Kernel Size | 64 | 每像素采样数。越大越精确但越慢（无模糊，改为多采样） |

---

## 六、为什么没有模糊 Pass

标准 SSAO 通常在输出后跑一次双边滤波来平滑噪点。
这里有意去掉了，原因是：
- 滤波需要额外一张 FBO、一个 Pass、一套参数，实现更复杂。
- 用更多采样点（64 个）可以把噪点控制到可接受范围。
- 对于编辑器预览用途，轻微噪点比实现复杂度更可以接受。

代价是每像素运算量更大（64 次纹理采样 + 深度重建），但现代 GPU 对全屏 Pass 处理很快。
