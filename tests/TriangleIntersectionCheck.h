#pragma once

// 八个三角形在中心相接。即使调换观察轴、从背面看或斜着看，中间的点仍在同一张平面上。
static void CheckWallDirections(Redflag::VulkanComputeDevice& device, std::ostream& report)
{
    using namespace Redflag;
    const glm::vec3 center(2.25f, -1.75f, 3.5f);
    const glm::vec3 axes[] = {{1,0,0}, {0,1,0}, {0,0,1}};
    constexpr int size = 17;
    std::array<GISurface, size * size> surfaces;
    for (int axis = 0; axis < 3; ++axis)
    {
        const glm::vec3 across = axes[(axis+1)%3], up = axes[(axis+2)%3], normal = axes[axis];
        GIModel wall;
        wall.Path = "tests/fixtures/SharedEdges.obj";
        wall.Transform = { glm::vec4(across,0), glm::vec4(up,0), glm::vec4(normal,0), glm::vec4(center,1) };
        GlobalIllumination gi(device, std::vector<GIModel>{wall}, size, size);
        for (int side : {-1, 1})
        {
            for (bool perspective : {false, true})
            {
                glm::vec3 eye = center + float(side)*3.f*normal;
                if (perspective) eye += .45f*across + .3f*up;
                const glm::mat4 projection = perspective ? glm::perspective(.22f,1.f,.1f,8.f) :
                    glm::ortho(-.5f,.5f,-.5f,.5f,.1f,8.f);
                gi.SetCamera(projection*glm::lookAt(eye,center,up),eye);
                device.Readback(gi.GetSurfaces(),surfaces.data(),sizeof(surfaces));
                unsigned missed = 0;
                for (const GISurface& s : surfaces)
                    if (s.Position.w == 0 || std::abs(s.Position[axis]-center[axis]) > .0001f) ++missed;
                report << axis << ',' << side << ',' << (perspective ? "perspective" : "orthographic")
                       << ',' << surfaces.size() << ',' << missed << '\n';
                if (missed) throw std::runtime_error("Missed a translated wall from axis " + std::to_string(axis));
            }
        }
    }
}

static void CheckOpeningAndShadow(Redflag::VulkanComputeDevice& device)
{
    using namespace Redflag;
    GIModel left, right;
    left.Path = right.Path = "tests/fixtures/SharedEdges.obj";
    left.Transform = glm::translate(glm::mat4(1), glm::vec3(-1.001f,0,0));
    right.Transform = glm::translate(glm::mat4(1), glm::vec3(1.001f,0,0));
    GlobalIllumination opening(device,std::vector<GIModel>{left,right},17,1);
    const glm::vec3 eye(0,0,3);
    const glm::mat4 view = glm::lookAt(eye,glm::vec3(0),glm::vec3(0,1,0));
    opening.SetCamera(glm::ortho(-.5f,.5f,-.25f,.25f,.1f,8.f)*view,eye);
    std::array<GISurface,17> row;
    device.Readback(opening.GetSurfaces(),row.data(),sizeof(row));
    for (size_t i=0; i<row.size(); ++i)
        if ((row[i].Position.w == 0) != (i == 8)) throw std::runtime_error("Real gap between the two walls changed");

    // 镜头直看白墙中心，点光源从右上方照过来。小挡板在灯前才应该遮光。
    GIModel receiver;
    receiver.Path = left.Path;
    auto readLight = [&](float blockerDistance, bool glass) {
        GIModel blocker;
        blocker.Path = receiver.Path;
        blocker.Transform = glm::scale(glm::translate(glm::mat4(1),glm::vec3(blockerDistance,0,blockerDistance)),
                                       glm::vec3(.25f));
        if (glass) blocker.TransparentParts = {1};
        GlobalIllumination gi(device,std::vector<GIModel>{receiver,blocker},1,1);
        gi.SetCamera(glm::ortho(-.1f,.1f,-.1f,.1f,.1f,8.f)*view,eye);
        gi.SetLights({{{2,0,2,1},{10,10,10,0}}});
        gi.Render(GIMethod::DirectReference);
        glm::vec4 color;
        device.Readback(gi.GetOutput(),&color,sizeof(color));
        return color.r;
    };
    const float blocked = readLight(1,false);
    const float behind = readLight(3,false);
    const float throughGlass = readLight(1,true);
    if (blocked != 0.f || !(behind > .01f) || std::abs(throughGlass-behind) > .00001f)
        throw std::runtime_error("An opaque panel should cast a shadow only between the wall and the light");
    std::cout << "Real gap: center ray passed, both sides hit. Shadow: " << blocked << " in front, "
              << behind << " behind the light, " << throughGlass << " with thin glass.\n";
}

static void CheckLongTriangle(Redflag::VulkanComputeDevice& device)
{
    using namespace Redflag;
    // 在 float 中从 -8192 加回边长，会把公共边的 x 从 0.00025 算成 0。放大这段位置逐像素检查。
    GIModel wall;
    wall.Path = "tests/fixtures/LongSharedEdge.obj";
    GlobalIllumination gi(device, std::vector<GIModel>{wall}, 17, 3);
    const glm::vec3 eye(.00025f,0,1);
    const glm::mat4 view = glm::lookAt(eye, glm::vec3(eye.x,0,0), glm::vec3(0,1,0));
    gi.SetCamera(glm::ortho(-.0005f,.0005f,-.1f,.1f,.1f,3.f)*view, eye);
    std::array<GISurface, 17*3> pixels;
    device.Readback(gi.GetSurfaces(), pixels.data(), sizeof(pixels));
    for (const GISurface& surface : pixels)
        if (surface.Position.w == 0 || std::abs(surface.Position.z) > .0001f)
            throw std::runtime_error("A ray fell between triangles of very different sizes");
    std::cout << "Long triangle: all " << pixels.size() << " rays hit beside the short neighbor.\n";
}
