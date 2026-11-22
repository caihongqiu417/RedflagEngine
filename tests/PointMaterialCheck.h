#pragma once

// CPU 用双精度逐灯积分，与 GPU 的灯簇和蓄水池分别核对。glTF 2.0，附录 B.3。
static glm::dvec3 ReferencePoint(const GISurface& s, const GILight& lamp, glm::dvec3 V)
{
    const glm::dvec3 n(s.Normal), base(s.Albedo);
    const glm::dvec3 delta = glm::dvec3(lamp.Position) - glm::dvec3(s.Position);
    const double r = glm::length(delta), radius = lamp.Intensity.w;
    const glm::dvec3 L = delta/r, H = glm::normalize(L+V);
    const double nl = glm::dot(n,L), nv = glm::dot(n,V);
    if (nl <= 0 || nv <= 0 || (radius > 0 && r >= radius)) return {};
    const double nh = std::clamp(glm::dot(n,H), 0., 1.), vh = std::clamp(glm::dot(V,H), 0., 1.);
    const double a = std::max(.002, double(s.Normal.w)*s.Normal.w), a2 = a*a;
    const double d = a2 / (glm::pi<double>() * std::pow(1.-nh*nh + nh*nh*a2, 2));
    const auto smith = [&](double c) { return std::sqrt(1.+a2*(1./(c*c)-1.)); };
    const double spec = d / (2.*nl*nv*(smith(nv)+smith(nl)));
    const double f = std::pow(1.-vh,5.), fd = .04 + .96*f;
    const glm::dvec3 metalF = base+(1.-base)*f;
    const glm::dvec3 dielectric = base*((1.-fd)/glm::pi<double>())+fd*spec;
    const glm::dvec3 brdf = glm::mix(dielectric, metalF*spec, double(s.Albedo.w));
    const double fade = radius > 0 ? std::max(0.,1.-r*r/(radius*radius)) : 1.;
    return brdf * glm::dvec3(lamp.Intensity) * (nl*fade*fade/(r*r));
}

static void CheckPointMaterials(VulkanComputeDevice& device, const fs::path& dir, RenderDocCapture& capture)
{
    fs::create_directories(dir);
    std::ofstream file(dir/"point-materials.csv");
    file << "material,center_rgb_r,edge_rgb_r,max_cpu_error,max_cluster_error,restir_1_frame_mse,restir_64_frame_mean_mse\n";
    const int size=33, count=size*size, center=count/2;
    const glm::mat4 view(1.f), projection=glm::ortho(-2.f,2.f,-2.f,2.f,.1f,10.f);
    GIModel geometry{};
    geometry.Transform = glm::translate(glm::mat4(1), glm::vec3(0,0,-5));
    GlobalIllumination gi(device,std::vector<GIModel>{geometry},size,size);
    gi.SetRasterCamera(projection,glm::vec3(0),true);
    const std::vector<GILight> lamps={{{0,0,-1,1},{5,4,3,6}},{{1.5,.2,-2,1},{2,4,6,4}},{{-.9,-.8,-1.5,1},{4,1,2,3}}};
    gi.SetLights(lamps);
    ScreenEffects effects(device,size,size);
    std::vector<GISurface> wall(count);
    std::vector<glm::vec4> direct(count), clustered(count), sample(count);
    std::vector<glm::dvec3> exact(count);
    auto mse = [&](const auto& image) {
        double error=0;
        for(int i=0;i<count;++i) {
            glm::dvec3 diff=glm::dvec3(image[i])-exact[i];
            error+=glm::dot(diff,diff);
        }
        return error/(3*count);
    };
    float centerLight[3]{};
    for(int material=0;material<3;++material)
    {
        const float roughness=material==1 ? .7f : .25f, metallic=material ? 1.f : 0.f;
        for(int y=0;y<size;++y) for(int x=0;x<size;++x) {
            int i=y*size+x;
            wall[i]={{(x+.5f)/size*4.f-2.f,2.f-(y+.5f)/size*4.f,-3,1},{0,0,1,roughness},{.8f,.35f,.12f,metallic}};
            exact[i]={}; for(const auto& l:lamps) exact[i]+=ReferencePoint(wall[i],l,{0,0,1});
        }
        device.Upload(gi.GetSurfaces(),wall.data(),wall.size()*sizeof(wall[0]));
        if (material==2) capture.Begin();
        gi.RenderDirect(false);
        device.Readback(gi.GetDirectOutput(),direct.data(),direct.size()*sizeof(direct[0]));
        const char* name=material==0?"dielectric":material==1?"rough-metal":"smooth-metal";
        std::ofstream(dir/(std::string(name)+".rgba32f"),std::ios::binary).write(
            reinterpret_cast<const char*>(direct.data()),direct.size()*sizeof(direct[0]));
        auto result=effects.Render(ScreenEffect::ClusteredLighting,gi,projection,view,0);
        device.Readback(result,clustered.data(),clustered.size()*sizeof(clustered[0]));
        if (material==2) capture.End();
        if (material==2) {
            std::ofstream times(dir/"point-times.csv");
            times<<"sample,all_lights_us,cluster_us\n";
            auto elapsed=[](auto submit) {
                auto start=std::chrono::steady_clock::now();
                submit();
                return std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count();
            };
            for(int i=-4;i<21;++i) {
                auto all=[&] { gi.RenderDirect(false); };
                auto cull=[&] { effects.Render(ScreenEffect::ClusteredLighting,gi,projection,view,0); };
                double a,b;
                if(i%2==0) { a=elapsed(all); b=elapsed(cull); }
                else { b=elapsed(cull); a=elapsed(all); }
                if(i>=0) times<<i+1<<','<<a<<','<<b<<'\n';
            }
        }
        double cpu=0,cluster=0,relative=0;
        for(int i=0;i<count;++i) for(int ch=0;ch<3;++ch) {
            cpu=std::max(cpu,std::abs(direct[i][ch]-exact[i][ch]));
            cluster=std::max(cluster,std::abs(double(direct[i][ch])-clustered[i][ch]));
            relative=std::max(relative,std::abs(direct[i][ch]-exact[i][ch])/std::max(1.,exact[i][ch]));
        }
        if(relative>.00015 || cluster>2e-3) throw std::runtime_error("Point light disagrees with CPU GGX or clustered lighting: "+std::to_string(cpu)+" / "+std::to_string(cluster));
        gi.ResetHistory();
        std::vector<glm::dvec3> mean(count);
        double first=0;
        for(int frame=0;frame<64;++frame) {
            gi.RenderDirect(true);
            device.Readback(gi.GetDirectOutput(),sample.data(),sample.size()*sizeof(sample[0]));
            if(!frame) first=mse(sample);
            for(int i=0;i<count;++i) mean[i]+=glm::dvec3(sample[i])/64.;
        }
        double final=mse(mean);
        if (!(final < first)) throw std::runtime_error("Accumulating ReSTIR DI did not reduce error");
        centerLight[material]=direct[center].r;
        file << name << ',' << centerLight[material] << ',' << direct[center+10].r << ',' << cpu << ',' << cluster << ',' << first << ',' << final << '\n';
    }
    if(!(centerLight[2]>centerLight[1] && centerLight[2]>centerLight[0])) throw std::runtime_error("Smooth metal lost its highlight");
    // 同一盏灯照同一面墙，把立方体挡在中间，逐灯与分簇都应读到零。
    geometry.Transform=glm::translate(glm::mat4(1),glm::vec3(0,0,-2));
    GlobalIllumination blocked(device,std::vector<GIModel>{geometry},size,size);
    blocked.SetRasterCamera(projection,glm::vec3(0),true); blocked.SetLights({lamps.front()});
    device.Upload(blocked.GetSurfaces(),wall.data(),wall.size()*sizeof(wall[0]));
    blocked.RenderDirect(false);
    device.Readback(blocked.GetDirectOutput(),direct.data(),direct.size()*sizeof(direct[0]));
    device.Readback(effects.Render(ScreenEffect::ClusteredLighting,blocked,projection,view,0),clustered.data(),clustered.size()*sizeof(clustered[0]));
    if(glm::vec3(direct[center])!=glm::vec3(0) || glm::vec3(clustered[center])!=glm::vec3(0))
        throw std::runtime_error("Light passed through the opaque blocker");
}

static void CheckMetalBounce(VulkanComputeDevice& device,const fs::path& dir)
{
    const int size=17;
    auto wall=[](glm::vec3 pos,glm::vec3 scale,glm::vec3 color,float metal){
        GIModel m{{},glm::scale(glm::translate(glm::mat4(1),pos),scale),color};
        m.MaterialFactors={{.6f,metal}};
        return m;
    };
    std::ofstream file(dir/"metal-bounce.csv");
    file<<"receiver_metal,wall_metal,receiver_red\n";
    double red[4]{};
    for(int m=0;m<4;++m){
        auto floor=wall({0,-.05f,0},{4,.1f,4},{.7f,.15f,.04f},float(m&1));
        auto back=wall({0,1,-2.05f},{4,2,.1f},{.65f,.65f,.65f},float(m>>1));
        GlobalIllumination room(device,std::vector<GIModel>{floor,back},size,size);
        glm::vec3 eye(0,1,4);
        room.SetCamera(glm::perspective(glm::radians(44.f),1.f,.1f,15.f)*glm::lookAt(eye,glm::vec3(0,1,-2),glm::vec3(0,1,0)),eye);
        room.SetLights({{{0,1,.2f,1},{20,20,20,0}}});
        room.SetProbeFeedback(true);
        for(int frame=0;frame<4;++frame) room.Render(GIMethod::DDGI,64);
        std::vector<glm::vec4> pixels(size*size);
        device.Readback(room.GetOutput(),pixels.data(),pixels.size()*sizeof(pixels[0]));
        red[m]=pixels[pixels.size()/2].r;
        file<<(m>>1)<<','<<(m&1)<<','<<red[m]<<'\n';
    }
    if(!(red[0]>red[1] && red[1]>=0 && red[2]==0 && red[3]==0))
        throw std::runtime_error("DDGI diffuse reflection ignored a metal receiver or source");
}
