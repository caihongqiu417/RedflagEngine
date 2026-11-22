// 方向光传播方向近似平行。角半径决定一束日光能从多少相邻方向照到接收面。
bool usesCascades(uint lamp)
{
    return (shadowWords[lamp] & 0x80000000u) != 0u;
}

float cascadeDepth(uint pixels, ivec2 p, int side, vec2 center, vec2 depthSlope)
{
    if (any(lessThan(p,ivec2(0))) || any(greaterThanEqual(p,ivec2(side)))) return uintBitsToFloat(0x7f800000u);
    float depth = uintBitsToFloat(shadowWords[pixels + uint(p.y*side+p.x)]);
    return depth - dot(vec2(p) - center, depthSlope);
}

float sampleCascade(uint table, int side, vec3 world, vec3 normal, vec3 toLight, float sourceSlope)
{
    mat4 lightVP;
    for (int c=0;c<4;++c)
        for (int r=0;r<4;++r) lightVP[c][r]=uintBitsToFloat(shadowWords[table+uint(4*c+r)]);
    float texel=uintBitsToFloat(shadowWords[table+16u]);
    float depthRange=uintBitsToFloat(shadowWords[table+17u]);
    uint pixels=table+18u;
    vec3 biased=world+normal*texel*(1.-clamp(dot(normal,toLight),0.,1.));
    vec3 ndc=(lightVP*vec4(biased,1)).xyz;
    if (any(greaterThan(abs(ndc),vec3(1)))) return 1.;
    vec2 xy=(ndc.xy*vec2(.5,-.5)+.5)*float(side)-.5;
    float receiver=ndc.z*.5+.5-2.e-6;

    // 灯斜照地面时，相邻两格里的同一地面有不同深度。先补回这段深度差，再判断是否另有物体挡住光。
    mat3 rows = transpose(mat3(lightVP));
    vec2 tilt = vec2(dot(normal,normalize(rows[0])), -dot(normal,normalize(rows[1])));
    vec2 depthSlope = tilt * texel / (max(dot(normal,toLight), .05) * depthRange);
    if (sourceSlope<=0.)
    {
        ivec2 base=ivec2(floor(xy));
        vec2 f=fract(xy);
        float a=mix(float(receiver<=cascadeDepth(pixels,base,side,xy,depthSlope)),
                    float(receiver<=cascadeDepth(pixels,base+ivec2(1,0),side,xy,depthSlope)),f.x);
        float b=mix(float(receiver<=cascadeDepth(pixels,base+ivec2(0,1),side,xy,depthSlope)),
                    float(receiver<=cascadeDepth(pixels,base+ivec2(1),side,xy,depthSlope)),f.x);
        return mix(a,b,f.y);
    }

    // 5×5 搜索限制为 16 个阴影像素的半径。继续增大角半径时，超过这段距离的半影会被截短。
    const float maxRadius=16.;
    float search=clamp(receiver*depthRange*sourceSlope/texel,1.,maxRadius);
    float blockers=0., depthSum=0.;
    for (int y=-2;y<=2;++y)
        for (int x=-2;x<=2;++x)
        {
            ivec2 q=ivec2(floor(xy+.5+vec2(x,y)*(.5*search)));
            float depth=cascadeDepth(pixels,q,side,xy,depthSlope);
            if (depth<receiver) { depthSum+=depth; blockers+=1.; }
        }
    if (blockers==0.) return 1.;
    // 换回米后，遮挡物到接收面的距离乘 tan(光源角半径)，即半影在世界中的半宽。
    float gap=(receiver-depthSum/blockers)*depthRange;
    float radius=clamp(gap*sourceSlope/texel,.5,maxRadius);
    float lit=0.;
    for (int y=-2;y<=2;++y)
        for (int x=-2;x<=2;++x)
        {
            ivec2 q=ivec2(floor(xy+.5+vec2(x,y)*(.5*radius)));
            lit+=float(receiver<=cascadeDepth(pixels,q,side,xy,depthSlope));
        }
    return lit/25.;
}

float cascadeVisibility(uint lamp, vec3 world, vec3 normal, vec3 toLight)
{
    uint first=shadowWords[lamp]&0x7fffffffu;
    uint count=shadowWords[first], side=shadowWords[first+1u];
    float sourceSlope=uintBitsToFloat(shadowWords[first+2u]);
    vec4 depthPlane;
    for (int i=0;i<4;++i) depthPlane[i]=uintBitsToFloat(shadowWords[first+3u+uint(i)]);
    float depth=dot(depthPlane,vec4(world,1));
    for (uint layer=0u;layer<count;++layer)
    {
        if (depth>uintBitsToFloat(shadowWords[first+7u+layer])) continue;
        uint table=first+7u+count+layer*(18u+side*side);
        return sampleCascade(table,int(side),world,normal,toLight,sourceSlope);
    }
    return 1.;
}
