// SPDX-License-Identifier: MIT
// PT-112, the sky (docs/agents/sky-1.md, sky-2.md), ON TRIAL behind the plugin's hidden "Sky trial": the sky as the tracer makes
// it, for the sky map every ray reads (pt_sky.hlsl) and for the camera's rays that meet nothing (pt_compose).
//
// The model is the published one HDRP 17's Physically Based Sky implements, written anew here (nothing of HDRP's code is
// copied; its parameters come from the plugin as HDRP's own methods work them out, tgpt_api.h TGPT_Sky):
// - A planet of radius R under an atmosphere of depth D: air (Rayleigh) and aerosols (Mie) whose densities fall off
//   exponentially with their scale heights, and an ozone layer that only absorbs, its density a tent between its lowest
//   height and that plus its width (Bruneton and Neyret 2008, "Precomputed Atmospheric Scattering").
// - The light of each celestial body that lights the atmosphere reaches a point through the transmittance to the
//   atmosphere's top (a table over height and the sun's angle: Bruneton's parameterisation), is scattered once towards the
//   eye by air (Rayleigh's phase) and aerosols (Cornette-Shanks' phase with the aerosols' anisotropy), and the light it
//   scatters more than once is Hillaire's (2020, "A Scalable and Production Ready Sky and Atmosphere Rendering Technique",
//   section 5.5) transfer: a 32 x 32 table over height and the sun's angle of the second order's light over (1 - its
//   transfer factor), times the total scattering at the point.
// - Along each view ray the single and multiple scattering are marched to the atmosphere's top or the ground, each step
//   integrated over its length with its own transmittance (Hillaire 2015, "Physically Based and Unified Volumetric
//   Rendering in Frostbite", slide 28). The ground is a Lambertian surface of HDRP's ground tint lit by each body through
//   the atmosphere and by the sky above it (a table of its irradiance over the sun's angle).
// - HDRP's artistic controls on top (its documented Color Saturation, Alpha Saturation, Alpha Multiplier, Horizon and
//   Zenith Tint and Horizon Zenith Shift), and its sky intensity (exposure x multiplier).
// - The celestial bodies' discs and flares, a moon's phase and earthshine, as HDRP 17 documents its directional lights'
//   celestial body settings; the discs only where the camera sees them (HDRP leaves them out of its sky cube map too).
// - HDRP's Cloud Layer: the game's cloud map baked with its sun as HDRP's Cloud Layer documents it (a slab 800 m thick at
//   the layer's altitude, the light marched through it in the layer's steps, two octaves of a forward and a backward
//   Henyey-Greenstein lobe), seen as a function of the direction only, drifted by the wind, lit by HDRP's sun through the
//   atmosphere and by the cloudless sky's light from below (pt_sky.hlsl mode 4).
// Lengths in metres, directions in the world, light in HDRP's units (lux, and its radiance), not pre-exposed.
#ifndef PT_SKY_HLSLI
#define PT_SKY_HLSLI

static const uint SKY_MODE_HDRP = 0u, SKY_MODE_MEASURE = 1u, SKY_MODE_OWN = 2u;   // tgpt_api.h TGPT_SKY_*
static const uint SKY_FLAG_HDRP_PIXELS = 1u, SKY_FLAG_CLOUDS = 2u, SKY_FLAG_UPPER = 4u, SKY_FLAG_SHOW_MAP = 8u;
static const uint SKY_BODY_LIGHTS = 1u, SKY_BODY_REFLECTS = 2u;
static const uint SKY_BODY_ROWS = 8u;                                          // tracer.cpp kSkyBodyRows
static const uint SKY_LAYER_ROWS = 5u;                                         // tracer.cpp kSkyLayerRows
static const uint2 SKY_TRANSMITTANCE_SIZE = uint2(256u, 64u);                  // tracer.cpp kSkyTransmittance*
static const uint SKY_MULTIPLE_SIZE = 32u;                                     // tracer.cpp kSkyMultiple
static const uint SKY_GROUND_SIZE = 256u;                                      // tracer.cpp kSkyGround
static const float SKY_CLOUD_SLAB = 800.0;                                     // the clouds' slab, as HDRP's Cloud Layer bakes it

// ---------------------------------------------------------------- the frame's numbers (tracer.cpp fill_sky)
uint sky_mode() { return g.skyMaps2.z & 0xFFu; }
uint sky_flags() { return g.skyMaps2.z >> 8; }
// The tracer's own sky this frame: its tables, the clouds' bake, the map, the misses.
bool sky_own() { return sky_mode() == SKY_MODE_OWN && g.skyMaps.x != INVALID && g.skyMaps.w != INVALID; }
// Whether the composite draws the pixels whose camera ray met nothing (else HDRP's colour stays there).
bool sky_draws_misses() { return sky_mode() == SKY_MODE_MEASURE || (sky_own() && (sky_flags() & SKY_FLAG_HDRP_PIXELS) == 0u); }
float sky_R() { return g.sky[0].w; }
float sky_D() { return g.sky[2].w; }
// The camera's height above the ground (at least 1 m, as HDRP keeps it) and its place in the planet's space.
float sky_camera_height() { return g.sky[6].w; }
float3 sky_camera() { return g.sky[0].xyz; }
uint sky_body_count() { return asuint(g.sky[9].w); }
float sky_intensity() { return g.sky[10].w; }

// ---------------------------------------------------------------- geometry over the sphere, kept exact near the ground
// At height h above the ground, the radius r = R + h; h (2R + h) = r^2 - R^2 without the difference of two huge squares.
float sky_r2_minus_R2(float h) { return h * (2.0 * sky_R() + h); }
// The cosine of the angle from the zenith at which a ray from height h grazes the ground (below it: the ground).
float sky_horizon_mu(float h) { const float r = sky_R() + h; return -sqrt(saturate(sky_r2_minus_R2(h) / (r * r))); }
// How far a ray from height h, mu the cosine of its angle from the zenith, goes to the atmosphere's top (from inside).
float sky_to_top(float h, float mu) {
    const float r = sky_R() + h, D = sky_D();
    const float d = mu * mu + max(D - h, 0.0) * (2.0 * sky_R() + D + h) / (r * r);
    return max(r * (-mu + sqrt(max(d, 0.0))), 0.0);
}
// How far it goes to the ground; negative: it never meets it.
float sky_to_ground(float h, float mu) {
    const float r = sky_R() + h;
    if (mu >= 0.0) return -1.0;
    const float d = mu * mu - sky_r2_minus_R2(h) / (r * r);
    return d < 0.0 ? -1.0 : max(r * (-mu - sqrt(d)), 0.0);
}
// The height and the cosine from the local zenith after t metres along a ray from height h at mu.
float2 sky_after(float h, float mu, float t) {
    const float r = sky_R() + h;
    const float q = sky_r2_minus_R2(h) + t * (t + 2.0 * r * mu);       // r'^2 - R^2
    const float r1 = sqrt(max(sky_R() * sky_R() + q, 1.0));
    return float2(max(q / (r1 + sky_R()), 0.0), clamp((r * mu + t) / r1, -1.0, 1.0));
}

// ---------------------------------------------------------------- the media
struct SkyMedium { float3 extinction, air, aerosol; };
SkyMedium sky_medium(float h) {
    SkyMedium m;
    const float air = exp(-h * g.sky[1].w), aerosol = exp(-h * g.sky[4].w);
    const float ozone = g.sky[5].y > 0.0 ? saturate(1.0 - abs(h - g.sky[5].x) * g.sky[5].y) : 0.0;
    m.air = g.sky[2].rgb * air;
    m.aerosol = g.sky[3].rgb * aerosol;
    m.extinction = max(g.sky[1].rgb * air + g.sky[3].w * aerosol + g.sky[4].rgb * ozone, 1e-12);
    return m;
}
float sky_rayleigh(float c) { return 3.0 / (16.0 * PI) * (1.0 + c * c); }
// Cornette-Shanks with the aerosols' anisotropy g (sky[5].z) and its constant part (sky[5].w).
float sky_mie(float c) { const float g1 = g.sky[5].z; return g.sky[5].w * (1.0 + c * c) / pow(max(1.0 + g1 * g1 - 2.0 * g1 * c, 1e-6), 1.5); }
float sky_cornette_shanks(float g1, float c) {
    return 3.0 / (8.0 * PI) * (1.0 - g1 * g1) / (2.0 + g1 * g1) * (1.0 + c * c) / pow(max(1.0 + g1 * g1 - 2.0 * g1 * c, 1e-6), 1.5);
}
float sky_henyey_greenstein(float g1, float c) { return (1.0 - g1 * g1) / (4.0 * PI * pow(max(1.0 + g1 * g1 - 2.0 * g1 * c, 1e-6), 1.5)); }

// ---------------------------------------------------------------- the transmittance table (Bruneton's parameterisation)
// u from the distance to the top between its least and most at that height, v from the height's horizon distance.
float2 sky_transmittance_uv(float h, float mu) {
    const float H = sqrt(sky_r2_minus_R2(sky_D())), rho = sqrt(sky_r2_minus_R2(h));
    const float d = sky_to_top(h, mu), dMin = sky_D() - h, dMax = rho + H;
    const float xMu = saturate((d - dMin) / max(dMax - dMin, 1e-3)), xR = saturate(rho / H);
    const float2 n = float2(SKY_TRANSMITTANCE_SIZE);
    return float2(0.5 / n.x + xMu * (1.0 - 1.0 / n.x), 0.5 / n.y + xR * (1.0 - 1.0 / n.y));
}
// The inverse, for the table's texel: its height and cosine.
float2 sky_transmittance_texel(float2 uv) {
    const float2 n = float2(SKY_TRANSMITTANCE_SIZE);
    const float xMu = saturate((uv.x - 0.5 / n.x) / (1.0 - 1.0 / n.x)), xR = saturate((uv.y - 0.5 / n.y) / (1.0 - 1.0 / n.y));
    const float R = sky_R(), H = sqrt(sky_r2_minus_R2(sky_D())), rho = H * xR;
    const float h = rho * rho / (sqrt(R * R + rho * rho) + R);
    const float r = R + h, dMin = sky_D() - h, dMax = rho + H, d = dMin + xMu * (dMax - dMin);
    const float mu = d <= 0.0 ? 1.0 : clamp((H * H - rho * rho - d * d) / (2.0 * r * d), -1.0, 1.0);
    return float2(h, mu);
}
// The light that reaches height h from a body at mu (0 below the ground's horizon).
float3 sky_transmittance(float h, float mu) {
    if (sky_to_ground(h, mu) >= 0.0) return 0.0;
    Texture2D<float4> t = ResourceDescriptorHeap[g.skyMaps.x];
    return t.SampleLevel(s_linearClamp, sky_transmittance_uv(h, mu), 0).rgb;
}
// The multiple scattering's transfer at height h with the body at mu (Hillaire 2020, 5.5), per unit of the body's light.
float3 sky_multiple(float h, float mu) {
    Texture2D<float4> t = ResourceDescriptorHeap[g.skyMaps.y];
    const float n = float(SKY_MULTIPLE_SIZE);
    const float2 uv = float2(saturate(mu * 0.5 + 0.5), saturate(h / sky_D())) * ((n - 1.0) / n) + 0.5 / n;
    return t.SampleLevel(s_linearClamp, uv, 0).rgb;
}
// The ground's irradiance with the body at mu, per unit of its light: what reaches it straight and from the sky above.
float3 sky_ground_irradiance(float mu) {
    Texture2D<float4> t = ResourceDescriptorHeap[g.skyMaps.z];
    const float n = float(SKY_GROUND_SIZE);
    return t.SampleLevel(s_linearClamp, float2(saturate(mu * 0.5 + 0.5) * ((n - 1.0) / n) + 0.5 / n, 0.5), 0).rgb;
}

// ---------------------------------------------------------------- the celestial bodies (fill_sky: SKY_BODY_ROWS rows each)
struct SkyBody {
    float3 direction; float angularRadius;   // towards it
    float3 color; uint flags;
    float3 surface; float cosInner;
    float3 flare; float cosOuter;
    float3 sunTravel; float earthshine;
    float3 right; int texture;
    float3 up; float flareSize;
    float flareFalloff;
};
SkyBody sky_body(uint i) {
    const uint b = i * SKY_BODY_ROWS;
    SkyBody o;
    o.direction = g.skyBodies[b].xyz; o.angularRadius = g.skyBodies[b].w;
    o.color = g.skyBodies[b + 1].xyz; o.flags = asuint(g.skyBodies[b + 1].w);
    o.surface = g.skyBodies[b + 2].xyz; o.cosInner = g.skyBodies[b + 2].w;
    o.flare = g.skyBodies[b + 3].xyz; o.cosOuter = g.skyBodies[b + 3].w;
    o.sunTravel = g.skyBodies[b + 4].xyz; o.earthshine = g.skyBodies[b + 4].w;
    o.right = g.skyBodies[b + 5].xyz; o.texture = asint(g.skyBodies[b + 5].w);
    o.up = g.skyBodies[b + 6].xyz; o.flareSize = g.skyBodies[b + 6].w;
    o.flareFalloff = g.skyBodies[b + 7].x;
    return o;
}

// ---------------------------------------------------------------- along a view ray
// What a view ray from height h (the planet's point below it in the direction `up`) along V gathers: the light the
// atmosphere scatters towards its start, from it to the top or the ground, lit by the frame's bodies, or by one body of
// unit light towards unitDir when unitSun; and the transmittance over that way. With split > 0, also the scattered light
// over its first split metres (the haze in front of the clouds). withMultiple false: the multiple scattering's table is not
// read (while it is being made). The camera is inside the atmosphere (the game's lies under 2 km; HDRP keeps it 1 m up).
struct SkyRay { float3 light, transmittance, lightToSplit; bool ground; float groundT; };
SkyRay sky_march(float h, float3 up, float3 V, uint steps, float split, bool unitSun, float3 unitDir, bool withMultiple) {
    SkyRay o; o.light = 0.0; o.transmittance = 1.0; o.lightToSplit = 0.0; o.ground = false; o.groundT = 0.0;
    h = clamp(h, 0.0, sky_D() - 1.0);
    const float mu = clamp(dot(up, V), -1.0, 1.0);
    const float toGround = sky_to_ground(h, mu);
    o.ground = toGround >= 0.0;
    const float tEnd = o.ground ? toGround : sky_to_top(h, mu);
    o.groundT = tEnd;
    const uint count = unitSun ? 1u : sky_body_count();
    float3 T = 1.0;
    for (uint i = 0u; i < steps; ++i) {
        // Quadratic steps: short near the start, where the air is densest and the first stretch matters most.
        const float a = float(i) / float(steps), b = float(i + 1u) / float(steps);
        const float ta = a * a * tEnd, tb = b * b * tEnd, dt = tb - ta, tm = 0.5 * (ta + tb);
        const float hm = sky_after(h, mu, tm).x;
        const float3 n = normalize(up * (sky_R() + h) + V * tm); // the local zenith there
        const SkyMedium m = sky_medium(hm);
        float3 S = 0.0;
        for (uint k = 0u; k < count; ++k) {
            float3 L = unitDir, color = 1.0;
            if (!unitSun) {
                const SkyBody body = sky_body(k);
                if ((body.flags & SKY_BODY_LIGHTS) == 0u) continue;
                L = body.direction; color = body.color;
            }
            const float c = dot(L, V), mul = dot(n, L);
            float3 s = sky_transmittance(hm, mul) * (m.air * sky_rayleigh(c) + m.aerosol * sky_mie(c));
            if (withMultiple) s += sky_multiple(hm, mul) * (m.air + m.aerosol);
            S += color * s;
        }
        const float3 stepT = exp(-m.extinction * dt);
        const float3 gathered = T * S * (1.0 - stepT) / m.extinction;
        if (split > 0.0) o.lightToSplit += gathered * saturate((split - ta) / max(dt, 1e-3));
        o.light += gathered;
        T *= stepT;
    }
    o.transmittance = T;
    return o;
}

// The light the ground sends back where the ray met it (HDRP's ground tint, Lambertian), lit by every body that lights.
float3 sky_ground_light(float3 groundNormal) {
    float3 c = 0.0;
    const uint count = sky_body_count();
    for (uint k = 0u; k < count; ++k) {
        const SkyBody body = sky_body(k);
        if ((body.flags & SKY_BODY_LIGHTS) == 0u) continue;
        c += body.color * sky_ground_irradiance(dot(groundNormal, body.direction));
    }
    return c * g.sky[6].rgb / PI;
}

float3 sky_desaturate(float3 c, float s) { return lerp(luminance(c).xxx, c, s); }
// HDRP's artistic controls on what the atmosphere scatters and hides, by how high the ray looks (cosChi from the zenith at
// the start; cosHorizon the horizon's there).
void sky_artistic(float cosChi, float cosHorizon, inout float3 light, inout float3 opacity) {
    light = sky_desaturate(light, g.sky[9].x);
    opacity = sky_desaturate(opacity, g.sky[9].y) * g.sky[9].z;
    const float horizonAngle = acos(clamp(cosHorizon, -1.0, 1.0)), angle = acos(clamp(cosChi, -1.0, 1.0));
    const float up = horizonAngle > 1e-4 ? saturate(1.0 - angle / horizonAngle) : 1.0; // 0 at the horizon, 1 at the zenith
    const float shaped = exp(g.sky[7].w * up) * g.sky[8].w - g.sky[8].w;         // Horizon Zenith Shift (fill_sky)
    light *= lerp(g.sky[7].rgb, g.sky[8].rgb, shaped);
}

// ---------------------------------------------------------------- the clouds (HDRP's Cloud Layer)
struct SkyLayer {
    float4 opacities; float rotation, steps, sigma, altitude;
    float3 light; int map;
    float2 scroll; float scrolled; uint distortion;
    float ambientDimmer; int flowmap; bool on;
};
SkyLayer sky_layer(uint l) {
    const uint b = l * SKY_LAYER_ROWS;
    SkyLayer o;
    o.opacities = g.skyClouds[b];
    o.rotation = g.skyClouds[b + 1].x; o.steps = g.skyClouds[b + 1].y; o.sigma = g.skyClouds[b + 1].z; o.altitude = g.skyClouds[b + 1].w;
    o.light = g.skyClouds[b + 2].xyz; o.map = asint(g.skyClouds[b + 2].w);
    o.scroll = g.skyClouds[b + 3].xy; o.scrolled = g.skyClouds[b + 3].z; o.distortion = asuint(g.skyClouds[b + 3].w);
    o.ambientDimmer = g.skyClouds[b + 4].x; o.flowmap = asint(g.skyClouds[b + 4].y); o.on = asuint(g.skyClouds[b + 4].z) != 0u;
    return o;
}
bool sky_clouds_on() { return (sky_flags() & SKY_FLAG_CLOUDS) != 0u && g.skyMaps2.x != INVALID; }
bool sky_clouds_upper() { return (sky_flags() & SKY_FLAG_UPPER) != 0u; }
uint sky_cloud_layers() { return sky_layer(1u).on ? 2u : 1u; }
// A direction's place in a latitude-longitude map as HDRP's Cloud Layer lays its map (u from atan2(x, z), v from the
// elevation; the upper hemisphere stretched over the whole map when the layer covers it alone).
float2 sky_cloud_uv(float3 dir) {
    float2 uv = float2(atan2(dir.x, dir.z) * (0.5 / PI) + 0.5, asin(clamp(dir.y, -1.0, 1.0)) / PI + 0.5);
    if (sky_clouds_upper()) uv.y = uv.y * 2.0 - 1.0;
    return uv;
}
float3 sky_cloud_direction(float2 uv) {
    if (sky_clouds_upper()) uv.y = uv.y * 0.5 + 0.5;
    const float phi = (uv.x - 0.5) * 2.0 * PI, lat = (uv.y - 0.5) * PI;
    const float y = sin(lat), s = cos(lat);
    return float3(sin(phi) * s, y, cos(phi) * s);
}
// A direction turned about the up axis by an angle given as (cos, sin), as HDRP turns its clouds' sun.
float3 sky_turn_up(float3 p, float2 cs) { return float3(cs.x * p.x - cs.y * p.z, p.y, cs.y * p.x + cs.x * p.z); }
// What the baked layer holds in a direction: the sun's light it scatters (x) and its opacity (y). The layers lie one under
// the other in the baked texture.
float2 sky_cloud_baked(float3 dir, uint l) {
    Texture2D<float4> t = ResourceDescriptorHeap[g.skyMaps2.x];
    float2 uv = sky_cloud_uv(dir);
    uint w, hgt; t.GetDimensions(w, hgt);
    const float rows = float(hgt) / float(sky_cloud_layers());
    uv.y = (clamp(uv.y * rows, 0.5, rows - 0.5) + float(l) * rows) / float(hgt);
    return t.SampleLevel(s_linearWrap, uv, 0).xy;
}
// How far a ray from the ground goes along dir to the sphere `altitude` above it.
float sky_cloud_distance(float3 dir, float altitude) {
    const float R = sky_R(), mu = dir.y;
    return R * (-mu + sqrt(max(mu * mu + altitude * (2.0 * R + altitude) / (R * R), 0.0)));
}
// The light of HDRP's sun at a cloud (its colour comes with the layer): through the atmosphere as HDRP's controls let it
// (Alpha Saturation and Multiplier: the game's 0 lets it all through), as much of its disc as stands above the horizon there.
float3 sky_cloud_sun(float3 positionFromGround) {
    const float3 sun = g.sky[10].xyz;
    const float R = sky_R();
    const float3 p = positionFromGround + float3(0.0, R, 0.0);
    const float r = length(p), h = max(r - R, 0.0), mu = dot(p, sun) / r;
    const float horizon = sky_horizon_mu(h);
    const float sunRadius = acos(clamp(g.sunDir.w, -1.0, 1.0));
    const float above = asin(clamp(mu, -1.0, 1.0)) - asin(clamp(horizon, -1.0, 1.0));
    const float visible = sunRadius > 1e-5 ? saturate(0.5 + 0.5 * above / sunRadius) : (above >= 0.0 ? 1.0 : 0.0);
    if (!(visible > 0.0)) return 0.0;
    float3 opacity = 1.0 - sky_transmittance(h, max(mu, horizon + 1e-5));
    opacity = sky_desaturate(opacity, g.sky[9].y) * g.sky[9].z;
    return (1.0 - opacity) * visible;
}
// The cloudless sky's light on the clouds from below (pt_sky.hlsl mode 4), HDRP's clouds' ambient probe.
float3 sky_cloud_ambient() {
    if (g.skyMaps2.y == INVALID) return 0.0;
    RWTexture2D<float4> a = ResourceDescriptorHeap[g.skyMaps2.y];
    return a[uint2(0, 0)].rgb;
}
float2 sky_cloud_sample(float3 dir, uint l) { return sky_cloud_baked(dir, l); }
// One layer in a direction: premultiplied (its light, its opacity), times the cloud layer's opacity.
float4 sky_cloud_layer(float3 dir, uint l) {
    const SkyLayer layer = sky_layer(l);
    const float3 position = dir * sky_cloud_distance(dir, layer.altitude);
    float2 cloud;
    if (layer.distortion != 0u) {
        // The wind: two samples shifted along it by up to the most a cloud travels (twice the altitude), half a cycle apart,
        // crossfaded so that the one that jumps back is the one not shown.
        const float travel = 2.0 * layer.altitude;
        const float2 shift = frac(layer.scrolled / travel + float2(0.0, 0.5)) - 0.5;
        float3 delta = float3(layer.scroll.x, 0.0, layer.scroll.y);
        if (layer.distortion == 2u && layer.flowmap >= 0 && g.textures.x != INVALID) {
            const float3 tangent = normalize(cross(dir, float3(0.0, 1.0, 0.0))), bitangent = cross(tangent, dir);
            const float3 windDir = sky_turn_up(dir, layer.scroll);
            Texture2D<float4> flow = ResourceDescriptorHeap[NonUniformResourceIndex(g.textures.x + uint(layer.flowmap))];
            const float2 f = flow.SampleLevel(s_linearWrap, sky_cloud_uv(windDir), 0).rg * 2.0 - 1.0;
            delta = f.x * tangent + f.y * bitangent;
        }
        const float2 a = sky_cloud_sample(normalize(position + shift.x * delta * travel), l);
        const float2 b = sky_cloud_sample(normalize(position + shift.y * delta * travel), l);
        cloud = lerp(a, b, abs(2.0 * shift.x));
    } else cloud = sky_cloud_sample(dir, l);
    const float3 light = layer.light * sky_cloud_sun(position);
    const float3 ambient = max(sky_cloud_ambient(), 0.0) * layer.ambientDimmer;
    return float4(cloud.x * light + ambient * cloud.y, cloud.y) * g.skyClouds[2u * SKY_LAYER_ROWS].x;
}
// Both layers over each other (the second under the first); none below the horizon when the map covers the upper half.
float4 sky_clouds(float3 dir) {
    if (!sky_clouds_on()) return 0.0;
    if (sky_clouds_upper() && dir.y < 0.0) return 0.0;
    float4 c = sky_layer(0u).on ? sky_cloud_layer(dir, 0u) : 0.0;
    if (sky_layer(1u).on) c += sky_cloud_layer(dir, 1u) * (1.0 - c.a);
    return c;
}

// ---------------------------------------------------------------- the discs
// The bodies' discs and flares where the camera looks (HDRP's sky shows them; its cube map leaves them out).
float3 sky_discs(float3 V) {
    float3 c = 0.0;
    const uint count = sky_body_count();
    for (uint k = 0u; k < count; ++k) {
        const SkyBody body = sky_body(k);
        if (!(body.angularRadius > 0.0)) continue;
        const float cosA = dot(body.direction, V);
        if (cosA >= body.cosInner) {
            float3 color = body.surface;
            if ((body.flags & SKY_BODY_REFLECTS) != 0u) {
                // A moon: lit by its sun as a Lambertian ball, by the earth's light (earthshine) by its phase.
                const float rho = tan(body.angularRadius);
                const float near = cosA - sqrt(max(rho * rho - (1.0 - cosA * cosA), 0.0));
                const float3 normal = normalize(V * near - body.direction);
                const float lit = saturate(dot(normal, -body.sunTravel));
                const float sinPhase = sqrt(max(1.0 - dot(body.sunTravel, -body.direction), 0.0)) * 0.70710678;
                color *= lit / PI + (1.0 - sinPhase * sqrt(sinPhase)) * body.earthshine;
            }
            if (body.texture >= 0 && g.textures.x != INVALID) {
                const float2 angles = float2(asin(clamp(dot(-V, body.right), -1.0, 1.0)), asin(clamp(-dot(-V, body.up), -1.0, 1.0)));
                const float2 uv = angles / body.angularRadius * 0.5 + 0.5;
                Texture2D<float4> surface = ResourceDescriptorHeap[NonUniformResourceIndex(g.textures.x + uint(body.texture))];
                color *= surface.SampleLevel(s_linearClamp, uv, 0).rgb;
            }
            c = color;
        } else if (cosA >= body.cosOuter) {
            const float past = max(acos(clamp(cosA, -1.0, 1.0)) - body.angularRadius, 0.0);
            c += body.flare * pow(saturate(1.0 - past / max(body.flareSize, 1e-7)), body.flareFalloff);
        }
    }
    return c;
}

// ---------------------------------------------------------------- the sky in a direction
static const uint SKY_MAP_STEPS = 32u, SKY_PIXEL_STEPS = 40u;
// The sky without clouds from the camera: the atmosphere (with HDRP's controls), the ground under the horizon, and the discs
// where `discs`; times HDRP's sky intensity. lightToClouds: the atmosphere's light in front of the clouds' first layer.
float3 sky_atmosphere(float3 V, bool discs, uint steps, out float3 lightToClouds) {
    const float h = sky_camera_height();
    const float3 up = normalize(sky_camera());
    const float split = sky_clouds_on() ? sky_cloud_distance(V, sky_layer(0u).altitude) : 0.0;
    const SkyRay ray = sky_march(h, up, V, steps, split, false, 0.0, true);
    float3 light = ray.light, opacity = 1.0 - ray.transmittance;
    float3 lightSplit = ray.lightToSplit, opacitySplit = 0.0;
    const float cosChi = dot(up, V), cosHorizon = sky_horizon_mu(min(h, sky_D()));
    sky_artistic(cosChi, cosHorizon, light, opacity);
    sky_artistic(cosChi, cosHorizon, lightSplit, opacitySplit);
    float3 behind = 0.0;
    if (ray.ground) {
        const float3 p = up * (sky_R() + h) + V * ray.groundT;
        behind = sky_ground_light(normalize(p));
    } else if (discs) behind = sky_discs(V);
    lightToClouds = lightSplit * sky_intensity();
    return (light + behind * (1.0 - opacity)) * sky_intensity();
}
// The sky the rays read (the map, pt_sky.hlsl mode 5): no discs; the clouds with the atmosphere's light in front of them, as
// the camera sees them (sky-3: without that light the clouds in the map, which reflections and bounced light read, had dark
// hearts where the camera's are white; 11 % apart under a 30-degree sun, sky-2.md).
float3 sky_for_map(float3 V) {
    float3 haze;
    const float3 sky = sky_atmosphere(V, false, SKY_MAP_STEPS, haze);
    float4 clouds = sky_clouds(V);
    if (clouds.a > 0.0) clouds.rgb = min(clouds.rgb / clouds.a, 65504.0) * clouds.a;
    return sky * (1.0 - clouds.a) + clouds.rgb + haze * clouds.a;
}
// The sky a camera ray that met nothing shows: the discs, the clouds, the atmosphere's light in front of the clouds over
// them (HDRP's fog on its clouds, of the atmosphere's part: what lies between the camera and the layer).
float3 sky_for_pixel(float3 V) {
    float3 haze;
    const float3 sky = sky_atmosphere(V, true, SKY_PIXEL_STEPS, haze);
    const float4 clouds = sky_clouds(V);
    return sky * (1.0 - clouds.a) + clouds.rgb + haze * clouds.a;
}
// The sky map the rays read, straight (the measuring switch draws the misses from it): without the Sky Intensity row.
float3 sky_map_direct(float3 dir) {
    Texture2D<float4> sky = ResourceDescriptorHeap[g.inputs1.w];
    const float u = atan2(dir.x, dir.z) * (0.5 / PI) + 0.5;
    const float v = acos(clamp(dir.y, -1.0, 1.0)) / PI;
    return sky.SampleLevel(s_linearWrap, float2(u, v), 0).rgb;
}
// What a camera ray that met nothing shows when the tracer draws it (sky_draws_misses), in HDRP's units, not exposed.
// (SKY_FLAG_SHOW_MAP, for tests: the map the rays read, seen the same way.)
float3 sky_camera_miss(float3 dir) { return sky_own() && (sky_flags() & SKY_FLAG_SHOW_MAP) == 0u ? sky_for_pixel(dir) : sky_map_direct(dir); }
#endif
