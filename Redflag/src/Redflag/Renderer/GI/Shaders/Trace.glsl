#ifdef RF_ALPHA_MASK
#include "AlphaMask.glsl"
#endif

bool hitsBounds(Node box, vec3 origin, vec3 inv, bvec3 parallel, float limit)
{
    vec3 lo = (box.lo.xyz - origin) * inv, hi = (box.hi.xyz - origin) * inv;
    vec3 enter = min(lo, hi), leave = max(lo, hi);
    if (any(parallel)) {
        // 射线沿盒面前进时，这一轴不限制距离；若起点在盒外，则永远不会从这一轴进入。
        bvec3 inside = equal(clamp(origin, box.lo.xyz, box.hi.xyz), origin);
        enter = mix(enter, vec3(0), parallel);
        leave = mix(leave, mix(vec3(-1), vec3(limit), inside), parallel);
    }
    // 光线擦过盒子时，浮点舍入可能使它被漏掉。按 PBRT §6.1 略微放宽离开盒子的距离，三角形仍按原来的边求交。
    const float gamma3 = 3. / (16777216. - 3.);
    float first = max(0., max(enter.x, max(enter.y, enter.z)));
    float last = min(leave.x, min(leave.y, leave.z)) * (1. + 2. * gamma3);
    return first <= min(limit, last);
}

float edgeArea(vec2 a, vec2 b)
{
    // 相邻三角形沿相反方向经过同一条边。用相同的两个乘积相减，两边算出的符号才会相反。
    precise float xy = a.x * b.y, yx = a.y * b.x;
    precise float area = xy - yx;
    if (area == 0.) {
        // 两个乘积舍入到同一个数时，用融合乘加取回各自舍掉的部分。
        area = fma(a.x, b.y, -xy) - fma(a.y, b.x, -yx);
    }
    return area;
}

// 找光线最先碰到的表面，要比较各次命中的距离。查点光源是否被挡住，只要在灯前遇到一个不透明三角形就能返回。
int trace(vec3 o, vec3 d, inout float far, bool shadow, out vec2 barycentric)
{
    barycentric = vec2(0);
    bvec3 parallel = equal(d, vec3(0));
    vec3 inv = 1. / mix(d, vec3(1), parallel);
    // 沿射线方向看过去，把三角形投到二维平面。选方向分量最大的轴作深度，避开近乎为零的除数。
    vec3 ad = abs(d);
    int z = ad.x > ad.y ? (ad.x > ad.z ? 0 : 2) : (ad.y > ad.z ? 1 : 2);
    ivec3 axes = ivec3((z + 1) % 3, (z + 2) % 3, z);
    // 一条射线只需算一次投影矩阵，接下来用它投影每个三角形的三个顶点。
    mat3x2 plane = mat3x2(0);
    plane[axes.x] = vec2(1, 0);
    plane[axes.y] = vec2(0, 1);
    plane[z] = -vec2(d[axes.x], d[axes.y]) / d[z];
    int hit = -1;
    uint k = 0u;
    while (k < uint(node.length()))
    {
        Node box = node[k];
        if (!hitsBounds(box, o, inv, parallel, far))
        {
            k = box.range.z;
            continue;
        }
        ++k;
        for (uint i = box.range.x; i < box.range.x + box.range.y; ++i)
        {
            Triangle t = tri[i];
            if (t.a.w == 0.) continue; // 薄玻璃在透明阶段合成，不充当完全遮光的墙。
            vec3 a = t.a.xyz - o, b = t.b.xyz - o, c = t.c.xyz - o;
            precise vec2 pa = plane * a, pb = plane * b, pc = plane * c;
            vec3 areas = vec3(edgeArea(pb, pc), edgeArea(pc, pa), edgeArea(pa, pb));
            if (any(lessThan(areas, vec3(0))) && any(greaterThan(areas, vec3(0)))) continue;
            float sum = areas.x + areas.y + areas.z;
            if (sum == 0.) continue;

            float h = dot(areas, vec3(a[z], b[z], c[z])) / (sum * d[z]);
            if (h > EPS * .25 && h < far)
            {
#ifdef RF_ALPHA_MASK
                // 射线经过镂空处时，保留原来的最远距离，继续找它后面的表面。
                if (t.albedo.w < 0. && !passesAlphaMask(t.albedo.w, areas.yz / sum)) continue;
#endif
                far = h;
                hit = int(i);
                barycentric = areas.yz / sum;
                if (shadow)
                    return hit;
            }
        }
    }
    return hit;
}

bool pointInShadow(vec3 position, vec3 normal, vec3 toLight, float distanceToLight)
{
    float far = distanceToLight - 2. * EPS;
    vec2 weights;
    return trace(position + normal * EPS, toLight, far, true, weights) >= 0;
}
