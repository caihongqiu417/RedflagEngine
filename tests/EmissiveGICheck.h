#pragma once
#include <stb_image_write.h>

// 对平行的灯牌与白墙作面积积分。CPU 从矩形的宽高和两个原始纹素计算，不使用 BVH、着色器或随机数。
static glm::dvec3 IntegratePanel(glm::vec3 wall)
{
    constexpr int nx = 128, ny = 96;
    const double green = 2. * std::pow((128. / 255. + .055) / 1.055, 2.4);
    glm::dvec3 sum(0);
    for (int y = 0; y < ny; ++y)
        for (int x = 0; x < nx; ++x)
        {
            double u = (x + .5) / nx;
            double redWeight = 1. - std::abs(2. * u - .5);
            if (redWeight < 0.) redWeight = -redWeight;
            const glm::dvec3 emission(4. * redWeight, green * (1. - redWeight), 0.);
            const glm::dvec3 delta = glm::dvec3(.4 + 1.2 * u, (y + .5) / ny - .5, 1.) - glm::dvec3(wall);
            double d2 = glm::dot(delta, delta);
            sum += emission * (.8 * delta.z * delta.z / (glm::pi<double>() * d2 * d2));
        }
    return sum * (1.2 / (nx * ny));
}

static void CheckEmissiveGI(Redflag::VulkanComputeDevice& device, const std::filesystem::path& folder,
                            bool reload, RenderDocCapture& capture)
{
    using namespace Redflag;
    constexpr int size = 17, count = size * size;
    std::filesystem::create_directories(folder);
    const auto bake = folder / "panel.rfprobes";
    GIModel wall, panel;
    wall.EntityID = 1;
    wall.Transform = glm::scale(glm::translate(glm::mat4(1), glm::vec3(0,0,-.05)), glm::vec3(2.2,2.2,.1));
    wall.Tint = glm::vec3(.8f);
    panel.EntityID = 2;
    panel.Path = "tests/fixtures/EmissivePanel.gltf";
    panel.Transform = glm::translate(glm::mat4(1), glm::vec3(1,0,1));
    const glm::vec3 eye(.007,0,3);
    const auto camera = glm::ortho(-.25f,.25f,-.25f,.25f,.1f,8.f) * glm::lookAt(eye,glm::vec3(eye.x,0,0),glm::vec3(0,1,0));
    auto read = [&](const GlobalIllumination& gi) {
        std::vector<glm::vec4> result(count);
        device.Readback(gi.GetOutput(), result.data(), result.size()*sizeof(result[0]));
        return result;
    };
    GlobalIllumination gi(device, std::vector<GIModel>{wall,panel}, size,size);
    gi.SetCamera(camera,eye);
    const auto raw = folder / "baked.rgba32f";
    if (reload)
    {
        if (!gi.LoadProbes(bake)) throw std::runtime_error("Light panel bake rejected after a process restart");
        gi.Render(GIMethod::DDGI);
        auto restored = read(gi);
        std::vector<glm::vec4> expected(count);
        const auto bytes = count*sizeof(expected[0]);
        if (!std::ifstream(raw,std::ios::binary).read(reinterpret_cast<char*>(expected.data()),bytes) ||
            std::memcmp(restored.data(),expected.data(),bytes))
            throw std::runtime_error("Light panel bake changed after a process restart");
        std::cout << "Reopened light panel: all " << count << " linear HDR pixels matched.\n";
        return;
    }

    std::vector<GISurface> hits(count);
    device.Readback(gi.GetSurfaces(),hits.data(),hits.size()*sizeof(hits[0]));
    std::ofstream(folder/"receiver.f32",std::ios::binary).write(reinterpret_cast<const char*>(hits.data()),hits.size()*sizeof(hits[0]));
    std::vector<glm::dvec3> reference;
    for (const auto& s:hits)
    {
        if (s.Position.w == 0 || std::abs(s.Position.z) > .0001f || s.Normal.z < .99f)
            throw std::runtime_error("The camera should show only the white receiver: hit W="+std::to_string(s.Position.w)+
                ", Z="+std::to_string(s.Position.z)+", normal Z="+std::to_string(s.Normal.z));
        reference.push_back(IntegratePanel(s.Position));
    }
    std::ofstream csv(folder / "measurements.csv");
    csv << "method,mean_r,mean_g,mean_b,rmse_rgb,red_gradient_rmse\n";
    auto measure = [&](const char* name, const auto& actual, bool compare = true) {
        glm::dvec3 mean(0);
        double error = 0., gradientError = 0.;
        std::vector<glm::vec4> image(count);
        for (int i=0; i<count; ++i)
        {
            glm::dvec3 c(actual[i]), difference = c-reference[i];
            mean += c/double(count);
            error += glm::dot(difference,difference)/(3*count);
            image[i] = glm::vec4(glm::vec3(c),1);
            if (i % size != 0) gradientError += std::pow(difference.r - (actual[i-1].r - reference[i-1].r),2);
            if (i >= size) gradientError += std::pow(difference.r - (actual[i-size].r - reference[i-size].r),2);
        }
        csv << name << ',' << mean.r << ',' << mean.g << ',' << mean.b << ',';
        if (compare) csv << std::sqrt(error) << ',' << std::sqrt(gradientError / (2*size*(size-1)));
        else csv << ',';
        csv << '\n';
        std::ofstream(folder/(std::string(name)+".rgba32f"),std::ios::binary)
            .write(reinterpret_cast<const char*>(image.data()),count*sizeof(image[0]));
        return mean;
    };
    auto expected = measure("CPU-area-integral",reference);
    gi.Render(GIMethod::DirectReference);
    if (measure("no-point-lights",read(gi),false) != glm::dvec3(0)) throw std::runtime_error("Direct light should be zero");
    gi.Render(GIMethod::IndirectReference,2048);
    const auto highImage = read(gi);
    auto high = measure("GPU-2048spp",highImage);

    std::vector<glm::dvec3> average(count);
    constexpr int frames = 512;
    for (int i=0;i<frames;++i)
    {
        if (i==frames-1) capture.Begin();
        gi.Render(GIMethod::ReSTIRGI);
        if (i==frames-1) capture.End();
        auto image = read(gi);
        for (int p=0;p<count;++p) average[p] += glm::dvec3(image[p])/double(frames);
    }
    auto restir = measure("ReSTIR-GI-512-frames",average);
    for (int c=0;c<2;++c)
        if (std::abs(high[c]/expected[c]-1.)>.035 || std::abs(restir[c]/expected[c]-1.)>.10)
            throw std::runtime_error("Offscreen panel disagrees with the independent area integral");
    if (high.b || restir.b) throw std::runtime_error("Black blue channel emitted light");

    for (unsigned rays : {3u,16u,64u})
    {
        gi.SetSeed(137);
        for (int frame=0;frame<24;++frame) gi.Render(GIMethod::DDGI,rays);
        auto label = "DDGI-24-frames-"+std::to_string(rays)+"-rays";
        measure(label.c_str(),read(gi));
    }
    gi.BakeProbes(bake);
    gi.Render(GIMethod::DDGI);
    auto baked = read(gi);
    auto probes = measure("DDGI-bake",baked);
    if (probes.r < .01 || probes.g <= .0001 || probes.b != 0.)
        throw std::runtime_error("Baked probes missed the red/green light panel");
    std::ofstream(raw,std::ios::binary).write(reinterpret_cast<const char*>(baked.data()),count*sizeof(baked[0]));

    auto checkDark = [&](const char* label, const GIModel& changed) {
        GlobalIllumination other(device,std::vector<GIModel>{wall,changed},size,size);
        other.SetCamera(camera,eye);
        if (other.LoadProbes(bake)) throw std::runtime_error("Stale emitter bake accepted");
        other.Render(GIMethod::IndirectReference,32);
        if (measure(label,read(other),false) != glm::dvec3(0)) throw std::runtime_error("Unlit side emitted light");
    };
    auto back = panel;
    back.Transform = glm::rotate(panel.Transform,glm::pi<float>(),glm::vec3(0,1,0));
    checkDark("panel-facing-away",back);
    auto off = panel;
    off.MaterialEmissions = {glm::vec3(0)};
    checkDark("emission-disabled",off);

    // 两份临时 glTF 共用原来的几何与 UV，只把背面打开、或把灯牌贴图换成全蓝。
    std::ifstream source(panel.Path);
    const std::string gltf{std::istreambuf_iterator<char>(source),{}};
    auto variant = [&](const char* name, const std::string& from, const std::string& to) {
        auto json = gltf;
        json.replace(json.find(from),from.size(),to);
        auto copy = panel;
        copy.Path = folder/name;
        std::ofstream(copy.Path) << json;
        return copy;
    };
    auto twoSided = variant("both-sides.gltf", "\"doubleSided\": false", "\"doubleSided\": true");
    twoSided.Transform = back.Transform;
    GlobalIllumination both(device,std::vector<GIModel>{wall,twoSided},size,size);
    both.SetCamera(camera,eye);
    both.Render(GIMethod::IndirectReference,128);
    if (measure("both-sides-facing-away",read(both),false).r <= .01)
        throw std::runtime_error("glTF doubleSided emitter lost its back light");

    auto blue = variant("blue-map.gltf","\"emissiveTexture\": {\n        \"index\": 0",
                          "\"emissiveTexture\": {\n        \"index\": 1");
    // 贴图只有蓝色，而灯牌的蓝色强度为零。换图后灯牌应当熄灭，烘焙时存下的旧光照也不能再用。
    checkDark("changed-emissive-map",blue);

    auto external = panel;
    auto png = folder/"external-panel.png";
    external.EmissionOverrides[0].Image = png;
    const uint8_t bluePixels[]{0,0,255, 0,0,255}, stripes[]{255,0,0, 0,128,0};
    if (!stbi_write_png(png.string().c_str(),2,1,3,bluePixels,6)) throw std::runtime_error("Cannot write external map");
    checkDark("external-blue-map",external);
    if (!stbi_write_png(png.string().c_str(),2,1,3,stripes,6)) throw std::runtime_error("Cannot update external map");
    GlobalIllumination striped(device,std::vector<GIModel>{wall,external},size,size);
    striped.SetCamera(camera,eye);
    striped.Render(GIMethod::IndirectReference,2048);
    const auto externalImage = read(striped);
    measure("external-map-original-uv",externalImage);
    if (externalImage != highImage) throw std::runtime_error("Replacing the texture changed its material UV set");

    external.EmissionOverrides[0].Image = std::filesystem::path{};
    GlobalIllumination noMap(device,std::vector<GIModel>{wall,external},size,size);
    if (noMap.LoadProbes(bake)) throw std::runtime_error("Removing the emissive texture accepted stale probes");
    noMap.SetCamera(camera,eye);
    noMap.Render(GIMethod::IndirectReference,128);
    auto flat = measure("removed-emissive-map",read(noMap),false);
    if (flat.r <= 0 || std::abs(flat.g/flat.r-.5)>.0001) throw std::runtime_error("Removed map lost the RGB factor");

    auto enabledBack = back;
    enabledBack.EmissionOverrides[0].DoubleSided = true;
    GlobalIllumination doubleOverride(device,std::vector<GIModel>{wall,enabledBack},size,size);
    doubleOverride.SetCamera(camera,eye);
    doubleOverride.Render(GIMethod::IndirectReference,128);
    const auto corrected = read(doubleOverride);
    measure("double-sided-override",corrected,false);
    if (corrected != read(both)) throw std::runtime_error("The override and imported doubleSided settings differ");

    // 把点光源放到白墙这一侧，自发光面存在时，普通灯仍须参与直接光计算。
    gi.SetLights({{{0,0,1,1},{1,1,1,0}}});
    gi.Render(GIMethod::DirectReference);
    if (measure("panel-and-point-direct",read(gi),false).b <= 0.) throw std::runtime_error("Point light disappeared");
}
