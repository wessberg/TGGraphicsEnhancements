// SPDX-License-Identifier: MIT
// The fog's own small check (PT-118; TGPT_TEST_FOG_FROXEL_ONLY=1): explicitly selected, never part of the whole suite.
// The camera's fog (its light in cells: pt_fog_froxel, pt_fog_integrate, pt_fog_tail) held against WHAT IS KNOWN EXACTLY
// where that exists, and elsewhere against the numbers the per-pixel route gave before it went (pt_fog.hlsl, until
// 2026-10-12; "pins": pathtracing_fog_froxel_pins.inl): the mean of 16 frames after the settling, no denoiser, region
// by region.
// Until 2026-10-11 it asked only for a finite picture, and the grid went white in the game while it passed; until
// 2026-10-12 it had no case past 80 m and let the grid read the mist's cache the old route had just fed, and the two
// routes were 7 % and 24 % apart in the game's daylight scenes while it passed (docs/agents/fogrule.md).
//   On the tracer's own camera rays, as the game runs (with HDRP's G-buffer as the first surface the composite lays the
// fog over the sky's pixels alone; TGPT_TEST_FOG_GBUFFER=1 for that). The lamps fall off with distance alone, as every
// light the plugin sends but an effect's (TGPT_LIGHT_NO_RANGE, their reach the plugin's: as far as 50 lux).
//   IN THE ROOM (every way ends within 80 m; pinned): no lamp near the camera; one 2 m, 0.3 m and 0.02 m in front of it
// (0.02: in the first slices, where a cell is millimetres deep); a candle-like emitting quad 3 x 5 cm; and two cases with
// no sun in the frame, as in the game's interiors (a lamp, the candle: PT-133).
//   OUTDOORS: the camera turned away from the room, over the mound and a ground 4 km wide into the open sky, the fog
// as the game sends it (Max Fog Distance 5 km, the far distance 1,000 km):
//   "open": a uniform fog, a uniform sky, an unshadowed sun, a black ground, no lamp. The light scattered once is known
//     exactly there, pixel by pixel (the fog's albedo times 1 - e^(-k s) times the sun's light by the phase function
//     plus the sky's over the upper half of the directions, s the way to the ground or the Max Fog Distance), and
//     behind the fog the sky, or the ground's own highlight of the sun (black by its base colour, the ground still
//     reflects 4 %: groundShine below), dimmed by e^(-k s).
//   "open, a lamp": the same fog and ground with no sun and no sky and one lamp, 6 m ahead (inside the first 20 m) and
//     60 m ahead (in the far cells). A lamp's light in a uniform fog along a straight way is one integral (its strength
//     over the squared distance, the fog on its way and on the way back, the phase function). On the ground the
//     lamp's highlight too: a surface lit through the fog and seen through it.
//   "open, no ground": the uniform fog without a ground and with the light it scatters more than once (the mist's
//     cache). In an endless uniform fog what the cache converges to is known too (each order the one before times the
//     albedo: C = a D / (1 - a), D the first order's mean over all directions). PRINTED, NOT JUDGED: the mist's
//     cache is fed at points inside the camera's view alone and read all around them, and a CPU model of that reads
//     a quarter and more short of the fixed point (an entry owed since 2026-10-12: docs/agents/fogrule.md).
//   "outdoors, the fog scene's fog" and "the vista's": the height fog of the game's two daylight scenes (the camera 27 m
//     under the fog's base, and 2 m above it; the vista's forward scattering), the check's own scene with a far ground,
//     once with the light scattered once (pinned to the per-pixel route's numbers) and once with the mist's cache
//     (pinned to this pass's own numbers: a change there is a change of the mist's light, to be explained).
//   "outdoors, the night's fog" (PT-37, docs/agents/fog-3.md): the medium of the game's night (a mean free path of
//     9 m under its base height, thinning above it with a scale height of 17 m, the camera 2.1 m over the base; dark,
//     albedo 0.24; forward scattering 0.48), over a ground that reflects nothing, once lit by a directional light
//     alone (the game's "moon": 40 degrees up, at a right angle to the view) and once by a uniform sky alone. The
//     light scattered once is known exactly there too, by quadrature along every pixel's way (the sun's light dimmed
//     by the fog above each point, as HDRP dims a directional light; the sky's from the upper half of the directions,
//     dimmed the same way). It is the first
//     case that holds the cells in a fog that changes with height. Beside it, PRINTED, NOT JUDGED: what HDRP's own
//     volumetric fog gives for the same medium, light and sky, by hand (its VolumetricLighting.compute: the
//     directional light times its volumetric dimmer, 1.23 for the game's moon, dimmed by the fog above; its ambient
//     probe, the sky from all around by the phase function, NOT dimmed and not shadowed), and the tracer's over it.
//   Every case starts from an EMPTY mist cache (one frame without the cache empties it).
//   A region's "grain" is printed with its mean: how far a pixel strays from its own mean from frame to frame.
//   For looking closer (one case then, of the switches given; its picture fog.png and the mean fog-mean.f32,
// 1920 x 1080 x 3 floats, bottom row first; such a case is printed, not judged, unless it is one of the open ones):
//   TGPT_TEST_FOG_CASE=<n>: the n-th case of the list alone (from 0; a case takes half a minute, the list six).
//   TGPT_TEST_FOG_LAMP=<metres>, TGPT_TEST_FOG_CANDLE=1: as the cases.
//   TGPT_TEST_FOG_CANDLE=<a number below 1>: the emitting quad that many times as bright (the tracer samples a glowing
//     surface as a light only as far as it still gives 2 lux: 16 m for the case's quad, 3 m at 0.04).
//   TGPT_TEST_FOG_RANGED=1: the lamps keep a range of 12 m and HDRP's window (an effect's light).
//   TGPT_TEST_FOG_EFFECT=<candela>: an effect's light, strong, with a range of 0.6 m, 5 m ahead.
//     (Lamps with a range and glowing surfaces: the per-pixel route lost their light on a long way, PT-132: it took its
//     lamps from the lists at one point of the way. The sky's strip, a ranged lamp 0.3 m ahead: 0.119 against the
//     cells' 0.251, and 0.251 range-free: docs/agents/fogfix.md.)
//   TGPT_TEST_FOG_LAMPS=<n>: n more lamps through the room (more than 8 in a cell of the light grid: the lists then
//     draw by their cumulative weights). TGPT_TEST_FOG_LAMP_POWER=<n>: the lamps n times as strong.
//   TGPT_TEST_FOG_SCALE=<n>: the sun n times brighter and the exposure n times smaller (the game's reference radiance is
//     about 30,000, this scene's 2: the same picture, other storage units). TGPT_TEST_FOG_ANISO=<g>: the fog's anisotropy.
//   TGPT_TEST_FOG_CACHE=1: the light the fog scatters more than once (the mist's cache, TGPT_FRAME_FOG_WALK).
//   TGPT_TEST_FOG_BOXES=1: seven fog boxes away from the camera's rays; with TGPT_TEST_FOG_INTERSECT=1 one crosses them.
//   TGPT_TEST_FOG_NOSUN=1: no sun in the frame (as the check's own "no sun" cases).
//   TGPT_TEST_FOG_PLACE=<1 to 6>: one of the outdoor cases (1 open, 2 open without a ground, 3 the fog scene's fog, 4 the
//     vista's, 5 open with a lamp TGPT_TEST_FOG_LAMP metres ahead, 6 the night's fog: its moon alone, with
//     TGPT_TEST_FOG_NOSUN=1 its sky alone), with TGPT_TEST_FOG_CACHE=1 the mist's cache.
//     TGPT_TEST_FOG_NOSKY=1: no sky light. TGPT_TEST_FOG_FRAMES=<n>: that many frames (the last 16 are averaged).
//   TGPT_TEST_FOG_SUN=<n>: the sun alone n times brighter, the exposure and the lamps as they are: a dark room in bright
//     daylight (the game's tavern: the sun's 94,000 lux outside its walls, candles inside).
//   TGPT_TEST_FOG_ABSORBER=1: the open cases' ground reflects nothing (no highlight of the sun or the lamp on it): the
//     listed cases as they are, with the fog's own light alone on the ground.
if(fogFroxelOnly) {
    fogExtinction=1.f/39.9f; fogBase=100.f; fogRcpScale=0.02f;
    maxFogDistance=80.f; fogColorSent[3]=80.f; bounces=1;
    auto asked=[](const char* name) { return GetEnvironmentVariableA(name,nullptr,0)>0; };
    auto number=[](const char* name, float otherwise) { char text[24]{}; return GetEnvironmentVariableA(name,text,sizeof text)>0 ? float(std::atof(text)) : otherwise; };
    ownRays=!asked("TGPT_TEST_FOG_GBUFFER");
    fogAnisotropy=number("TGPT_TEST_FOG_ANISO",0.f);
    const bool fogCache=asked("TGPT_TEST_FOG_CACHE"), ranged=asked("TGPT_TEST_FOG_RANGED");
    { const float n=number("TGPT_TEST_FOG_SCALE",0.f); if(n>0.f) { sunLux*=n; setExposure(0.25f/n); } }
    { const float n=number("TGPT_TEST_FOG_SUN",0.f); if(n>0.f) sunLux*=n; }
    const uint32_t baseOptions=TGPT_OPTION_NO_DENOISER|TGPT_OPTION_NO_CAUSTICS|TGPT_OPTION_NO_SPECULAR;
    // Seven volumes, matching the tavern's count, all outside these camera segments (one across them when asked).
    if(asked("TGPT_TEST_FOG_BOXES")) {
        TGPT_FogBox boxes[7]{};
        for (uint32_t i=0;i<7;++i) {
            auto& box=boxes[i]; box.center[0]=1000.f+float(i)*10.f; box.center[1]=1000.f; box.center[2]=1000.f;
            box.extinction=.2f; box.right[0]=box.up[1]=1.f; box.endTimesRcpDistFadeLen=1.f; box.mask=-1; box.maskMost=1.f;
            for(int a=0;a<3;++a) { box.extents[a]=1.f; box.albedo[a]=.8f; box.rcpPosFaceFade[a]=box.rcpNegFaceFade[a]=100.f; box.tiling[a]=1.f; }
        }
        if(asked("TGPT_TEST_FOG_INTERSECT")) {
            boxes[0].center[0]=0.f; boxes[0].center[1]=3.f; boxes[0].center[2]=-2.f;
            boxes[0].extents[0]=.8f; boxes[0].extents[1]=1.3f; boxes[0].extents[2]=2.f;
            boxes[0].albedo[0]=.2f; boxes[0].albedo[1]=.8f; boxes[0].albedo[2]=.4f;
        }
        expect(SetFogBoxes && SetFogBoxes(boxes,7)==0,"seven fog boxes uploaded (one intersects when requested)");
    }
    // A candle's flame as an emitting surface: the emissive panel's material, bright enough for its 15 cm2 that the
    // tracer samples it as a light through the whole room (about 500 cd: a reach of 16 m).
    const float flame=number("TGPT_TEST_FOG_CANDLE",1.f);
    materials.push_back(materials[4]);
    for(float& e:materials.back().emissive) e*=100000.f*(flame>0.f && flame<1.f ? flame : 1.f);
    const uint32_t flameMaterial=uint32_t(materials.size()-1);
    // The open cases' ground: black by its base colour, so that it sends no diffuse light. It is still a dielectric (a
    // reflectance of 4 %, the room's smoothness of 0.3): the sun and a lamp make a highlight on it, which the direct
    // light's pass shades and the fog dims on the way to the camera. That highlight is part of what is known exactly
    // (groundShine, below). Until 2026-10-12 it was left out, and the first run of this check read the ground x1.24
    // (the sun), x1.93 (a lamp 6 m ahead) and x3.43 (a lamp 60 m ahead, at a grazing angle) of the value without it,
    // the sky and the air right: docs/agents/fog-3.md has each term.
    // TGPT_TEST_FOG_ABSORBER=1: a ground that reflects nothing (a metal of base colour 0: no diffuse colour and a
    // reflectance of 0), the term switched off in the fixture: a ground pixel then shows the fog's own light alone.
    const bool absorber=asked("TGPT_TEST_FOG_ABSORBER");
    materials.push_back(materials[0]);
    for(int c=0;c<3;++c) materials.back().baseColor[c]=0.f;
    if(absorber) materials.back().metallic=1.f;
    const uint32_t blackMaterial=uint32_t(materials.size()-1);
    // The night case's ground: one that reflects nothing, whatever the switch says.
    materials.push_back(materials.back()); materials.back().metallic=1.f;
    const uint32_t absorbingMaterial=uint32_t(materials.size()-1);
    const double groundF0=absorber ? 0.0 : 0.04, groundRoughness=std::max(1.0-double(materials.back().smoothness), 0.045);
    expect(SetMaterials(0, materials.data(), uint32_t(materials.size()))==0, "the fog check's flame material set");
    const std::vector<TGPT_Instance> roomInstances=instances; const std::vector<uint32_t> roomIndices=indices;
    const V3 ahead=normalize(V3{0.f-cam.position.x, 2.2f-cam.position.y, 2.f-cam.position.z});
    // The open cases' sky: one radiance in every direction (the closed form's).
    const float flatSky[3]={0.5f, 0.6f, 0.8f};
    ComPtr<ID3D11Texture2D> uniformSky;
    {
        const int size=8; std::vector<uint16_t> face(size_t(size)*size*4);
        for(size_t i=0;i<size_t(size)*size;++i) { for(int k=0;k<3;++k) face[i*4+k]=float_to_half(flatSky[k]); face[i*4+3]=float_to_half(1); }
        D3D11_TEXTURE2D_DESC d{}; d.Width=d.Height=size; d.MipLevels=1; d.ArraySize=6; d.Format=DXGI_FORMAT_R16G16B16A16_FLOAT; d.SampleDesc.Count=1;
        d.BindFlags=D3D11_BIND_SHADER_RESOURCE; d.MiscFlags=D3D11_RESOURCE_MISC_TEXTURECUBE;
        D3D11_SUBRESOURCE_DATA init[6]; for(int f=0;f<6;++f) init[f]={face.data(), UINT(size*8), 0};
        device->CreateTexture2D(&d, init, &uniformSky);
    }
    // The outdoor cases' camera: from the room's front over the mound (3 to 15 m ahead) and the far ground into the sky.
    const V3 outPosition{0.f, 3.2f, -9.f}, outTarget{0.f, 2.2f, -40.f};
    const Camera outside=make_camera(outPosition, outTarget, 60.f, float(W)/float(H));
    const V3 outForward=normalize(outTarget-outPosition), outRight=normalize(cross({0, 1, 0}, outForward)), outUp=cross(outForward, outRight);
    // noSun: no sun in the frame (TGPT_Frame.sunColor's w 0), as in the game's interiors, where the plugin finds none.
    // (2026-10-12: with it the cells' pass of the builds of 2026-10-11 gave cells of 1e30 storage units and more, the
    // picture white: the tavern. The sun's code skipped, the value "the light so far" came out of the lamps' loop as
    // garbage in one or two of its three channels. docs/agents/fogfix.md, PT-133.)
    // place: 0 the room; 1 open (the closed form's); 2 open without a ground (the mist's fixed point); 3 outdoors with
    // the fog scene's fog; 4 with the vista's; 5 open with one lamp `lamp` metres ahead and no other light.
    // cache: the light the fog scatters more than once. pin: the regions' means that hold the case where nothing is
    // known exactly (all 0: printed, not judged); pathtracing_fog_froxel_pins.inl says where each row comes from.
    // place 6: the night's fog, lit by its moon alone, or (noSun) by its sky alone.
    struct FogCase { const char* name; float lamp; bool candle; bool noSun; int place; bool cache; double pin[5]; };
    std::vector<FogCase> cases{
#include "pathtracing_fog_froxel_pins.inl"
    };
    const bool single=asked("TGPT_TEST_FOG_LAMP") || asked("TGPT_TEST_FOG_CANDLE") || ranged || asked("TGPT_TEST_FOG_EFFECT") ||
                      asked("TGPT_TEST_FOG_LAMPS") || asked("TGPT_TEST_FOG_LAMP_POWER") || asked("TGPT_TEST_FOG_ANISO") || fogCache || asked("TGPT_TEST_FOG_SCALE") ||
                      asked("TGPT_TEST_FOG_BOXES") || asked("TGPT_TEST_FOG_GBUFFER") || asked("TGPT_TEST_FOG_SUN") || asked("TGPT_TEST_FOG_NOSUN") || asked("TGPT_TEST_FOG_PLACE");
    const bool oneCase=asked("TGPT_TEST_FOG_CASE");
    if(oneCase) { const size_t n=size_t(std::max(number("TGPT_TEST_FOG_CASE",0.f), 0.f)); if(n<cases.size()) cases={cases[n]}; }
    if(single) {
        const int place=std::clamp(int(number("TGPT_TEST_FOG_PLACE",0.f)), 0, 6);
        cases={{"the case asked for", number("TGPT_TEST_FOG_LAMP",place==5 ? 6.f : 0.f), asked("TGPT_TEST_FOG_CANDLE"), asked("TGPT_TEST_FOG_NOSUN") || place==5, place, fogCache, {0, 0, 0, 0, 0}}};
    }
    // The regions (rows from the bottom). The room's: the sky's strip, the back wall, the centre, round the candle, the
    // frame. Outdoors (each as wide left of the middle as right of it): the sky, the band above the horizon, the far
    // ground (23 to 60 m: past the first 20 m), the near ground (the mound, or the open cases' ground 6 to 9 m away), the
    // frame. With the open cases' lamp: the middle, round the lamp, in the sky's place.
    struct Region { const char* name; uint32_t x0, y0, x1, y1; };
    const Region roomRegions[5]={{"sky strip", 600, 808, 1320, 860}, {"back wall", 620, 560, 1300, 780}, {"centre", 860, 440, 1060, 640},
                                 {"candle", 743, 487, 943, 687}, {"frame", 0, 0, W, H}};
    const Region outRegions[5]={{"sky", 660, 700, 1260, 1000}, {"above the horizon", 660, 600, 1260, 680}, {"far ground", 660, 440, 1260, 520},
                                {"near ground", 660, 0, 1260, 200}, {"frame", 0, 0, W, H}};
    const Region lampRegions[5]={{"round the lamp", 860, 460, 1060, 660}, {"above the horizon", 660, 700, 1260, 1000}, {"far ground", 660, 440, 1260, 520},
                                 {"near ground", 660, 0, 1260, 200}, {"frame", 0, 0, W, H}};
    // How far a region's mean may be from what is known exactly, or from its pin: 3 % and 0.0005 of white (a mean of 16
    // noisy frames is within about 1 %; the cells' light near a lamp is spread over a slice's depth along the ray, which
    // the mean over a region does not see).
    const double tolerance=0.03, floorLevel=0.0005;
    // HDRP's phase function (pt_medium fog_phase), and its integral over the upper half of the directions for a way
    // whose direction has the height y (the sky's light on a point of the fog: a table over y, by quadrature).
    auto phase=[](double g, double c) { const double g2=g*g; return (3.0/(8.0*3.14159265358979))*(1.0-g2)/(2.0+g2)*(1.0+c*c)/std::pow(std::max(1.0+g2-2.0*g*c, 1e-6), 1.5); };
    auto upperHalf=[&](double g) {
        std::vector<double> table(257);
        for(int i=0;i<=256;++i) {
            const double y=-1.0+2.0*double(i)/256.0, side=std::sqrt(std::max(0.0, 1.0-y*y));
            double sum=0; const int nt=96, np=192;
            for(int a=0;a<nt;++a) { const double theta=(a+0.5)/nt*1.5707963267949, st=std::sin(theta), ct=std::cos(theta);
                for(int b2=0;b2<np;++b2) { const double phi=(b2+0.5)/np*6.28318530717959; sum+=phase(g, side*st*std::cos(phi)+y*ct)*st; } }
            table[size_t(i)]=sum*(1.5707963267949/nt)*(6.28318530717959/np);
        }
        return table;
    };
    // A lamp's light in a uniform fog (extinction k) along a way from `from` along d over `way` metres, scattered to the
    // way's start, for a lamp of strength 1 with nothing in its light's way (pt_common light_incident: its strength over
    // the squared distance, down to its own size, and the fog between it and the point; the phase function; the fog
    // back to the start). The integral is taken over the angle the way's points are seen under from the lamp (the
    // substitution equiangular sampling uses), where what is left of it is smooth (against a sum over the way in
    // half-millimetre steps: within a thousandth, docs/agents/fogrule.md).
    auto lampGlow=[&](V3 from, V3 d, double way, V3 lamp, double radius, double k, double g) {
        const V3 to=lamp-from;
        const double along=double(dot(to, d)), h2=std::max(double(dot(to, to))-along*along, 1e-10), h=std::sqrt(h2), own=radius*radius+1e-4;
        const double first=std::atan2(-along, h), last=std::atan2(way-along, h);
        const int n=96; double sum=0;
        for(int i=0;i<n;++i) {
            const double angle=first+(last-first)*(i+0.5)/n, offset=h*std::tan(angle), t=along+offset, d2=h2+offset*offset, dist=std::sqrt(d2);
            sum+=phase(g, -offset/dist)/std::max(d2, own)*std::exp(-k*(dist+t))*d2/h; // (the way's metres an angle spans: d2 / h)
        }
        return k*sum*(last-first)/n;
    };
    // The ground's highlight, per unit of the light arriving along l at a surface of normal n seen from v (the lobe
    // times n.l): a rough dielectric's as the tracer shades it (pt_common ggx_specular: Schlick's Fresnel, whose
    // grazing reflectance is min(50 F0, 1); GGX's D at alpha, the square of the roughness; Smith's G1 for either side;
    // over 4 n.v). Written out here by hand, not read from the tracer.
    auto groundShine=[&](V3 n, V3 v, V3 l) {
        const V3 h=normalize(v+l);
        const double nl=std::clamp(double(dot(n, l)), 0.0, 1.0), nv=std::clamp(double(dot(n, v)), 0.0, 1.0);
        const double nh=std::clamp(double(dot(n, h)), 0.0, 1.0), vh=std::clamp(double(dot(v, h)), 0.0, 1.0);
        if(nl<=0.0 || nv<=0.0 || groundF0<=0.0) return 0.0;
        const double alpha=groundRoughness*groundRoughness, a2=alpha*alpha;
        const double fresnel=groundF0+(std::min(50.0*groundF0, 1.0)-groundF0)*std::pow(1.0-vh, 5.0);
        const double lobe=nh*nh*(a2-1.0)+1.0, spread=a2/(3.14159265358979*lobe*lobe);
        auto shadowing=[&](double c) { return 2.0*c/(c+std::sqrt(a2+(1.0-a2)*c*c)); };
        return fresnel*spread*shadowing(nl)*shadowing(nv)/(4.0*nv);
    };
    // ---- The night's fog (place 6): a fog that thins with height, by hand.
    // The fog's density over its extinction k, at the height h over its base: 1 under the base, e^(-h / H) above it.
    // heightOver: that, summed over the height from the base to h (continuous at the base). With it the optical
    // depth of a straight way from the height h0, whose direction has the height dy, over its first t metres is
    // k (heightOver(h0 + dy t) - heightOver(h0)) / dy, and straight up from h to the fog's top k (H - heightOver(h)).
    // heightBack: the height at which that sum is v.
    auto heightOver=[](double h, double H) { return h<0.0 ? h : H*(1.0-std::exp(-h/H)); };
    auto heightBack=[](double v, double H) { return v<0.0 ? v : -H*std::log(std::max(1.0-v/H, 1e-300)); };
    // The sky's light on a point of that fog, per unit of a uniform sky's radiance, scattered along a way whose
    // direction has the height y: the phase function summed over the upper half of the directions, each dimmed by the
    // fog between the point and the sky (e^(-tau / the direction's height), tau the optical depth straight up from
    // the point): a table over the root of tau (0 to tauMost, 48 steps) and y (-1 to 1, 64 steps), by quadrature.
    const int skySteps=48, skyHeights=64;
    auto skyDimmedTable=[&](double g, double tauMost) {
        std::vector<double> table(size_t(skySteps+1)*(skyHeights+1));
        const int nt=48, np=96;
        for(int i=0;i<=skySteps;++i) for(int j=0;j<=skyHeights;++j) {
            const double root=double(i)/skySteps, tau=tauMost*root*root, y=-1.0+2.0*double(j)/skyHeights, side=std::sqrt(std::max(0.0, 1.0-y*y));
            double sum=0;
            for(int a=0;a<nt;++a) {
                const double theta=(a+0.5)/nt*1.5707963267949, st=std::sin(theta), ct=std::cos(theta), dim=std::exp(-tau/ct);
                for(int b2=0;b2<np;++b2) { const double phi=(b2+0.5)/np*6.28318530717959; sum+=phase(g, side*st*std::cos(phi)+y*ct)*st*dim; }
            }
            table[size_t(i)*(skyHeights+1)+j]=sum*(1.5707963267949/nt)*(6.28318530717959/np);
        }
        return table;
    };
    auto skyDimmed=[&](const std::vector<double>& table, double tauMost, double tau, double y) {
        const double a=std::clamp(std::sqrt(std::max(tau, 0.0)/tauMost), 0.0, 1.0)*skySteps, b=std::clamp((y+1.0)*0.5, 0.0, 1.0)*skyHeights;
        const int i=std::min(int(a), skySteps-1), j=std::min(int(b), skyHeights-1);
        const double fa=a-i, fb=b-j;
        auto at=[&](int ii, int jj) { return table[size_t(ii)*(skyHeights+1)+jj]; };
        return (at(i, j)*(1.0-fb)+at(i, j+1)*fb)*(1.0-fa)+(at(i+1, j)*(1.0-fb)+at(i+1, j+1)*fb)*fa;
    };
    // One way through that fog (extinction k under the base, scale height H; from the height h0 over the base, its
    // direction's height dy, over `way` metres), summed over the share u of the light the way's fog takes (u = 1 less
    // the transmittance to a point, so that each of 32 steps of u is the same share of the fog's own light):
    // sunPart: the sun's light at the way's points, per unit of its light (dimmed by the fog above each: e^(-tau /
    // the sun's height)); skyPart: the dimmed sky's (skyDimmed); taken: 1 less the way's transmittance.
    struct NightWay { double sunPart, skyPart, taken; };
    auto nightWay=[&](double k, double H, double h0, double dy, double way, double sunHeight, const std::vector<double>& table, double tauMost) {
        const double from=heightOver(h0, H), level=std::abs(dy)<1e-7 ? 1.0 : 0.0;
        const double depth=level>0.0 ? k*way*(h0<0.0 ? 1.0 : std::exp(-h0/H)) : k*(heightOver(h0+dy*way, H)-from)/dy;
        NightWay w{0.0, 0.0, 1.0-std::exp(-std::max(depth, 0.0))};
        const int n=32;
        for(int i=0;i<n;++i) {
            const double u=(i+0.5)/n*w.taken, at=-std::log(std::max(1.0-u, 1e-300));      // the optical depth to the point
            const double h=level>0.0 ? h0 : heightBack(from+at*dy/k, H), tau=k*(H-heightOver(h, H));
            w.sunPart+=std::exp(-tau/std::max(sunHeight, 0.001)); w.skyPart+=skyDimmed(table, tauMost, tau, dy);
        }
        w.sunPart*=w.taken/n; w.skyPart*=w.taken/n;
        return w;
    };
    const V3 daySun=sunDir;
    uint32_t frameBase=10000;
    for(const FogCase& fogCase:cases) {
        sunOn=!fogCase.noSun;
        const bool night=fogCase.place==6;
        const bool open=fogCase.place==1 || fogCase.place==2 || fogCase.place==5, outdoors=fogCase.place!=0;
        // The night's moon: 40 degrees up, at a right angle to the view (the frame's sun is put back after the case).
        sunDir=night ? normalize(V3{0.766f, 0.643f, 0.f}) : daySun;
        // The night's fog is dark (a hundredth of the day's light and less): shown at a hundred times the exposure, so
        // that its numbers stand clear of the check's floor.
        const float usualExposure=exposure;
        if(night) setExposure(usualExposure*100.f);
        const Camera& view=outdoors ? outside : cam;
        const Region* regions=fogCase.place==5 ? lampRegions : outdoors ? outRegions : roomRegions;
        // The medium and the frame of the place.
        float albedo[3]={0.58f, 0.97f, 0.99f};
        fogExtinction=1.f/39.9f; fogBase=100.f; fogRcpScale=0.02f; maxFogDistance=80.f; fogColorSent[3]=80.f; rayDistance=500.f;
        fogAnisotropy=number("TGPT_TEST_FOG_ANISO",0.f);
        skyIntensity=asked("TGPT_TEST_FOG_NOSKY") || fogCase.place==5 || (night && sunOn) ? 1e-6f : 1.f;
        if(outdoors) { maxFogDistance=5000.f; fogColorSent[3]=5000.f; rayDistance=1e6f; }
        if(open) { fogExtinction=0.0141f; fogBase=0.f; fogRcpScale=0.f; if(fogCase.place==2) { albedo[0]=0.3f; albedo[1]=0.5f; albedo[2]=0.7f; } }
        if(fogCase.place==3) { fogExtinction=0.01412f; fogBase=outPosition.y+26.8f; fogRcpScale=0.02453f; albedo[0]=albedo[1]=albedo[2]=1.f; }
        if(fogCase.place==4) { fogExtinction=0.001466f; fogBase=outPosition.y-2.1f; fogRcpScale=0.01008f; albedo[0]=albedo[1]=albedo[2]=1.f; if(!asked("TGPT_TEST_FOG_ANISO")) fogAnisotropy=0.6f; }
        // The game's night (the harness's audit of its save "night": a mean free path of 9 m, base height 67.56 and
        // maximum height 184.63, so a scale height of 16.95 m; the camera at 69.7; albedo and anisotropy as there).
        if(night) { fogExtinction=1.f/9.f; fogBase=outPosition.y-2.14f; fogRcpScale=1.f/16.95f; albedo[0]=0.243f; albedo[1]=0.239f; albedo[2]=0.208f; if(!asked("TGPT_TEST_FOG_ANISO")) fogAnisotropy=0.48f; }
        for(int c=0;c<3;++c) fogMediumAlbedo[c]=albedo[c];
        expect(SetSky(uint64_t(uintptr_t(open || night ? uniformSky.Get() : sky.Get())))==0, "the fog check's sky set");
        std::vector<TGPT_Light> lamps{light};
        if(fogCase.place==5) {
            // The open cases' lamp: on the camera's axis, a little above it; as strong as a candle group at 6 m, as a
            // hearth at 60 (so that its glow shows through 60 m of this fog).
            lamps.clear();
            TGPT_Light l=light;
            const V3 at=outPosition+outForward*fogCase.lamp+outUp*(fogCase.lamp<20.f ? 0.3f : 1.f);
            l.position[0]=at.x; l.position[1]=at.y; l.position[2]=at.z;
            for(float& c:l.color) c*=fogCase.lamp<20.f ? 25.f : 1000.f;
            l.id=2; lamps.push_back(l);
        } else if(fogCase.lamp>0.f) {
            TGPT_Light l=light;
            l.position[0]=cam.position.x+ahead.x*fogCase.lamp+0.01f; l.position[1]=cam.position.y+ahead.y*fogCase.lamp-0.01f; l.position[2]=cam.position.z+ahead.z*fogCase.lamp;
            l.id=2; lamps.push_back(l);
        }
        {
            uint32_t seed=12345u;
            auto next=[&] { seed=seed*1664525u+1013904223u; return float(seed>>8)/16777216.f; };
            for(int i=0, n=int(number("TGPT_TEST_FOG_LAMPS",0.f));i<n;++i) {
                TGPT_Light l=light;
                l.position[0]=-5.f+10.f*next(); l.position[1]=0.5f+4.5f*next(); l.position[2]=-5.f+10.f*next();
                for(float& c:l.color) c*=0.1f;
                l.id=uint32_t(10+i); lamps.push_back(l);
            }
        }
        for(auto& l:lamps) {
            for(float& c:l.color) c*=number("TGPT_TEST_FOG_LAMP_POWER",1.f);
            if(!ranged) {
                l.flags|=TGPT_LIGHT_NO_RANGE;
                const float lum=0.2126f*l.color[0]+0.7152f*l.color[1]+0.0722f*l.color[2];
                l.range=std::min(std::max(std::sqrt(lum/50.f), 0.5f), 40.f); // SceneLights.Reach
            }
        }
        if(const float cd=number("TGPT_TEST_FOG_EFFECT",0.f); cd>0.f) {
            TGPT_Light l=light;
            l.position[0]=cam.position.x+ahead.x*5.f+0.4f; l.position[1]=cam.position.y+ahead.y*5.f+0.3f; l.position[2]=cam.position.z+ahead.z*5.f;
            l.color[0]=cd; l.color[1]=cd*0.6f; l.color[2]=cd*1.2f; l.range=0.6f; l.flags=0; l.id=5; lamps.push_back(l);
        }
        const bool noLamps=fogCase.place==1 || fogCase.place==2 || night;
        expect(noLamps ? SetLights(nullptr,0)==0 : SetLights(lamps.data(),uint32_t(lamps.size()))==0,"the fog check's lamps set");
        instances=roomInstances; indices=roomIndices;
        auto add=[&](const M4& world, int mesh, uint32_t material) {
            TGPT_Instance in{}; for(int r=0;r<3;++r) for(int c=0;c<4;++c) in.transform[r*4+c]=world.m[r][c];
            in.mesh=mesh; in.materialBase=uint32_t(indices.size()); in.mask=0xFF;
            indices.push_back(material); instances.push_back(in);
        };
        if(open || night) { instances.clear(); indices.clear(); }
        if(night) add(translate_scale({0.f, 0.f, 0.f}, {4000.f, 1.f, 4000.f}), quadHandle, absorbingMaterial); // a ground that reflects nothing
        if(fogCase.place==1 || fogCase.place==5) add(translate_scale({0.f, 0.f, 0.f}, {4000.f, 1.f, 4000.f}), quadHandle, blackMaterial); // the black ground
        if(fogCase.place==2) add(translate_scale({0.f, -3000.f, 3000.f}, {1.f, 1.f, 1.f}), quadHandle, blackMaterial);      // nothing: a speck far behind and below
        if(fogCase.place==3 || fogCase.place==4) add(translate_scale({0.f, -0.35f, 0.f}, {4000.f, 1.f, 4000.f}), quadHandle, 0u); // a far ground under the mound
        if(fogCase.candle) {
            const V3 at{cam.position.x+ahead.x*4.f-0.5f, cam.position.y+ahead.y*4.f+0.2f, cam.position.z+ahead.z*4.f};
            add(mul(translate_scale(at, {0.03f, 1, 0.05f}), rot_x(-pi/2)), quadHandle, flameMaterial); // facing -z, toward the camera
        }
        expect(SetScene(instances.data(), uint32_t(instances.size()), indices.data(), uint32_t(indices.size()))==0, "the fog check's scene set");
        const uint32_t options=baseOptions|(fogCase.cache ? 0u : TGPT_OPTION_NO_RADIANCE_CACHE);
        const uint32_t frames=std::max(uint32_t(number("TGPT_TEST_FOG_FRAMES", fogCase.cache && outdoors ? 400.f : 160.f)), 32u);
        // What is known exactly (the open cases), a region's mean of the three channels, as the composite shows it.
        double known[5]{}; bool haveKnown=false;
        if(open && !asked("TGPT_TEST_FOG_GBUFFER")) {
            haveKnown=true;
            const std::vector<double> half=upperHalf(double(fogAnisotropy));
            const double tanHalf=std::tan(30.0*3.14159265358979/180.0), aspect=double(W)/double(H), k=double(fogExtinction);
            const double sunE=sunOn ? double(sunLux) : 0.0, skyOn=double(skyIntensity)>1e-3 ? double(skyIntensity) : 0.0;
            for(int region=0;region<5;++region) {
                double total=0;
                for(uint32_t y=regions[region].y0;y<regions[region].y1;++y) for(uint32_t x=regions[region].x0;x<regions[region].x1;++x) {
                    const float nx=float((2.0*(x+0.5)/W-1.0)*aspect*tanHalf), ny=float((2.0*(y+0.5)/H-1.0)*tanHalf);
                    const V3 d=normalize(outForward+outRight*nx+outUp*ny);
                    // The way: to the black ground (where the ray meets it within its 4 km), else the Max Fog Distance with
                    // the sky behind it.
                    double way=double(maxFogDistance); bool sky=true;
                    if(fogCase.place!=2 && d.y<-1e-6f) {
                        const double t=double(outPosition.y)/double(-d.y);
                        if(std::abs(outPosition.x+d.x*t)<=2000.0 && std::abs(outPosition.z+d.z*t)<=2000.0) { way=t; sky=false; }
                    }
                    const double through=std::exp(-k*way);
                    const double table=(double(d.y)+1.0)*128.0; const int i0=std::clamp(int(table), 0, 255);
                    const double skyShare=half[size_t(i0)]+(half[size_t(i0)+1]-half[size_t(i0)])*(table-i0);
                    const double sunShare=phase(double(fogAnisotropy), double(dot(sunDir, d)));
                    double glow=0;
                    if(fogCase.place==5) glow=lampGlow(outPosition, d, way, V3{lamps[0].position[0], lamps[0].position[1], lamps[0].position[2]}, double(lamps[0].radius), k, double(fogAnisotropy));
                    // The ground's highlight (groundShine) where the way ends on the ground: the unshadowed sun's (per
                    // unit of its light; this fog does not dim the sun), and the lamp's (per unit of its strength: over
                    // the squared distance, down to its own size, and the fog between the lamp and the ground). The sky
                    // makes none (TGPT_OPTION_NO_SPECULAR: no reflection rays). Both dimmed by the fog on the way back.
                    double sunShine=0, lampShine=0;
                    if(!sky) {
                        const V3 up{0.f, 1.f, 0.f}, back=d*-1.f;
                        if(sunE>0.0) sunShine=groundShine(up, back, sunDir);
                        if(fogCase.place==5) {
                            const V3 to=V3{lamps[0].position[0], lamps[0].position[1], lamps[0].position[2]}-(outPosition+d*float(way));
                            const double d2=double(dot(to, to)), dist=std::sqrt(d2), own=double(lamps[0].radius)*double(lamps[0].radius)+1e-4;
                            lampShine=groundShine(up, back, to*float(1.0/dist))/std::max(d2, own)*std::exp(-k*dist);
                        }
                    }
                    for(int c=0;c<3;++c) {
                        const double a=double(albedo[c]), s=double(flatSky[c])*skyOn;
                        // The mist's cache in an endless uniform fog: C = a D / (1 - a), D the first order over all directions.
                        const double mist=fogCase.cache ? a*(sunE/(4.0*3.14159265358979)+s*0.5)/(1.0-a) : 0.0;
                        // (Behind the fog: the ground's highlight, or the sky as HDRP drew it: the fixture's own sky colour,
                        // which the composite lays the fog over; not the rays' sky.)
                        const double behind=sky ? double(skyColor[c])/double(exposure) : sunE*sunShine+(fogCase.place==5 ? double(lamps[0].color[c])*lampShine : 0.0);
                        total+=(a*(1.0-through)*(sunE*sunShare+s*skyShare+mist)+behind*through+(fogCase.place==5 ? a*double(lamps[0].color[c])*glow : 0.0))*double(exposure);
                    }
                }
                known[region]=total/(3.0*double(regions[region].x1-regions[region].x0)*double(regions[region].y1-regions[region].y0));
            }
        }
        // The night's fog: what is known exactly of the tracer's (the light scattered once, each way by quadrature:
        // nightWay), and what HDRP's own volumetric fog gives for the same medium, light and sky, by hand
        // (VolumetricLighting.compute: "transmittance * scattering * (phase * blendValue.rgb + probeRadiance)", the
        // directional light through EvaluateLight_Directional, which dims it by the height fog above the point, times
        // its volumetricLightDimmer; probeRadiance its ambient probe, the sky's cube map from all around by the phase
        // function, the same at every point: not dimmed by the fog, not shadowed). hdrp: its probe as it is (the whole
        // cube map: a uniform one gives the sky's radiance itself); hdrpAbove: counting the sky above the horizon alone.
        double hdrp[5]{}, hdrpAbove[5]{}; bool haveHdrp=false;
        const double moonDimmer=1.23; // the volumetric dimmer of the game's moon (its night's audit)
        if(night && !asked("TGPT_TEST_FOG_GBUFFER")) {
            haveKnown=haveHdrp=true;
            const double k=double(fogExtinction), fogH=1.0/double(fogRcpScale), h0=double(outPosition.y)-double(fogBase), tauMost=k*(fogH+double(fogBase))*1.001;
            const std::vector<double> half=upperHalf(double(fogAnisotropy)), table=skyDimmedTable(double(fogAnisotropy), tauMost);
            const double tanHalf=std::tan(30.0*3.14159265358979/180.0), aspect=double(W)/double(H);
            const double sunE=sunOn ? double(sunLux) : 0.0, skyOn=double(skyIntensity)>1e-3 ? double(skyIntensity) : 0.0;
            for(int region=0;region<5;++region) {
                // (The frame: every second pixel each way; the picture is smooth.)
                const uint32_t stride=region==4 ? 2u : 1u;
                double total=0, totalHdrp=0, totalAbove=0, count=0;
                for(uint32_t y=regions[region].y0;y<regions[region].y1;y+=stride) for(uint32_t x=regions[region].x0;x<regions[region].x1;x+=stride) {
                    const float nx=float((2.0*(x+0.5)/W-1.0)*aspect*tanHalf), ny=float((2.0*(y+0.5)/H-1.0)*tanHalf);
                    const V3 d=normalize(outForward+outRight*nx+outUp*ny);
                    double way=double(maxFogDistance); bool sky=true;
                    if(d.y<-1e-6f) {
                        const double t=double(outPosition.y)/double(-d.y);
                        if(std::abs(outPosition.x+d.x*t)<=2000.0 && std::abs(outPosition.z+d.z*t)<=2000.0) { way=t; sky=false; }
                    }
                    const NightWay w=nightWay(k, fogH, h0, double(d.y), way, double(sunDir.y), table, tauMost);
                    const double table256=(double(d.y)+1.0)*128.0; const int i0=std::clamp(int(table256), 0, 255);
                    const double above=half[size_t(i0)]+(half[size_t(i0)+1]-half[size_t(i0)])*(table256-i0);
                    const double sunShare=phase(double(fogAnisotropy), double(dot(sunDir, d)));
                    for(int c=0;c<3;++c) {
                        // (Behind the fog: the sky as HDRP drew it, the fixture's own sky colour, whatever lights the fog.)
                        const double a=double(albedo[c]), s=double(flatSky[c])*skyOn, behind=sky ? double(skyColor[c])/double(exposure)*(1.0-w.taken) : 0.0;
                        total+=(a*(sunE*sunShare*w.sunPart+s*w.skyPart)+behind)*double(exposure);
                        totalHdrp+=(a*(moonDimmer*sunE*sunShare*w.sunPart+s*w.taken)+behind)*double(exposure);
                        totalAbove+=(a*(moonDimmer*sunE*sunShare*w.sunPart+s*above*w.taken)+behind)*double(exposure);
                    }
                    count+=3.0;
                }
                known[region]=total/count; hdrp[region]=totalHdrp/count; hdrpAbove[region]=totalAbove/count;
            }
        }
        double means[5]{}, grain[5]{};
        {
            fogFrameFlags=TGPT_FRAME_TRACED_FOG|(fogCase.cache ? TGPT_FRAME_FOG_WALK : 0u);
            // From an empty mist cache: a frame without the cache empties it for the next one that has it.
            cut=true; run(TGPT_VIEW_NONE,view,view,frameBase,baseOptions|TGPT_OPTION_NO_RADIANCE_CACHE); context->Flush();
            std::vector<double> sum(size_t(W)*H*3, 0.0), lumSum(size_t(W)*H, 0.0), lumSquares(size_t(W)*H, 0.0);
            uint32_t samples=0;
            for(uint32_t i=0;i<frames;++i) {
                cut=i==0; run(TGPT_VIEW_NONE,view,view,frameBase+1+i,options);
                if(i+16>=frames) {
                    const auto pixels=readback();
                    for(size_t j=0;j<pixels.size();++j) sum[j]+=pixels[j];
                    for(size_t j=0;j<lumSum.size();++j) { const double l=(double(pixels[j*3])+pixels[j*3+1]+pixels[j*3+2])/3.0; lumSum[j]+=l; lumSquares[j]+=l*l; }
                    ++samples;
                }
                context->Flush();
            }
            cut=false;
            frameBase+=1000;
            std::vector<float> averaged(sum.size());
            bool finite=true;
            for(size_t j=0;j<sum.size();++j) { averaged[j]=float(sum[j]/samples); finite=finite&&std::isfinite(averaged[j])&&averaged[j]>=0.f; }
            for(int r=0;r<5;++r) {
                double total=0, spread=0;
                for(uint32_t y=regions[r].y0;y<regions[r].y1;++y) for(uint32_t x=regions[r].x0;x<regions[r].x1;++x) {
                    const float* px=&averaged[(size_t(y)*W+x)*3]; total+=double(px[0])+px[1]+px[2];
                    const size_t j=size_t(y)*W+x; const double m=lumSum[j]/samples;
                    spread+=std::sqrt(std::max(lumSquares[j]/samples-m*m, 0.0)*double(samples)/double(samples-1));
                }
                const double count=double(regions[r].x1-regions[r].x0)*double(regions[r].y1-regions[r].y0);
                means[r]=total/(3.0*count);
                // The grain: how far a pixel strays from its own mean from frame to frame, over the region's mean.
                grain[r]=spread/count/std::max(means[r], 1e-9);
            }
            char stages[2048]{}; GetStageTimes(stages,sizeof(stages));
            std::printf("FOG %s: %s %.5f, %s %.5f, %s %.5f, %s %.5f, frame %.8f; grain %.3f %.3f %.3f %.3f %.3f; %s\n",fogCase.name,
                        regions[0].name,means[0],regions[1].name,means[1],regions[2].name,means[2],regions[3].name,means[3],means[4],
                        grain[0],grain[1],grain[2],grain[3],grain[4],stages);
            if(single || oneCase) {
                save(averaged,"fog.png",true);
                // The undenoised linear mean (PNG display transforms lose precision).
                std::ofstream raw(images/"fog-mean.f32",std::ios::binary);
                raw.write(reinterpret_cast<const char*>(averaged.data()),std::streamsize(averaged.size()*sizeof(float)));
            }
            expect(finite,"fog image finite and nonnegative");
        }
        // Against what is known exactly, else against the case's pin. (The mist's fixed point is printed, not judged:
        // the header says why.)
        const bool pinned=fogCase.pin[4]>0.0, judged=!((fogCase.place==2 || night) && fogCase.cache);
        if(haveKnown || pinned) {
            const double* want=haveKnown ? known : fogCase.pin;
            bool right=true; double furthest=0;
            for(int r=0;r<5;++r) {
                const double off=std::abs(means[r]-want[r]);
                furthest=std::max(furthest, off/std::max(want[r], 1e-9));
                right=right && off<=tolerance*want[r]+floorLevel;
            }
            std::printf("      %s: over %s: %s x%.3f (%.5f), %s x%.3f (%.5f), %s x%.3f (%.5f), %s x%.3f (%.5f), frame x%.3f (%.5f) (the furthest off %.1f %%)\n",
                        fogCase.name,haveKnown ? "what is known exactly" : "its pin",
                        regions[0].name,means[0]/std::max(want[0],1e-9),want[0],regions[1].name,means[1]/std::max(want[1],1e-9),want[1],
                        regions[2].name,means[2]/std::max(want[2],1e-9),want[2],regions[3].name,means[3]/std::max(want[3],1e-9),want[3],
                        means[4]/std::max(want[4],1e-9),want[4],100.0*furthest);
            if(judged) expect(right,haveKnown ? "the fog is what is known exactly (each region's mean within 3 %)" : "the fog is what its pin holds it to (each region's mean within 3 %)");
        }
        if(haveHdrp) {
            std::printf("      %s: HDRP's own fog by hand (%s): %s %.5f, %s %.5f, %s %.5f, %s %.5f, frame %.5f; the tracer's over it: x%.3f, x%.3f, x%.3f, x%.3f, frame x%.3f\n",
                        fogCase.name,sunOn ? "its moon with the volumetric dimmer 1.23" : "its probe: the sky from all around, not dimmed by the fog",
                        regions[0].name,hdrp[0],regions[1].name,hdrp[1],regions[2].name,hdrp[2],regions[3].name,hdrp[3],hdrp[4],
                        means[0]/std::max(hdrp[0],1e-9),means[1]/std::max(hdrp[1],1e-9),means[2]/std::max(hdrp[2],1e-9),means[3]/std::max(hdrp[3],1e-9),means[4]/std::max(hdrp[4],1e-9));
            if(!sunOn) std::printf("      %s: the same counting the sky above the horizon alone: %s %.5f, %s %.5f, %s %.5f, %s %.5f, frame %.5f; the tracer's over it: x%.3f, x%.3f, x%.3f, x%.3f, frame x%.3f\n",
                        fogCase.name,regions[0].name,hdrpAbove[0],regions[1].name,hdrpAbove[1],regions[2].name,hdrpAbove[2],regions[3].name,hdrpAbove[3],hdrpAbove[4],
                        means[0]/std::max(hdrpAbove[0],1e-9),means[1]/std::max(hdrpAbove[1],1e-9),means[2]/std::max(hdrpAbove[2],1e-9),means[3]/std::max(hdrpAbove[3],1e-9),means[4]/std::max(hdrpAbove[4],1e-9));
        }
        if(night) setExposure(usualExposure);
    }
    sunDir=daySun;
    ReadStatus(&status);
    expect(status.framesFailed==0,"fog frames completed without a failed trace");
    std::printf(failures ? "FAILED %d fog checks\n" : "fog-only checks passed\n",failures);
    return failures ? 1 : 0;
}
