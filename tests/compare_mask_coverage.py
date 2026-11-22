"""从项目根目录运行 python tests/compare_mask_coverage.py。只使用 CPU。
64×64 画面中心的方片占 38×38 个像素。每四列纹素有一列铁丝，其余三列透明。
比较正式 Vulkan 输出和单点裁剪候选；另算每个像素有多少面积真正被铁丝挡住。
"""
from pathlib import Path
import numpy as np
import json
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
TARGET = ROOT / "output/validation/fine-mask"
far = json.loads((TARGET/"far-after.json").read_bytes())
width = far["projected_quad_width_px"]
x, y = np.meshgrid(np.arange(13, 51), np.arange(13, 51))
left = 32 - width*.5
texels_y = 32 / width


# 与 GI.comp::random01 一致。一个屏幕像素生成两个子像素坐标，不使用帧号。
def pcg(state):
    state = state * np.uint32(747796405) + np.uint32(2891336453)
    word = ((state >> ((state >> np.uint32(28)) + np.uint32(4))) ^ state) * np.uint32(277803737)
    return state, (((word >> np.uint32(22)) ^ word) >> np.uint32(8)).astype(float) / 2**24


seed = x.astype(np.uint32) ^ (y.astype(np.uint32) << np.uint32(16))
seed, ux = pcg(seed)
seed, uy = pcg(seed)


def bilinear_1d(image, texel):
    pos = texel - .5
    left = np.floor(pos).astype(int)
    f = pos-left
    return (1-f)*image[left % image.size] + f*image[(left+1) % image.size]


alpha = np.asarray(Image.open(TARGET/"mask.png"))[0,:,3] / 255.
red = np.asarray(Image.open(TARGET/"opacity.png"))[0,:,0] / 255.
assert np.array_equal(alpha, red) and np.array_equal(alpha, (np.arange(1024)%4)==0)
mips = [alpha]
while mips[-1].size>1:
    # 贴图每缩小一半，相邻两个纹素取平均，再写回 8 位通道。
    mips.append(np.floor(mips[-1].reshape(-1,2).mean(axis=1)*255+.5)/255.)


def keep_sample(base, opacity, texel, factor=1., cutoff=.5):
    # 两张图可有不同的宽度。按同一个 UV 取底色的 A 和透明图的 R，再与材质阈值比较。
    uv = texel / alpha.size
    return bilinear_1d(base, uv * base.size) * factor * bilinear_1d(opacity, uv * opacity.size) >= cutoff


def evaluate(scale, start):
    center = (x+.5-left)*scale+start
    if max(scale,texels_y) <= 1:
        original = bilinear_1d(mips[0], center)>=.5
        candidate = original.copy()
    else:
        lod = min(np.log2(max(scale,texels_y)), len(mips)-1)
        lo=int(np.floor(lod)); hi=int(np.ceil(lod))
        al=(1-(lod-lo))*bilinear_1d(mips[lo],center/2**lo)+(lod-lo)*bilinear_1d(mips[hi],center/2**hi)
        original = al>=.5
        at=center+(ux-.5)*scale
        candidate=keep_sample(alpha, np.ones(1), at)
    # 铁丝横向占 [4k, 4k+1]，求这个像素左右端点之间落在铁丝上的长度。
    def cumulative(t):
        whole=np.floor(t/4.)
        return whole+np.clip(t-whole*4.,0.,1.)
    cover=(cumulative(center+scale/2)-cumulative(center-scale/2))/scale
    return original,candidate,cover


def box_mae(image,truth):
    loc=np.lib.stride_tricks.sliding_window_view(image.astype(float)-truth, (8,8))
    return float(np.abs(loc.mean(axis=(-1,-2))).mean())


def measurements(scale, start):
    raw, test, reference=evaluate(scale,start)
    # 向屏幕右侧多取 1/8 像素对应的纹素。保持屏幕坐标和几何覆盖不动，只考察方片内部的图案。
    moved_raw, moving, moving_ref=evaluate(scale,start+scale/8)
    reference_delta=moving_ref-reference
    def delta_error(first, second):
        return float(np.abs(second.astype(float) - first.astype(float) - reference_delta).mean())
    return dict(pixel_count=int(raw.size), baseline_covered_pixels=int(raw.sum()), candidate_covered_pixels=int(test.sum()),
                ref=float(reference.mean()), baseline=float(raw.mean()), candidate=float(test.mean()),
                baseline_mse=float(((raw-reference)**2).mean()), candidate_mse=float(((test-reference)**2).mean()),
                baseline_8px_mae=box_mae(raw,reference),candidate_8px_mae=box_mae(test,reference),
                baseline_flips_at_eighth_pixel=float((raw!=moved_raw).mean()),
                candidate_flips_at_eighth_pixel=float((test!=moving).mean()),
                ref_abs_change_at_eighth_pixel=float(np.abs(reference_delta).mean()),
                baseline_mse_second_position=float(((moved_raw-moving_ref)**2).mean()),
                candidate_mse_second_position=float(((moving-moving_ref)**2).mean()),
                baseline_temporal_delta_mae=delta_error(raw,moved_raw),candidate_temporal_delta_mae=delta_error(test,moving))


def area_check(scale,start):
    # 条纹沿纵向没有变化。横向均匀取 4096 个点即可独立检验上面的面积积分。
    xs=(x[0]+.5-left)*scale+start
    u=xs[:,None]+(np.arange(4096)[None,:]/4096+.5/4096-.5)*scale
    avg=(bilinear_1d(alpha,u)>=.5).mean(1)
    exact=evaluate(scale,start)[2][0]
    return float(np.abs(exact-avg).max())


scales=[12/width,1.,1.125,1.5,2.,4.,8.,1024/width]
rows=[]
errs=[]
for scale in scales:
    origin=512.125 if scale==scales[0] else 0.
    # 每次沿图像横向平移四分之一纹素，16 次正好走完一组铁丝。
    phases=[measurements(scale,origin+i/4) for i in range(16)]
    combined={"scale_texels_per_px":scale,"center_phase":phases[0],"phases":phases}
    for key in ("candidate","baseline"):
        abs_error=[abs(r[key]-r["ref"]) for r in phases]
        combined[key+"_mean_abs_coverage_error"]=float(np.mean(abs_error))
        combined[key+"_worst_abs_coverage_error"]=float(max(abs_error))
    rows.append(combined)
    errs.extend(area_check(scale,origin+i/4) for i in range(16))
for label,scale,start in [("after",scales[0],512.125),("far-after",scales[-1],0.)]:
    raster=(np.asarray(Image.open(TARGET/(label+"-raster.png")))[1::3,1::3]>127)[13:51,13:51]
    assert np.array_equal(raster,evaluate(scale,start)[0]),label

# A、R 和模型透明度先相乘，再与阈值比较。恰好等于阈值时保留。
u=(x+.5-left)*scales[-1]+(ux-.5)*scales[-1]
for a,r,factor,expected in [(.49,1.,1.,False),(1.,.49,1.,False),(1.,1.,.49,False),
                           (.75,.75,.8,False),(.8,.625,1.,True),(.8,.625,.999,False)]:
    test=keep_sample(np.full(1,a), np.full(1,r), u, factor)
    assert (test==expected).all()
# A 与 R 的实线错开两列时，乘积应处处低于阈值。两张图使用同一组子像素坐标。
opposed=np.roll(alpha,2)
assert not keep_sample(alpha, opposed, u).any()
assert np.array_equal(keep_sample(alpha, np.ones(1), u), keep_sample(np.ones(1), red, u))
assert np.array_equal(evaluate(scales[-1],0)[1],evaluate(scales[-1],0)[1])

report=dict(method="CPU emulation of bilinear repeat at mip 0 and a deterministic PCG sample in a projected pixel. Both previous Vulkan output images in the 38x38 region matched all pixels using the baseline CPU sampler.",
    projection=dict(viewport=[64,64], covered_pixels_shape=[38,38], quad_width_px=width,
                    texture_dimensions=[1024,32], vertical_texels_per_pixel=texels_y, roi="inclusive 13..50 x and y",
                    checked_materials="base A stripe, identical independent R stripe, six constant/tint products, opposing A/R stripe translations"),
    definitions=dict(pixel_MSE="mean((foreground_at_this_position - exact_area_at_this_position)^2), both in [0,1]",
        eight_pixel_MAE="mean abs difference of average coverage in each overlapping 8x8 group inside the 38x38 region",
        shift="Move the UV pattern by the texel width of 1/8 of a horizontal screen pixel; the camera, silhouette, RNG coordinates and per-pixel footprint stay fixed.",
        flips="fraction of screen pixels whose binary foreground changed between the two positions; this includes genuine edge movement",
        temporal_delta_MAE="mean(abs((candidate_t1-candidate_t0) - (exact_t1-exact_t0))) at identical screen coordinates, independently integrating reference at both positions",
        single_sample="PCG from GI.comp::random01; fixed integer pixel position seeds two numbers; both A and R read at same UV; weighted implicit-mip RGB, depth writing and blend state remain as in the formal pipeline."),
    shader_check=dict(compiler="glslc, Vulkan SDK 1.3.275.0, -O --target-env=vulkan1.3; four isolated SPIR-V variants passed spirv-val.",
        independent_modules="Mesh (picking / opaque surfaces / blend) and Emission; compact-ID blend binary SHA256 is identical to original: 86b1f1045bb081e40fce1075aa5c9c3623d3c252159beda3b49ac9779f8af71a.",
        static_work="On the MASK path, two size queries and UV derivatives choose the branch. For minification the tested Mesh and Emission shaders retain a center color read and add one mip0 base read for alpha. Opacity is still sampled once along each path. Two PCG draws select a common offset. No device timing was collected for the candidate."),
    references=["https://research.nvidia.com/labs/rtr/publication/pharr2024stochtex/","https://jcgt.org/published/0009/03/02/"],
    area_reference=dict(exact="integral of the linearly filtered base A >= 0.5 on each horizontal pixel interval",
        high_sample_reference="4096 equally spaced horizontal positions per pixel; all rows identical because fixture stripes are vertical",
        max_abs_error_against_exact=float(max(errs))), rows=rows)
(TARGET/"coverage-candidate.json").write_text(json.dumps(report,indent=2)+"\n")

# 图中白色表示被铁丝完全挡住，参考图里的灰色表示只有部分面积被挡住。
fonts=[Path("C:/Windows/Fonts/msyh.ttc"),Path("/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc")]
fontfile=next((entry for entry in fonts if entry.exists()),None)
def font(size):
    return ImageFont.truetype(str(fontfile),size) if fontfile else ImageFont.load_default(size=size)

labels = (["当前光栅", "单点候选", "像素覆盖参考"], ["近景", "远景"]) if fontfile else (
         ["Current raster","One sample","Reference"],["Near","Far"])
out=Image.new("RGB",(730,517),"white")
draw=ImageDraw.Draw(out)
for col,label in enumerate(labels[0]):
    draw.text((163+col*222,28),label,anchor="mm",font=font(22),fill="#222a33")
for row,(scale,start) in enumerate([(scales[0],512.125),(scales[-1],0.)]):
    originals=evaluate(scale,start)
    top=54+row*227
    draw.text((29,top+100),labels[1][row],anchor="mm",font=font(19),fill="#222a33")
    for col,data in enumerate(originals):
        img=Image.fromarray(np.uint8(data*255+.5)).resize((190,190),Image.Resampling.NEAREST)
        out.paste(img,(68+col*222,top))
        draw.rectangle((67+col*222,top-1,258+col*222,top+190),outline="#a3aab4")
        draw.text((163+col*222,top+204),f"{100*data.mean():.2f}%",anchor="mm",font=font(17),fill="#263443")
legend = "白色表示铁丝遮住整个像素，灰色表示只遮住一部分。" if fontfile else "White: fully covered. Gray: partly covered."
draw.text((365,507),legend,anchor="mm",font=font(13),fill="#45515f")
out.save(TARGET/"coverage-candidate.png")
for r in rows:
    a=r["center_phase"]; print("%.3f: ref %.3f raw %.3f candidate %.3f | mse %.4f / %.4f | 8px mae %.4f / %.4f | flips %.3f / %.3f | phased MAE %.3f / %.3f"%
          (r["scale_texels_per_px"],a["ref"],a["baseline"],a["candidate"],a["baseline_mse"],a["candidate_mse"],a["baseline_8px_mae"],a["candidate_8px_mae"],a["baseline_flips_at_eighth_pixel"],a["candidate_flips_at_eighth_pixel"],r["baseline_mean_abs_coverage_error"],r["candidate_mean_abs_coverage_error"]))
print("Maximum reference difference",max(errs))
