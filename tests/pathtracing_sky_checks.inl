    // PT-112, the sky (docs/agents/sky-2.md), alone with TGPT_TEST_SKY_ONLY=1 (included right after the warm-up): the tracer's own
    // sky (pt_sky.hlsl, pt_sky.hlsli) for the game's atmosphere as the open world's volume sets HDRP's Physically Based Sky
    // (EarthAdvanced, aerosols 0.5 at 1200 m, anisotropy 0.47, ground 0.074, Alpha Multiplier 0), its Sun and Moon (the moon
    // as bright, opposite the sun, as the game's lights are) and a cloud layer of two layers over a cloud map made here, under a
    // sun 30 and 3 degrees up: the camera's misses drawn per pixel, and the map the rays read seen the same way
    // (TGPT_SKY_SHOW_MAP), written as PNGs (sky-*.png); their colour and brightness checked against what the model must give.
    if(GetEnvironmentVariableA("TGPT_TEST_SKY_ONLY", nullptr, 0)>0) {
        auto SetSkyModel=reinterpret_cast<decltype(&TGPT_SetSkyModel)>(GetProcAddress(pt, "TGPT_SetSkyModel"));
        expect(SetSkyModel!=nullptr, "TGPT_SetSkyModel exported");
        // A cloud map, 1024 x 512 (the upper hemisphere), RGBA8: soft heaps in red, thin streaks in alpha, of value noise.
        int32_t cloudMap=-1; ComPtr<ID3D11Texture2D> cloudTexture;
        {
            const UINT cw=1024, ch=512;
            auto hash=[](int x, int y) { uint32_t h=uint32_t(x)*374761393u+uint32_t(y)*668265263u; h=(h^(h>>13))*1274126177u; return float((h^(h>>16))&0xFFFFu)/65535.f; };
            auto noise=[&](float x, float y, int period) {
                const int x0=int(std::floor(x)), y0=int(std::floor(y)); const float fx=x-float(x0), fy=y-float(y0);
                auto at=[&](int i, int j) { return hash(((i%period)+period)%period, j); };
                const float sx=fx*fx*(3-2*fx), sy=fy*fy*(3-2*fy);
                return (at(x0, y0)*(1-sx)+at(x0+1, y0)*sx)*(1-sy)+(at(x0, y0+1)*(1-sx)+at(x0+1, y0+1)*sx)*sy;
            };
            std::vector<uint32_t> texels(size_t(cw)*ch);
            for(UINT y=0;y<ch;++y) for(UINT x=0;x<cw;++x) {
                float n=0, a=1, f=1.f/32.f; for(int o=0;o<4;++o) { n+=a*noise(x*f, y*f, int(cw*f)); a*=0.5f; f*=2.f; }
                n/=1.875f;
                const float heap=std::clamp((n-0.52f)*4.f, 0.f, 1.f), streak=std::clamp((noise(x/96.f, y/12.f, int(cw/96))-0.4f)*2.f, 0.f, 1.f);
                texels[size_t(y)*cw+x]=uint32_t(std::lround(heap*255.f))|(uint32_t(std::lround(streak*255.f))<<24);
            }
            D3D11_TEXTURE2D_DESC td{cw, ch, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1,0}, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0};
            D3D11_SUBRESOURCE_DATA init{texels.data(), cw*4, 0};
            device->CreateTexture2D(&td, &init, &cloudTexture);
            TGPT_SharedTexture shared{sizeof(TGPT_SharedTexture), TGPT_ABI};
            if(cloudTexture && RegisterSharedTexture && RegisterSharedTexture(uint64_t(uintptr_t(cloudTexture.Get())), &shared)>=0) cloudMap=shared.handle;
            expect(cloudMap>=0, "the cloud map is opened by the tracer (shared, no copy)");
        }
        const float sunLux=100000.f, pi=3.14159265f;
        auto body=[&](V3 toward, float diameterDegrees, bool reflects) {
            TGPT_SkyBody b{};
            b.direction[0]=toward.x; b.direction[1]=toward.y; b.direction[2]=toward.z;
            b.angularRadius=diameterDegrees*0.5f*pi/180.f;
            for(int i=0;i<3;++i) b.color[i]=sunLux;
            b.flags=TGPT_SKY_BODY_LIGHTS|(reflects ? TGPT_SKY_BODY_REFLECTS : 0u);
            const float perSolidAngle=1.f/(2.f*pi*(1.f-std::cos(b.angularRadius)));
            for(int i=0;i<3;++i) { b.surface[i]=reflects ? sunLux : sunLux*perSolidAngle; b.flare[i]=b.surface[i]; }
            b.flareSize=2.f*pi/180.f; b.flareFalloff=4.f; b.earthshine=0.01f; b.surfaceTexture=-1; b.distance=1.5e11f;
            b.right[0]=1; b.up[1]=1;
            return b;
        };
        auto skyFor=[&](V3 sun, bool clouds, bool showMap) {
            TGPT_Sky s{sizeof(TGPT_Sky), TGPT_ABI};
            s.mode=TGPT_SKY_OWN; s.flags=(clouds ? TGPT_SKY_CLOUDS|TGPT_SKY_CLOUDS_UPPER : 0u)|(showMap ? TGPT_SKY_SHOW_MAP : 0u);
            s.planetCenter[1]=-6378100.f; s.planetRadius=6378100.f;
            const float air[3]={5.8e-6f, 13.5e-6f, 33.1e-6f};
            for(int i=0;i<3;++i) { s.airExtinction[i]=air[i]; s.airScattering[i]=air[i]; }
            s.airScaleHeight=8000.f; s.atmosphereDepth=8000.f/0.144765f;
            const float aerosol=-std::log(1.f-0.5f)/1200.f;
            for(int i=0;i<3;++i) s.aerosolScattering[i]=aerosol;
            s.aerosolExtinction=aerosol; s.aerosolScaleHeight=1200.f; s.aerosolAnisotropy=0.47f;
            s.ozoneExtinction[0]=0.65e-6f; s.ozoneExtinction[1]=1.88e-6f; s.ozoneExtinction[2]=0.08e-6f; s.ozoneStart=20000.f; s.ozoneWidth=20000.f;
            for(int i=0;i<3;++i) { s.groundAlbedo[i]=0.074177f; s.horizonTint[i]=1.f; s.zenithTint[i]=1.f; }
            s.colorSaturation=1.f; s.alphaSaturation=1.f; s.alphaMultiplier=0.f; s.horizonZenithShift=0.f; s.intensity=1.f;
            s.bodyCount=2; s.bodies[0]=body(sun, 0.5f, false); s.bodies[1]=body(sun*-1.f, 10.f, true);
            for(int i=0;i<3;++i) s.bodies[1].sunTravel[i]=-(&sun.x)[i];
            s.hdrpSun[0]=sun.x; s.hdrpSun[1]=sun.y; s.hdrpSun[2]=sun.z; s.cloudOpacity=1.f;
            const float exposure=1.f/(1.2f*std::pow(2.f, 0.669203f)); // the game's layers' exposure -0.67 EV
            const float weights[2][4]={{0.330797f, 0.0661595f, 0.0661595f, 0.133841f}, {0.0669203f, 0.f, 0.f, 0.0669203f}};
            for(int l=0;l<2;++l) {
                TGPT_SkyLayer& c=s.layers[l];
                for(int i=0;i<4;++i) c.opacities[i]=weights[l][i];
                c.rotation=0; c.steps=6; c.sigma=0.1f; c.altitude=2000.f;
                for(int i=0;i<3;++i) c.light[i]=exposure*sunLux;
                c.map=cloudMap; c.scroll[0]=0.f; c.scroll[1]=-1.f; c.scrolled=float(l)*700.f; c.distortion=1; c.ambientDimmer=1.f; c.flowmap=-1; c.on=1;
            }
            s.cloudsHash=uint32_t(std::lround(std::atan2(sun.y, sun.z)*180.f/pi)); s.cloudWidth=1024; s.cloudHeight=512;
            return s;
        };
        // The camera on the ground looking towards the sun and 35 degrees up (0 to 70 degrees up in the picture: every reference
        // direction below is in it): the sky over most of the picture. 2 m up as the reference, and 400 m in front of the warm-up's
        // room (sky-3: from 9 m the room filled the picture), which then stands under 1 degree above the horizon.
        const Camera skyCam=make_camera({0, 2, -400}, {0, 2+std::tan(35.f*pi/180.f)*11.f, -389}, 70.f, float(W)/float(H));
        auto direction=[&](UINT x, UINT y) {
            const float nx=(float(x)+0.5f)/float(W)*2.f-1.f, ny=1.f-(float(y)+0.5f)/float(H)*2.f;
            float v[4]; const float c[4]={nx, ny, 1e-7f, 1.f};
            for(int i=0;i<4;++i) { v[i]=0; for(int j=0;j<4;++j) v[i]+=skyCam.invViewProj.m[i][j]*c[j]; }
            return normalize(V3{v[0]/v[3], v[1]/v[3], v[2]/v[3]});
        };
        // The CPU reference (docs/agents/sky-2.md, sky-3's section: sky_reference_fine.py): sun elevation, view elevation, view
        // azimuth from the sun's (degrees), radiance (HDRP's units): a 2 m high camera, the view ray in 80 steps, the multiple
        // scattering's Psi worked out at each step's own height and sun cosine (64 directions, 24 steps), no tables. (sky-2's
        // first reference binned Psi by 2 km and the sun's cosine by 1/16, 13 % off 11 degrees up beside the 3-degree sun.)
        static const float skyReference[][6]={
            {30.f, 45.0f, 0.0f, 15917.5f, 14623.6f, 13097.6f},
            {30.f, 30.0f, 40.0f, 11112.7f, 10396.0f, 9663.0f},
            {30.f, 20.0f, -40.0f, 11077.9f, 10249.8f, 9228.1f},
            {30.f, 60.0f, 30.0f, 8353.6f, 7990.3f, 7883.4f},
            {30.f, 38.0f, 0.0f, 18912.6f, 17218.1f, 15056.5f},
            {3.f, 45.0f, 0.0f, 782.3f, 640.0f, 684.0f},
            {3.f, 30.0f, 40.0f, 736.5f, 607.5f, 635.3f},
            {3.f, 20.0f, -40.0f, 710.5f, 562.2f, 540.3f},
            {3.f, 60.0f, 30.0f, 537.7f, 488.5f, 593.6f},
            {3.f, 11.0f, 0.0f, 695.2f, 470.4f, 360.0f},
        };
        ownRays=true;
        uint32_t frame=0;
        auto shot=[&](const TGPT_Sky& s, float ev, const char* name) {
            const float e=1.f/(1.2f*std::pow(2.f, ev));
            setExposure(e);
            expect(SetSkyModel && SetSkyModel(&s)==0, "the sky handed to the tracer");
            for(int i=0;i<3;++i) run(TGPT_VIEW_NONE, skyCam, skyCam, frame++, TGPT_OPTION_NO_DENOISER);
            auto px=readback();
            save(px, name, true);
            for(float& v:px) v/=e; // back to HDRP's units
            return px;
        };
        struct Stats { double r=0, g=0, b=0; size_t n=0; bool finite=true; };
        auto stats=[&](const std::vector<float>& px, V3 sun, float minAngle, float maxAngle, float minElevation, float maxElevation) {
            Stats st;
            for(UINT y=0;y<H;++y) for(UINT x=0;x<W;++x) {
                const V3 d=direction(x, y);
                const float elevation=std::asin(std::clamp(d.y, -1.f, 1.f))*180.f/pi, angle=std::acos(std::clamp(dot(d, sun), -1.f, 1.f))*180.f/pi;
                if(elevation<minElevation || elevation>maxElevation || angle<minAngle || angle>maxAngle) continue;
                const float* p=&px[(size_t(y)*W+x)*3];
                if(!std::isfinite(p[0]) || !std::isfinite(p[1]) || !std::isfinite(p[2]) || p[0]<0 || p[1]<0 || p[2]<0) st.finite=false;
                st.r+=p[0]; st.g+=p[1]; st.b+=p[2]; ++st.n;
            }
            if(st.n) { st.r/=st.n; st.g/=st.n; st.b/=st.n; }
            return st;
        };
        for(const float elevationDeg:{30.f, 3.f}) {
            const float el=elevationDeg*pi/180.f;
            const V3 sun=normalize(V3{0.f, std::sin(el), std::cos(el)});
            const float ev=elevationDeg>10.f ? 14.5f : 12.f;
            char name[64];
            std::snprintf(name, sizeof name, "sky-clear-%02.0f.png", elevationDeg); auto clear=shot(skyFor(sun, false, false), ev, name);
            std::snprintf(name, sizeof name, "sky-clear-map-%02.0f.png", elevationDeg); auto clearMap=shot(skyFor(sun, false, true), ev, name);
            std::snprintf(name, sizeof name, "sky-clouds-%02.0f.png", elevationDeg); auto cloudy=shot(skyFor(sun, true, false), ev, name);
            std::snprintf(name, sizeof name, "sky-clouds-map-%02.0f.png", elevationDeg); auto cloudyMap=shot(skyFor(sun, true, true), ev, name);
            const Stats away=stats(clear, sun, 40.f, 180.f, 20.f, 90.f), high=stats(clear, sun, 0.f, 180.f, 45.f, 90.f);
            const Stats nearSun=stats(clear, sun, 4.f, 15.f, 1.f, 90.f), all=stats(clear, sun, 0.f, 180.f, 20.f, 90.f);
            const double lAway=0.2126*away.r+0.7152*away.g+0.0722*away.b;
            std::printf("      sky, sun %.0f deg up: away from the sun (%zu px) %.0f %.0f %.0f (luminance %.0f), high %.0f %.0f %.0f, 4-15 deg from it %.0f %.0f %.0f\n",
                        elevationDeg, away.n, away.r, away.g, away.b, lAway, high.r, high.g, high.b, nearSun.r, nearSun.g, nearSun.b);
            expect(all.finite && all.n>size_t(W)*H/4, "the drawn sky is finite and not negative, over most of the picture");
            // At the directions the CPU reference (an independent integration of the same model, no tables: docs/agents/sky-2.md)
            // was taken at: the radiance within 12 % of it in each channel.
            for(const auto& r:skyReference) {
                if(std::fabs(r[0]-elevationDeg)>0.5f) continue;
                const float e=r[1]*pi/180.f, a=r[2]*pi/180.f;
                const V3 want{std::cos(e)*std::sin(a), std::sin(e), std::cos(e)*std::cos(a)};
                UINT bx=0, by=0; float best=-2.f;
                for(UINT y=0;y<H;++y) for(UINT x=0;x<W;++x) { const float c=dot(direction(x, y), want); if(c>best) { best=c; bx=x; by=y; } }
                double got[3]{}; int n=0;
                for(int dy=-2;dy<=2;++dy) for(int dx=-2;dx<=2;++dx) {
                    const int x=int(bx)+dx, y=int(by)+dy; if(x<0 || y<0 || x>=int(W) || y>=int(H)) continue;
                    for(int k=0;k<3;++k) got[k]+=clear[(size_t(y)*W+x)*3+k]; ++n;
                }
                for(double& v:got) v/=std::max(n, 1);
                double worst=0; for(int k=0;k<3;++k) worst=std::max(worst, std::fabs(got[k]-r[3+k])/std::max(double(r[3+k]), 1.0));
                std::printf("      at %.0f up, %.0f round: %.0f %.0f %.0f, the reference %.0f %.0f %.0f (%.1f %% apart at most)\n",
                            r[1], r[2], got[0], got[1], got[2], r[3], r[4], r[5], 100.0*worst);
                expect(best>0.9995f && worst<0.12, "the sky drawn is the model's (the CPU reference, within 12 %)");
            }
            if(elevationDeg>10.f) expect(lAway>500.0 && lAway<30000.0, "a clear sky's brightness under a 100 klux sun 30 degrees up (0.5 to 30 kcd/m2)");
            else expect(nearSun.r>nearSun.b, "beside the setting sun the sky is red and orange (the long way through the air)");
            // The map the rays read against the sky drawn per pixel: the same model (away from the discs and their flares).
            double diff=0, sum=0;
            for(UINT y=0;y<H;++y) for(UINT x=0;x<W;++x) {
                const V3 d=direction(x, y);
                if(d.y<std::sin(20.f*pi/180.f) || std::acos(std::clamp(dot(d, sun), -1.f, 1.f))<6.f*pi/180.f || std::acos(std::clamp(-dot(d, sun), -1.f, 1.f))<12.f*pi/180.f) continue;
                const size_t i=(size_t(y)*W+x)*3;
                const double a=0.2126*clear[i]+0.7152*clear[i+1]+0.0722*clear[i+2], b=0.2126*clearMap[i]+0.7152*clearMap[i+1]+0.0722*clearMap[i+2];
                diff+=std::fabs(a-b); sum+=a;
            }
            std::printf("      the map against the sky drawn per pixel: %.2f %% apart\n", sum>0 ? 100.0*diff/sum : 0.0);
            expect(sum>0 && diff/sum<0.05, "the map the rays read is the sky the camera sees (under 5 % apart)");
            // The sun's disc where the camera sees it.
            double brightest=0;
            for(UINT y=0;y<H;++y) for(UINT x=0;x<W;++x) if(std::acos(std::clamp(dot(direction(x, y), sun), -1.f, 1.f))<0.25f*pi/180.f) {
                const size_t i=(size_t(y)*W+x)*3; brightest=std::max(brightest, 0.2126*clear[i]+0.7152*clear[i+1]+0.0722*clear[i+2]); }
            std::printf("      the sun's disc: %.3g cd/m2\n", brightest);
            if(elevationDeg>10.f) expect(brightest>1e8, "the sun's disc is drawn at its radiance (its light over its solid angle)");
            // The clouds: drawn over part of the sky, with their light.
            size_t changed=0, counted=0;
            for(UINT y=0;y<H;++y) for(UINT x=0;x<W;++x) {
                if(direction(x, y).y<std::sin(20.f*pi/180.f)) continue;
                const size_t i=(size_t(y)*W+x)*3; ++counted;
                if(!std::isfinite(cloudy[i]) || !std::isfinite(cloudy[i+2])) { changed=0; break; }
                changed+=std::fabs(cloudy[i]-clear[i])>0.05f*clear[i]+1.f;
            }
            std::printf("      clouds over %.1f %% of the sky\n", counted ? 100.0*changed/counted : 0.0);
            expect(cloudMap<0 || (changed>counted/20 && changed<counted), "the clouds cover part of the sky");
            // (sky-3) The clouds in the map the rays read (reflections, bounced light) as the camera sees them: the same light, the
            // map's texels a little softer at the clouds' edges (a texel is 0.35 degrees, a pixel here 0.1).
            {
                double cdiff=0, csum=0;
                for(UINT y=0;y<H;++y) for(UINT x=0;x<W;++x) {
                    const V3 d=direction(x, y);
                    if(d.y<std::sin(20.f*pi/180.f) || std::acos(std::clamp(dot(d, sun), -1.f, 1.f))<6.f*pi/180.f || std::acos(std::clamp(-dot(d, sun), -1.f, 1.f))<12.f*pi/180.f) continue;
                    const size_t i=(size_t(y)*W+x)*3;
                    const double a=0.2126*cloudy[i]+0.7152*cloudy[i+1]+0.0722*cloudy[i+2], b=0.2126*cloudyMap[i]+0.7152*cloudyMap[i+1]+0.0722*cloudyMap[i+2];
                    cdiff+=std::fabs(a-b); csum+=a;
                }
                std::printf("      the clouds in the map against the clouds drawn per pixel: %.2f %% apart\n", csum>0 ? 100.0*cdiff/csum : 0.0);
                expect(cloudMap<0 || (csum>0 && cdiff/csum<0.10), "the map the rays read holds the clouds the camera sees (under 10 % apart)");
            }
        }
        ownRays=false;
        std::printf(failures ? "FAILED %d checks\n" : "sky checks passed\n", failures); return failures ? 1 : 0;
    }
