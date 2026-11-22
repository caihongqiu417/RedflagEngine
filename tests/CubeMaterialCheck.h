#pragma once

#include "Platform/Vulkan/VulkanDraw.h"
#include "Platform/Vulkan/VulkanTexture2D.h"
#include "Redflag/Renderer/Mesh.h"

static void CheckCubeMaterial(VulkanContext& context, VulkanComputeDevice& device, const fs::path& output)
{
    // 每个面的左上、右上、左下、右下各有一种颜色，翻转或旋转都会改变读数。
    const std::array<glm::u8vec3, 4> colors = {{{255,0,0}, {0,255,0}, {0,0,255}, {255,255,0}}};
    const fs::path chart = output / "cube-quadrants.ppm";
    {
        std::ofstream file(chart, std::ios::binary);
        file << "P6\n8 8\n255\n";
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x)
                file.write(reinterpret_cast<const char*>(&colors[(y / 4) * 2 + x / 4]), 3);
    }
    constexpr int size = 48;
    VulkanDraw raster(context, VulkanDraw::Output::LightingSurfaces);
    raster.Resize(size, size);
    RenderDevice::SetCurrent(&raster);
    Ref<Mesh> cube = Mesh::CreateCube();
    VulkanTexture2D texture(chart.string());
    RenderDevice::SetCurrent(nullptr);

    GIModel model;
    model.MaterialMaps[0].BaseColor = model.MaterialMaps[0].MetallicRoughness = chart;
    model.MaterialFactors.emplace_back(1, 1);
    GlobalIllumination rays(device, std::vector<GIModel>{model}, size, size);
    SurfaceMaterial material;
    material.Metallic = material.Roughness = 1;
    material.MetallicRoughnessMap = texture.GetSampledTexture();
    const uint32_t packCode[] =
#include "Platform/Vulkan/Shaders/PackSurfaces.comp.inc"
    ;
    const auto pack = device.CreatePipeline(packCode, sizeof(packCode),
        {ComputeBinding::Image, ComputeBinding::Image, ComputeBinding::Buffer}, sizeof(glm::mat4));
    std::vector<GISurface> traced(size * size), drawn(size * size);
    const auto packed = device.CreateBuffer(drawn.size() * sizeof(GISurface));
    const glm::mat4 projection = glm::ortho(-.75f, .75f, -.75f, .75f, .1f, 10.f);
    const glm::vec3 normals[] = {{1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1}};
    const char* faces[] = {"+X", "-X", "+Y", "-Y", "+Z", "-Z"};
    std::ofstream csv(output / "cube-material-uv.csv");
    csv << "face,x,y,raster_r,raster_g,raster_b,ray_r,ray_g,ray_b,raster_roughness,ray_roughness,raster_metallic,ray_metallic\n";
    for (int f = 0; f < 6; ++f)
    {
        const glm::vec3 n = normals[f], eye = n * 2.f;
        const glm::vec3 up = n.y ? glm::vec3(0, 0, -n.y) : glm::vec3(0, 1, 0);
        const glm::mat4 vp = projection * glm::lookAt(eye, glm::vec3(0), up);
        rays.SetCamera(vp, eye);
        device.Readback(rays.GetSurfaces(), traced.data(), traced.size() * sizeof(GISurface));
        raster.DrawSurfaces([&](CommandList& commands) {
            commands.DrawIndexed(cube->GetGeometry(), texture.GetSampledTexture(), vp, glm::mat4(1), glm::vec4(1), 1, material);
        });
        const glm::mat4 inverse = glm::inverse(vp);
        device.Dispatch(pack, {raster.GetAlbedo(), raster.GetNormalDepth(), packed}, &inverse, size / 8, size / 8);
        device.Readback(packed, drawn.data(), drawn.size() * sizeof(GISurface));

        for (int q = 0; q < 4; ++q)
        {
            const int x = q % 2 ? 31 : 16, y = q / 2 ? 31 : 16;
            const GISurface& a = drawn[y * size + x];
            const GISurface& b = traced[y * size + x];
            const glm::vec3 expected = glm::vec3(colors[q]) / 255.f;
            csv << faces[f] << ',' << x << ',' << y << ',' << a.Albedo.r << ',' << a.Albedo.g << ',' << a.Albedo.b
                << ',' << b.Albedo.r << ',' << b.Albedo.g << ',' << b.Albedo.b << ',' << a.Normal.w << ',' << b.Normal.w
                << ',' << a.Albedo.w << ',' << b.Albedo.w << '\n';
            for (const GISurface* s : {&a, &b})
                if (s->Position.w != 1 || glm::length(glm::vec3(s->Albedo) - expected) > .0001f ||
                    std::abs(s->Normal.w - expected.g) > .0001f || std::abs(s->Albedo.w - expected.b) > .0001f)
                    throw std::runtime_error(std::string("Cube material UV mismatch at ") + faces[f]);
        }
    }
}
