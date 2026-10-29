// SPDX-License-Identifier: MIT
#include "scene.hpp"
#include "texture_share.hpp"
#include "lamp_tree.hpp" // the lamps' search tree (update: after the frame's lights)
#include "light_grid.hpp" // the light grid's cells (build_light_grid)
#include <algorithm>
#include <chrono>
#include <cmath>
#include <numeric>
#include <cstring>
#include <future> // the light grid's build beside the record (record)
#include <intrin.h> // __popcnt64 (count_omm)
#include <list>
#include <unordered_set>

namespace tgpt {

uint32_t GpuScene::program_missing(uint32_t material, uint32_t& kind) const {
    kind=TGPT_PROGRAM_LIT;
    if(material>=gpuMaterials_.size()) return UINT32_MAX;
    const auto& m=gpuMaterials_[material]; kind=m.program;
    if(!kind) return 0;
    const size_t first=(size_t(m.programRow)+1)*4;
    if(first+4>gpuProgramRows_.size()) return UINT32_MAX;
    const auto* head=gpuProgramRows_.data()+first;
    if(head[0]!=kind || head[1]>32 || first+4+head[1]>gpuProgramRows_.size()) return UINT32_MAX;
    uint32_t open=0;
    for(uint32_t i=0;i<head[1];++i) if(int32_t(head[4+i])>=0) open|=1u<<i;
    return head[3]&~open;
}
namespace {
#if TGPT_MEGA_GEOMETRY
// CPU recording time, separate from the scene's GPU timestamps. No shader or public ABI changes.
struct MegaCpuTimer {
    double& value; std::chrono::steady_clock::time_point start=std::chrono::steady_clock::now();
    explicit MegaCpuTimer(double& v) : value(v) {}
    ~MegaCpuTimer() { value+=std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now()-start).count(); }
};
struct MegaCpuRecord {
    double* values; uint32_t trial; std::chrono::steady_clock::time_point start=std::chrono::steady_clock::now();
    ~MegaCpuRecord() {
        values[4]=std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now()-start).count();
        static double sum[8]{}; static uint64_t frames=0, since=0; static uint32_t previous=~0u;
        const uint64_t now=GetTickCount64();
        if(previous!=trial) { std::fill(std::begin(sum), std::end(sum), 0.0); frames=0; since=now; previous=trial; }
        ++frames; for(size_t i=0;i<8;++i) sum[i]+=values[i];
        if(now-since<1000) return;
        char b[640]; const double n=double(frames);
        std::snprintf(b, sizeof b, "Mega Geometry CPU trial %u: %llu frames; scene record %.3f ms, plant plan %.3f ms, plant pose/build %.3f ms "
            "(clusters %.3f ms), top level %.3f ms; cluster arguments rewritten %.1f%%; %.1f bottom levels in %.1f calls/frame, %.3f us CPU/bottom level",
            trial, (unsigned long long)frames, sum[4]/n, sum[0]/n, sum[1]/n, sum[2]/n, sum[3]/n,
            100.0*sum[5]/n, sum[6]/n, sum[7]/n, sum[6] ? 1000.0*sum[2]/sum[6] : 0.0);
        log(b); std::fill(std::begin(sum), std::end(sum), 0.0); frames=0; since=now;
    }
};
#endif
// PT-99: where the CPU's time goes while a frame's scene is recorded, by section, in the log every 10 s ("PT record CPU").
// The GPU waits for all of it: a frame is this time, then the trace (2026-10-12: 5 ms a frame in the tavern, 11 to 21 in
// the town, the whole gap between a frame and the tracer's GPU time). A clock read a section: nothing a frame can feel.
struct RecordCpu {
    static constexpr int kMax=56;
    const char* names[kMax]{}; double sums[kMax]{}, worst=0.0, frameMs=0.0; int count=0, current=-1; uint64_t frames=0, since=0;
    std::chrono::steady_clock::time_point last;
    void close(std::chrono::steady_clock::time_point now) {
        if(current<0) return;
        const double ms=std::chrono::duration<double, std::milli>(now-last).count();
        sums[current]+=ms; frameMs+=ms;
    }
    int find(const char* name) {
        for(int at=0;at<count;++at) if(names[at]==name) return at; // the same literal each frame: its address is its name
        for(int at=0;at<count;++at) if(std::strcmp(names[at], name)==0) return at; // (by its text where the linker kept two of it)
        if(count==kMax) return -1;
        names[count++]=name;
        return count-1;
    }
    void section(const char* name) {
        const auto now=std::chrono::steady_clock::now();
        close(now);
        current=find(name); last=now;
    }
    // A stretch inside the running section told under a name of its own (a group of plants made anew, among the plan's
    // cells): its time is taken out of the section's, so the sum stays the frame's.
    struct Carve {
        RecordCpu& cpu; const char* name; std::chrono::steady_clock::time_point start=std::chrono::steady_clock::now();
        ~Carve() {
            const double ms=std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now()-start).count();
            const int at=cpu.find(name);
            if(at<0) return;
            cpu.sums[at]+=ms;
            if(cpu.current>=0) cpu.sums[cpu.current]-=ms;
        }
    };
    void end() {
        close(std::chrono::steady_clock::now());
        current=-1; worst=(std::max)(worst, frameMs); frameMs=0.0; ++frames;
        const uint64_t tick=GetTickCount64();
        if(!since) since=tick;
        if(tick-since<10000) return;
        double all=0.0;
        for(int i=0;i<count;++i) all+=sums[i];
        std::string text="PT record CPU 10s: "+std::to_string(frames)+" frames; the scene's record a frame, ms: ";
        char b[160];
        for(int i=0;i<count;++i) { std::snprintf(b, sizeof b, "%s%s %.3f", i ? ", " : "", names[i], sums[i]/double(frames)); text+=b; }
        std::snprintf(b, sizeof b, "; sum %.3f, the longest frame %.3f", all/double(frames), worst);
        log(text+b);
        std::fill(std::begin(sums), std::end(sums), 0.0); worst=0.0; frames=0; since=tick;
    }
} record_cpu;
struct RecordCpuFrame { ~RecordCpuFrame() { record_cpu.end(); } };
const char* const kInstancesLoop="the instances' loop";
// PT-98: these are counted "per frame" of 60 a second and go by the clock (GpuScene::by_clock, set_clock): a frame that
// took 125 ms takes in 7.5 times as much, so the traced world arrives as soon at 8 frames a second as at 60 (the owner,
// 2026-10-09, of a town filling in: "it took a LONG time before it kicked in"; the tracer ran at 5 to 10 frames a second,
// and 48 builds a frame were 384 a second where 60 frames make 2,880).
constexpr uint64_t kReadBudget=64ull<<20;         // bytes of mesh data copied to staging per frame
// A budget that is memory in flight grows with the clock up to this many frames' worth and no further: the read-backs of
// up to three frames are in memory at once (copied, then mapped two frames or kReadMapMs later: 256 MB a frame at most),
// and a frame's new meshes go through one upload buffer (12 million triangles at most, about 440 MB).
constexpr double kMemoryClockMost=4.0;
// A read-back is mapped two frames after its copy, or one frame after it once this long has passed (two frames of 60 a
// second): at 8 frames a second the two frames were a quarter of a second for every mesh.
constexpr double kReadMapMs=33.0;
constexpr uint32_t kBuildsPerFrame=48;            // BLAS builds per frame
constexpr uint64_t kTrianglesPerFrame=3000000;    // and their triangles
constexpr uint64_t kChunk=16ull<<20;             // arena chunk size
constexpr uint64_t kOmmTrianglesPerFrame=262144;  // alpha-tested triangles given micromaps per frame
// A texture's alpha bounds for the micromap bake (pt_omm_bake.hlsl OMM_BOUNDS_*): its finest cells are 4 x 4 texels, the two
// steps that make them, and how many texels of the game's textures a frame's bake reads for them (four 4096 x 4096 ones).
constexpr uint32_t kOmmBoundsLevel=2, kOmmBoundsFirst=0xFFFFFFFEu, kOmmBoundsNext=0xFFFFFFFDu;
constexpr uint64_t kOmmBoundsTexelsPerFrame=1ull<<26;
constexpr uint64_t kOmmBudget=1024ull<<20;        // micromap input bytes over the whole scene (256 MB until round 20's finer leaves)
// A character's BLAS is refitted to its posed vertices every frame and rebuilt from them every this many frames, each on
// its own frame of the cycle: a refit costs a fraction of a build, and over a few frames of animation its boxes grow
// little. Rebuilt every frame, 64 characters of 8192 triangles took 0.84 ms of GPU time a frame (tgpt_gpu_test).
constexpr uint32_t kCharacterRebuild=4;

float half_to_float(uint16_t h) {
    const uint32_t sign=(h>>15)&1, exponent=(h>>10)&31, mantissa=h&1023;
    float value;
    if(exponent==0) value=std::ldexp(float(mantissa), -24);
    else if(exponent==31) value=mantissa ? 0.f : 65504.f;
    else value=std::ldexp(float(mantissa|1024), int(exponent)-25);
    return sign ? -value : value;
}
uint32_t format_bytes(uint32_t format) {
    switch(format) { case TGPT_VF_FLOAT32: return 4; case TGPT_VF_FLOAT16: case TGPT_VF_UNORM16: case TGPT_VF_SNORM16: return 2; default: return 1; }
}
// One vertex attribute of up to four components; missing ones read as 0 (w as 1).
void read_attribute(const uint8_t* p, uint32_t format, uint32_t dimension, float out[4]) {
    out[0]=out[1]=out[2]=0.f; out[3]=1.f;
    for(uint32_t i=0;i<dimension && i<4;++i) {
        switch(format) {
        case TGPT_VF_FLOAT32: { float f; std::memcpy(&f, p+i*4, 4); out[i]=f; break; }
        case TGPT_VF_FLOAT16: { uint16_t h; std::memcpy(&h, p+i*2, 2); out[i]=half_to_float(h); break; }
        case TGPT_VF_UNORM16: { uint16_t v; std::memcpy(&v, p+i*2, 2); out[i]=float(v)/65535.f; break; }
        case TGPT_VF_SNORM16: { int16_t v; std::memcpy(&v, p+i*2, 2); out[i]=std::max(-1.f, float(v)/32767.f); break; }
        case TGPT_VF_UNORM8: out[i]=float(p[i])/255.f; break;
        case TGPT_VF_SNORM8: out[i]=std::max(-1.f, float(int8_t(p[i]))/127.f); break;
        default: break;
        }
    }
}
float finite_or_zero(float f) { return std::isfinite(f) ? f : 0.f; }

// Which submeshes' surfaces close a volume (GeometryRecordGpu::flags, kGeometryClosed): every edge of each of its
// triangles is shared with exactly one other triangle of the mesh, which runs along it the other way (the twin may be
// another submesh's: a bottle's glass closed by its cork). Glass has an inside only there (pt_dielectric.hlsli): a ray
// that enters it meets its far side and leaves through it. An open sheet of glass (a pane of one quad, a shard, a
// cornea's shell, a cup drawn as one skin, a box without a bottom) has none, and is a thin pane to every ray. Taken
// for a solid whatever its mesh (until 2026-10-08), what lay behind an open sheet was seen from inside glass that the
// ray never left: bent once and dimmed to 1 / index^2 of its light (0.44 at 1.5; an eye under its cornea 0.53), and
// the offline test's clear box without a bottom cast 0.42 of a solid one's shadow.
// Vertices at the same place are one (a mesh repeats them along its texture seams and hard edges). A character's
// positions come every frame (none here: all zero): its vertices are told apart by their indices alone, so one with
// repeated vertices counts as open.
// And piece by piece (the flags' upper bits, kGeometrySolidShift): a submesh is often a closed body with a few open
// cards of the same material beside it (a candle's wax and its wick's card: 480 of 506 triangles of the game's
// Mesh_Candle_Big in the closed body, the card's 26 with 4 open edges), and one open edge must not make the whole body
// a sheet. A piece: the triangles joined through shared edges (over the whole mesh). It is solid where every edge of
// every one of its triangles has its one twin. Each submesh's triangles are put in that order here, the solid pieces'
// first (their order among themselves kept), and the flags hold how many those are: a triangle is of a solid piece
// where its index in the submesh is below that number (pt_common piece_solid). Light walks inside a material drawn
// from both sides only there (PT-40: subsurface_hit). A closed submesh's are all its triangles.
void mark_closed(MeshData& d) {
    for(auto& g:d.geometries) g.flags&=~(kGeometryClosed|kGeometrySolidBits);
    const size_t triangles=d.indices.size()/3, vertices=d.vertexCount;
    if(!triangles || !vertices) return;
    std::vector<uint32_t> one(vertices);
    bool placed=d.positions.size()>=vertices*3;
    if(placed) { placed=false; for(float v:d.positions) if(v!=0.f) { placed=true; break; } }
    if(placed) {
        // The same place: within a hundred-thousandth of the mesh's size (a seam's two copies of a vertex are the same
        // numbers in a modelled mesh; one computed twice, a cylinder's first and last column, differs in its last bits).
        float lo[3]={1e30f, 1e30f, 1e30f}, hi[3]={-1e30f, -1e30f, -1e30f};
        for(size_t v=0;v<vertices;++v) for(int i=0;i<3;++i) { const float p=finite_or_zero(d.positions[v*3+i]); lo[i]=std::min(lo[i], p); hi[i]=std::max(hi[i], p); }
        const double cell=std::max(1e-12, 1e-5*double(std::max(hi[0]-lo[0], std::max(hi[1]-lo[1], hi[2]-lo[2]))));
        size_t slots=16; while(slots<vertices*2) slots<<=1;
        std::vector<uint32_t> table(slots, kInvalid);
        std::vector<int64_t> at(vertices*3);
        for(size_t v=0;v<vertices;++v) for(int i=0;i<3;++i) at[v*3+i]=std::llround((double(finite_or_zero(d.positions[v*3+i]))-lo[i])/cell);
        for(uint32_t v=0;v<vertices;++v) {
            const int64_t* a=&at[size_t(v)*3];
            uint64_t h=uint64_t(a[0])*0x9E3779B97F4A7C15ull ^ uint64_t(a[1])*0xC2B2AE3D27D4EB4Full ^ uint64_t(a[2])*0x165667B19E3779F9ull; h^=h>>29;
            for(size_t slot=size_t(h)&(slots-1);;slot=(slot+1)&(slots-1)) {
                const uint32_t other=table[slot];
                if(other==kInvalid) { table[slot]=v; one[v]=v; break; }
                const int64_t* b=&at[size_t(other)*3];
                if(a[0]==b[0] && a[1]==b[1] && a[2]==b[2]) { one[v]=other; break; }
            }
        }
    } else for(uint32_t v=0;v<vertices;++v) one[v]=v;
    // Each edge by its two vertices, the lower first: how many triangles run along it that way, and the other way.
    // triangle: the first triangle met on the edge, which the others on it are joined to (the pieces).
    struct Edge { uint64_t key; uint32_t along, against, triangle; };
    size_t slots=16; while(slots<triangles*4) slots<<=1; // at most three edges a triangle
    std::vector<Edge> edges(slots, Edge{~0ull, 0, 0, kInvalid});
    std::vector<uint32_t> piece(triangles);
    for(size_t t=0;t<triangles;++t) piece[t]=uint32_t(t);
    auto piece_of=[&](uint32_t t) { while(piece[t]!=t) { piece[t]=piece[piece[t]]; t=piece[t]; } return t; };
    auto edge=[&](uint32_t a, uint32_t b) -> Edge& {
        const uint64_t key=a<b ? (uint64_t(a)<<32)|b : (uint64_t(b)<<32)|a;
        uint64_t h=key*0x9E3779B97F4A7C15ull; h^=h>>31;
        for(size_t slot=size_t(h)&(slots-1);;slot=(slot+1)&(slots-1)) {
            if(edges[slot].key==key) return edges[slot];
            if(edges[slot].key==~0ull) { edges[slot].key=key; return edges[slot]; }
        }
    };
    auto corners=[&](size_t t, uint32_t* c) {
        for(int i=0;i<3;++i) { const uint32_t index=d.indices[t*3+i]; c[i]=index<vertices ? one[index] : 0u; }
        return c[0]!=c[1] && c[1]!=c[2] && c[2]!=c[0];
    };
    for(size_t t=0;t<triangles;++t) {
        uint32_t c[3]; if(!corners(t, c)) continue;
        for(int i=0;i<3;++i) {
            const uint32_t a=c[i], b=c[(i+1)%3]; Edge& e=edge(a, b); ++(a<b ? e.along : e.against);
            if(e.triangle==kInvalid) e.triangle=uint32_t(t);
            else { const uint32_t mine=piece_of(uint32_t(t)), other=piece_of(e.triangle); if(mine!=other) piece[mine]=other; }
        }
    }
    // A piece is open where an edge of one of its triangles lacks its one twin; a triangle with two corners at one place
    // is of no piece.
    std::vector<uint8_t> open(triangles, 0);
    for(size_t t=0;t<triangles;++t) {
        uint32_t c[3]; if(!corners(t, c)) { open[t]=1; continue; }
        for(int i=0;i<3;++i) { const Edge& e=edge(c[i], c[(i+1)%3]); if(!(e.along==1 && e.against==1)) { open[piece_of(uint32_t(t))]=1; break; } }
    }
    std::vector<uint32_t> ordered;
    for(auto& g:d.geometries) {
        bool closed=true, any=false;
        const size_t first=g.indexStart/3, last=std::min(triangles, size_t(g.indexStart+g.indexCount)/3);
        for(size_t t=first;t<last && closed;++t) {
            uint32_t c[3]; if(!corners(t, c)) continue;
            any=true;
            for(int i=0;i<3 && closed;++i) { const Edge& e=edge(c[i], c[(i+1)%3]); closed=e.along==1 && e.against==1; }
        }
        if(closed && any) g.flags|=kGeometryClosed;
        if(last<=first) continue;
        size_t solid=last-first;
        if(!(closed && any)) {
            // The solid pieces' triangles first. (Nothing has read the triangles' order yet: clusters, micromaps and the
            // BLAS are made after this.)
            solid=0;
            for(size_t t=first;t<last;++t) if(!open[piece_of(uint32_t(t))]) ++solid;
            if(solid>0 && solid<last-first) {
                ordered.clear(); ordered.reserve((last-first)*3);
                for(int pass=0;pass<2;++pass)
                    for(size_t t=first;t<last;++t)
                        if((open[piece_of(uint32_t(t))]!=0)==(pass==1)) ordered.insert(ordered.end(), d.indices.begin()+t*3, d.indices.begin()+t*3+3);
                // (open and piece are by the triangles' old places: this submesh's are not asked again.)
                std::copy(ordered.begin(), ordered.end(), d.indices.begin()+first*3);
            }
        }
        g.flags|=uint32_t(std::min<size_t>(solid, kGeometrySolidMax))<<kGeometrySolidShift;
    }
}

// Each submesh's triangles in clusters of kClusterTriangles consecutive ones: their bounds and area in the mesh's space.
void compute_clusters(MeshData& d) {
    d.clusters.clear(); d.geometryClusters.assign(d.geometries.size()+1, 0);
    const size_t vertices=d.positions.size()/3;
    for(size_t g=0;g<d.geometries.size();++g) {
        d.geometryClusters[g]=uint32_t(d.clusters.size());
        const GeometryRecordGpu& geo=d.geometries[g];
        const uint32_t triangles=geo.indexCount/3;
        for(uint32_t first=0;first<triangles;first+=kClusterTriangles) {
            TriangleCluster c{{3e38f, 3e38f, 3e38f}, {-3e38f, -3e38f, -3e38f}, 0.f, {0.f, 0.f, 0.f}};
            for(uint32_t t=first;t<std::min(first+kClusterTriangles, triangles);++t) {
                float p[3][3];
                for(int k=0;k<3;++k) {
                    const size_t at=size_t(geo.indexStart)+size_t(t)*3+size_t(k);
                    const uint32_t i=at<d.indices.size() ? d.indices[at] : 0u;
                    for(int a=0;a<3;++a) { p[k][a]=i<vertices ? d.positions[size_t(i)*3+size_t(a)] : 0.f; c.lo[a]=std::min(c.lo[a], p[k][a]); c.hi[a]=std::max(c.hi[a], p[k][a]); }
                }
                const float e1[3]={p[1][0]-p[0][0], p[1][1]-p[0][1], p[1][2]-p[0][2]}, e2[3]={p[2][0]-p[0][0], p[2][1]-p[0][1], p[2][2]-p[0][2]};
                const float n[3]={e1[1]*e2[2]-e1[2]*e2[1], e1[2]*e2[0]-e1[0]*e2[2], e1[0]*e2[1]-e1[1]*e2[0]};
                const float length2=n[0]*n[0]+n[1]*n[1]+n[2]*n[2];
                if(!(length2>0.f) || !std::isfinite(length2)) continue;
                const float area=0.5f*std::sqrt(length2);
                c.area+=area;
                for(int a=0;a<3;++a) c.spread[a]+=area*n[a]*n[a]/length2;
            }
            d.clusters.push_back(c);
        }
    }
    d.geometryClusters[d.geometries.size()]=uint32_t(d.clusters.size());
}
// A cluster's area in the world under an instance's transform, from the squared lengths of its axis planes' images
// (planeScale2: |y' x z'|^2, |z' x x'|^2, |x' x y'|^2 for the images x', y', z' of the mesh's axes). A triangle of normal n
// and area a has the area a |n.x y'xz' + n.y z'xx' + n.z x'xy'| there, which without shear is a sqrt(sum n_i^2 |plane_i|^2);
// summed over the cluster, at most sqrt(area * sum spread_i |plane_i|^2) (Cauchy-Schwarz), and exactly that for a flat
// cluster under any scale and for any cluster under a uniform one.
float world_area(const TriangleCluster& c, const float planeScale2[3]) {
    const double s=double(c.spread[0])*planeScale2[0]+double(c.spread[1])*planeScale2[1]+double(c.spread[2])*planeScale2[2];
    return float(std::sqrt(std::max(0.0, double(c.area)*s)));
}
uint64_t mix64(uint64_t h, uint64_t v) { for(int b=0;b<8;++b) { h^=(v>>(b*8))&0xFF; h*=1099511628211ull; } return h; }
float luminance(const float c[3]) { return 0.2126f*c[0]+0.7152f*c[1]+0.0722f*c[2]; }
// Emissive clusters as lights: sampled where they can still give kEmitterReachLux (at least kEmitterMinReach from their
// triangles, at most as far as lights reach), beyond that left to the paths that meet them; a cluster that gives less
// than kEmitterMinIntensity along its normal is left to them altogether.
constexpr float kEmitterReachLux=2.f, kEmitterMinReach=0.35f, kEmitterMaxReach=40.f, kEmitterMinIntensity=0.25f;
// A build's slots may spread over this many before they are handed out again from the start.
constexpr uint32_t kEmitterSlots=2*kEmitterBudget;

// Suballocations can return to the pool only after the frame that last used them finishes.
// Empty chunks go back to the driver; smaller chunks avoid up to 512 MB of tail waste.
struct Arena {
    struct Chunk : Buffer { std::map<uint64_t,uint64_t> free; uint32_t live=0; uint64_t emptyAt=0; };
    std::list<Chunk> chunks;
    D3D12_RESOURCE_STATES initial=D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_FLAGS flags=D3D12_RESOURCE_FLAG_NONE;
    struct Range { Chunk* chunk=nullptr; uint64_t offset=0, size=0; D3D12_GPU_VIRTUAL_ADDRESS va() const { return chunk->va()+offset; } };
    std::map<uint64_t,Range> allocated;
    std::vector<std::pair<uint64_t,uint64_t>> retired;
    // What its live pieces take of its chunks (bytes() less this is free room inside chunks that are held whole).
    uint64_t used=0;
    // An empty chunk stays this many collects (one a frame) before it goes back to the driver: 0, at once. For an arena
    // whose pieces all go a few frames after they are made (buildArena), so that it does not make and free a chunk a frame.
    uint64_t keepEmpty=0, tick=0;
    Range allocate(ID3D12Device* device, uint64_t size, uint64_t alignment, const char* what) {
        size=(size+alignment-1)/alignment*alignment;
        auto take=[&](Chunk& c)->Range {
            for(auto it=c.free.begin();it!=c.free.end();++it) {
                const uint64_t lo=it->first, hi=lo+it->second, at=(lo+alignment-1)/alignment*alignment;
                if(at+size>hi) continue;
                c.free.erase(it); if(at>lo) c.free[lo]=at-lo; if(at+size<hi) c.free[at+size]=hi-at-size;
                Range range{&c,at,size}; ++c.live; used+=size; allocated[range.va()]=range; return range;
            }
            return {};
        };
        for(auto& c:chunks) { auto range=take(c); if(range.chunk) return range; }
        chunks.emplace_back(); auto& c=chunks.back();
        static_cast<Buffer&>(c)=make_buffer(device, std::max(kChunk,size), D3D12_HEAP_TYPE_DEFAULT, flags, initial, what);
        c.free[0]=c.size; return take(c);
    }
    bool owns(uint64_t va) const { return va && allocated.count(va)!=0; }
    void retire(uint64_t va, uint64_t fence) { if(va) retired.emplace_back(fence,va); }
    void collect(uint64_t completed) {
        ++tick;
        size_t kept=0;
        for(const auto& entry:retired) {
            if(entry.first>completed) { retired[kept++]=entry; continue; }
            auto found=allocated.find(entry.second); if(found==allocated.end()) continue;
            auto range=found->second; allocated.erase(found); auto& c=*range.chunk;
            uint64_t lo=range.offset, size=range.size;
            used-=std::min(used, range.size);
            auto next=c.free.lower_bound(lo);
            if(next!=c.free.end() && lo+size==next->first) { size+=next->second; c.free.erase(next); }
            next=c.free.lower_bound(lo);
            if(next!=c.free.begin()) { auto prev=std::prev(next); if(prev->first+prev->second==lo) { lo=prev->first; size+=prev->second; c.free.erase(prev); } }
            c.free[lo]=size;
            if(--c.live==0) {
                if(keepEmpty) c.emptyAt=tick;
                else for(auto it=chunks.begin();it!=chunks.end();++it) if(&*it==&c) { chunks.erase(it); break; }
            }
        }
        retired.resize(kept);
        // (Its last piece went when the GPU was done with it, keepEmpty frames ago: nothing reads the chunk any more.)
        if(keepEmpty) for(auto it=chunks.begin();it!=chunks.end();) { if(it->live==0 && tick-it->emptyAt>=keepEmpty) it=chunks.erase(it); else ++it; }
    }
    uint64_t bytes() const { uint64_t n=0; for(const auto& c:chunks) n+=c.size; return n; }
    void release_all(DeferredRelease& release, uint64_t fence) {
        for(auto& c:chunks) release.add(fence, c.resource.Get());
        chunks.clear(); allocated.clear(); retired.clear(); used=0;
    }
};
// PT-87 (docs/agents/memory-2.md): a still mesh's structure is built at its full size and its compacted copy (38 % of it
// on an RTX 5080) placed elsewhere a few frames later. In ONE arena the full-size holes were taken by later builds, and
// once the world had arrived its chunks stood whole around a few compacted pieces each: 113 MB of free room in a town.
// So the full-size builds have chunks of their own (buildArena), which empty as their structures are compacted and go
// back to the driver; what stays (the compacted ones, the characters', the micromaps') is packed together in
// accelerationArena. The same structures, built and compacted as before: only where they lie differs.
Arena geometryArena, accelerationArena, buildArena;
bool arenasReady=false;
constexpr uint64_t kBuildChunkKeep=120; // frames an emptied chunk of full-size builds stays for the next arrivals
void ensure_arenas() {
    if(arenasReady) return;
    geometryArena.initial=D3D12_RESOURCE_STATE_COMMON; geometryArena.flags=D3D12_RESOURCE_FLAG_NONE;
    accelerationArena.initial=D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE; accelerationArena.flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    buildArena.initial=accelerationArena.initial; buildArena.flags=accelerationArena.flags; buildArena.keepEmpty=kBuildChunkKeep;
    arenasReady=true;
}
// An acceleration structure's piece given back, whichever of the two arenas holds it.
void retire_structure(uint64_t va, uint64_t fence) { (buildArena.owns(va) ? buildArena : accelerationArena).retire(va, fence); }
// The test's switch (scene.hpp memory2_off), as the frame being recorded found it (GpuScene::record), and whether it is set.
uint32_t cutsOff=0;
bool cutsTest=false;
// Another switch of the offline test's alone, TGPT_TEST_SCENE_ORDER (tgpt_gpu_test's TGPT_TEST_PLANT_SHIFT_ONLY and
// TGPT_TEST_DECAL_MOVE_ONLY: PT-125's and PT-134's checks, docs/agents/fixt.md), as the frame being recorded found it.
// With it the scene SAYS, in its log and only when it changes, where each group of plants' records lie in this frame's
// buffers (pose_plants) and which address each mesh decal's top level was built with beside its mesh's own now (record):
// the two kept numbers that went stale, read on the CPU side. It changes nothing that is recorded or traced.
bool orderTest=false;

// Scratch for the acceleration structures one stage builds in a frame (the characters', the plant groups', the swaying
// cloths', a frame's new meshes'): each build has a piece of the stage's buffer to itself, so the GPU works on them at
// once, and one barrier after the last ends the stage. Built one after another over the same piece instead (2026-10-08,
// a barrier after each so that the next may use it), each build waited for the one before: 59 to 82 characters took 9 to
// 14 ms of a frame's scene stage, and the GPU test's 128 took 9.9 ms where side by side they take 0.3 to 0.5.
// The buffer holds what the stage builds in one frame, up to kScratchWindow (or its largest build, if that is more). A
// stage that needs more builds in waves: when the buffer is full, one barrier, and the next build starts at its front.
// A refit needs far less than a build, and each build is asked for what it needs this frame (scene.hpp skinUpdateBytes).
constexpr uint64_t kScratchWindow=32ull<<20;
uint64_t scratch_aligned(uint64_t bytes) { return (bytes+255)/256*256; }
D3D12_GPU_VIRTUAL_ADDRESS scratch_place(ID3D12GraphicsCommandList4* list, const Buffer& scratch, uint64_t& offset, uint64_t bytes) {
    offset=scratch_aligned(offset);
    if(offset && offset+bytes>scratch.size) {
        D3D12_RESOURCE_BARRIER barrier{}; barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV; // every build so far, done
        list->ResourceBarrier(1, &barrier);
        offset=0;
    }
    const D3D12_GPU_VIRTUAL_ADDRESS at=scratch.va()+offset;
    offset+=bytes;
    return at;
}
} // namespace

// The stage's buffer made to hold this frame's builds (total: their scratch together, each rounded as scratch_place
// rounds it; largest: the largest one's), a quarter more when it has to be made so that the next frames' fit too.
void GpuScene::fit_scratch(ID3D12Device5* device, DeferredRelease& release, uint64_t retireValue, Buffer& scratch, uint64_t& peak,
                           uint64_t total, uint64_t largest, const char* whose) {
    const uint64_t wanted=std::max<uint64_t>(std::max(largest, std::min(total, kScratchWindow)), 65536);
    peak=std::max(peak, wanted);
    if(scratch && scratch.size>=wanted) return;
    if(scratch) release.add(retireValue, scratch.resource.Get());
    const std::string label=std::string("Create acceleration structure scratch for ")+whose;
    scratch=make_buffer(device, scratch_aligned(wanted+wanted/4), D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                        D3D12_RESOURCE_STATE_UNORDERED_ACCESS, label.c_str());
    char text[256];
    std::snprintf(text, sizeof(text), "Acceleration structure scratch for %s: %.1f MB (this frame's builds need %.1f MB side by side, the largest %.2f MB%s)",
                  whose, double(scratch.size)/1048576.0, double(total)/1048576.0, double(largest)/1048576.0,
                  total>wanted ? "; built in waves, a barrier between them" : "");
    log(text);
}

GpuScene::GpuScene() { thread_=std::thread([this]{ worker(); }); }
GpuScene::~GpuScene() {
    { std::lock_guard<std::mutex> lock(workMutex_); stopping_=true; }
    workSignal_.notify_all();
    if(thread_.joinable()) thread_.detach(); // the process is ending; the worker holds no GPU objects
}

void GpuScene::worker() {
    for(;;) {
        Mesh* mesh=nullptr;
        {
            std::unique_lock<std::mutex> lock(workMutex_);
            workSignal_.wait(lock, [this]{ return stopping_ || !work_.empty(); });
            if(stopping_) return;
            mesh=work_.front(); work_.pop_front();
            converting_=mesh;
        }
        try { convert(*mesh); }
        catch(const std::exception& e) { log_throttled(std::string("Mesh conversion failed: ")+e.what()); mesh->state=Mesh::State::Failed; }
        catch(...) { mesh->state=Mesh::State::Failed; }
        {
            std::lock_guard<std::mutex> lock(workMutex_);
            done_.push_back(mesh);
            converting_=nullptr;
        }
        idleSignal_.notify_all();
    }
}

// A normal packed as pt_common.hlsli's pack_normal does (octahedral, 16 bits a coordinate).
uint32_t pack_normal(float x, float y, float z) {
    const float s=std::fabs(x)+std::fabs(y)+std::fabs(z);
    if(!(s>1e-20f) || !std::isfinite(s)) { x=0.f; y=1.f; z=0.f; }
    else { x/=s; y/=s; z/=s; }
    float ex=x, ey=y;
    if(z<0.f) { ex=(1.f-std::fabs(y))*(x>=0.f ? 1.f : -1.f); ey=(1.f-std::fabs(x))*(y>=0.f ? 1.f : -1.f); }
    auto q=[](float e) { return uint32_t(std::lround(std::clamp(e*0.5f+0.5f, 0.f, 1.f)*65535.f)); };
    return q(ex)|(q(ey)<<16);
}

// Read-back bytes to compact float3 positions, float2 UVs, RGBA8 colors and 32-bit indices with the
// submeshes' base vertices applied. Submeshes that aren't triangle lists keep their geometry slot as one
// degenerate triangle, so a ray's geometry index still names the submesh (and its material).
void GpuScene::convert(Mesh& mesh) {
    const MeshSource& s=*mesh.source;
    MeshData& d=mesh.data;
    if(s.cpu) {
        d.positions=s.positions; d.uvs=s.uvs; d.colors=s.colors; d.indices=s.indices; d.vertexCount=s.vertexCount;
        for(const auto& sub:s.submeshes) d.geometries.push_back(GeometryRecordGpu{sub.indexStart, sub.indexCount, 0, 0});
        if(d.geometries.empty()) d.geometries.push_back(GeometryRecordGpu{0, uint32_t(s.indices.size()), 0, 0});
        mark_closed(d); // before anything reads the triangles' order: it puts each submesh's solid pieces first
        if(!mesh.skinned) compute_clusters(d); // a character's positions come every frame: never an emitter
        mesh.state=Mesh::State::Converted; return;
    }
    auto attribute_fits=[&](uint32_t stream, uint32_t offset, uint32_t format, uint32_t dimension, uint32_t vertices) {
        if(stream>=4) return false;
        const auto& bytes=mesh.bytes[stream];
        const uint64_t need=uint64_t(vertices-1)*s.strides[stream]+offset+uint64_t(format_bytes(format))*dimension;
        return !bytes.empty() && s.strides[stream]>0 && need<=bytes.size();
    };
    const uint32_t vertices=s.vertexCount;
    if(!attribute_fits(s.positionStream, s.positionOffset, s.positionFormat, s.positionDimension, vertices))
        throw std::runtime_error("the vertex buffer is smaller than its positions ("+std::to_string(mesh.bytes[s.positionStream].size())+" bytes)");
    d.vertexCount=vertices;
    d.positions.resize(size_t(vertices)*3);
    const uint8_t* pb=mesh.bytes[s.positionStream].data();
    for(uint32_t v=0;v<vertices;++v) {
        float a[4]; read_attribute(pb+size_t(v)*s.strides[s.positionStream]+s.positionOffset, s.positionFormat, s.positionDimension, a);
        d.positions[size_t(v)*3]=finite_or_zero(a[0]); d.positions[size_t(v)*3+1]=finite_or_zero(a[1]); d.positions[size_t(v)*3+2]=finite_or_zero(a[2]);
    }
    if(attribute_fits(s.uvStream, s.uvOffset, s.uvFormat, s.uvDimension, vertices)) {
        d.uvs.resize(size_t(vertices)*2);
        const uint8_t* ub=mesh.bytes[s.uvStream].data();
        for(uint32_t v=0;v<vertices;++v) {
            float a[4]; read_attribute(ub+size_t(v)*s.strides[s.uvStream]+s.uvOffset, s.uvFormat, s.uvDimension, a);
            d.uvs[size_t(v)*2]=finite_or_zero(a[0]); d.uvs[size_t(v)*2+1]=finite_or_zero(a[1]);
        }
    }
    if(attribute_fits(s.uv2Stream, s.uv2Offset, s.uv2Format, s.uv2Dimension, vertices)) {
        d.uvs2.resize(size_t(vertices)*2);
        const uint8_t* ub=mesh.bytes[s.uv2Stream].data();
        for(uint32_t v=0;v<vertices;++v) {
            float a[4]; read_attribute(ub+size_t(v)*s.strides[s.uv2Stream]+s.uv2Offset, s.uv2Format, s.uv2Dimension, a);
            d.uvs2[size_t(v)*2]=finite_or_zero(a[0]); d.uvs2[size_t(v)*2+1]=finite_or_zero(a[1]);
        }
    }
    if(attribute_fits(s.normalStream, s.normalOffset, s.normalFormat, s.normalDimension, vertices)) {
        d.normals.resize(vertices);
        const uint8_t* nb=mesh.bytes[s.normalStream].data();
        for(uint32_t v=0;v<vertices;++v) {
            float a[4]; read_attribute(nb+size_t(v)*s.strides[s.normalStream]+s.normalOffset, s.normalFormat, s.normalDimension, a);
            d.normals[v]=pack_normal(finite_or_zero(a[0]), finite_or_zero(a[1]), finite_or_zero(a[2]));
        }
    }
    if(attribute_fits(s.tangentStream, s.tangentOffset, s.tangentFormat, s.tangentDimension, vertices)) {
        d.tangents.resize(size_t(vertices)*4);
        for(uint32_t v=0;v<vertices;++v) {
            float a[4]; read_attribute(mesh.bytes[s.tangentStream].data()+size_t(v)*s.strides[s.tangentStream]+s.tangentOffset, s.tangentFormat, 4, a);
            for(int k=0;k<4;++k) d.tangents[size_t(v)*4+k]=finite_or_zero(a[k]);
        }
    }
    if(attribute_fits(s.colorStream, s.colorOffset, s.colorFormat, s.colorDimension, vertices)) {
        d.colors.resize(vertices); if(s.decalAttributes) d.decalColors.resize(size_t(vertices)*4);
        const uint8_t* cb=mesh.bytes[s.colorStream].data();
        for(uint32_t v=0;v<vertices;++v) {
            float a[4]; read_attribute(cb+size_t(v)*s.strides[s.colorStream]+s.colorOffset, s.colorFormat, s.colorDimension, a);
            if(s.decalAttributes) for(int k=0;k<4;++k) d.decalColors[size_t(v)*4+k]=finite_or_zero(a[k]);
            uint32_t packed=0;
            for(int i=0;i<4;++i) packed|=uint32_t(std::lround(std::clamp(a[i], 0.f, 1.f)*255.f))<<(8*i);
            d.colors[v]=packed;
        }
    }
    const auto& ib=mesh.bytes[4];
    const uint32_t indexSize=s.indexFormat==32 ? 4 : 2;
    const uint64_t indexCount=ib.size()/indexSize;
    for(const auto& sub:s.submeshes) {
        GeometryRecordGpu g{uint32_t(d.indices.size()), 0, 0, 0};
        if(sub.topology==0 && uint64_t(sub.indexStart)+sub.indexCount<=indexCount && sub.indexCount>=3) {
            const uint32_t count=sub.indexCount-sub.indexCount%3;
            for(uint32_t i=0;i<count;++i) {
                uint32_t index;
                if(indexSize==4) std::memcpy(&index, ib.data()+(size_t(sub.indexStart)+i)*4, 4);
                else { uint16_t index16; std::memcpy(&index16, ib.data()+(size_t(sub.indexStart)+i)*2, 2); index=index16; }
                index+=sub.baseVertex;
                d.indices.push_back(index<vertices ? index : 0u);
            }
            g.indexCount=count;
        } else {
            d.indices.insert(d.indices.end(), {0u, 0u, 0u});
            g.indexCount=3;
        }
        d.geometries.push_back(g);
    }
    mark_closed(d); // before anything reads the triangles' order
    compute_clusters(d);
    for(auto& b:mesh.bytes) { b.clear(); b.shrink_to_fit(); }
    mesh.state=Mesh::State::Converted;
}

bool GpuScene::intake(DescriptorHeap* heap, DeferredRelease& release, uint64_t retireValue) {
    auto& e=engine();
    std::vector<std::pair<int32_t, std::unique_ptr<MeshSource>>> added;
    bool cleared=false;
    {
        std::lock_guard<std::mutex> lock(e.mutex);
        if(e.clearGeneration!=clearGeneration_) {
            clearGeneration_=e.clearGeneration; cleared=true;
            clear(heap,release,retireValue);
            log("The plugin cleared the scene; the traced scene starts over");
        }
        added.swap(e.newMeshes);
        retiredMeshes_.insert(retiredMeshes_.end(),e.retiredMeshes.begin(),e.retiredMeshes.end()); e.retiredMeshes.clear();
        if(e.materialsDirty) {
            materials_=e.materials; e.materialsDirty=false; materialsDirty_=true;
            ++materialsEpoch_; // (PT-99: the plants' kept list is made from the materials: plan_plants)
            subsurfaceMaterials_=0; for(const auto& m:materials_) if(m.flags&TGPT_MATERIAL_SUBSURFACE) ++subsurfaceMaterials_;
        }
        if(e.programsDirty) { programRows_=e.programRows; e.programsDirty=false; programsDirty_=true; }
        if(e.decalsDirty) { decals_=e.decals; e.decalsDirty=false; decalsDirty_=true; }
        if(e.lightsDirty) { lights_=e.lights; e.lightsDirty=false; }
    }
    if(!added.empty()) ++meshEpoch_; // (PT-99: a handle's mesh is another object from here on: the plants' kept list, plan_plants)
    for(auto& entry:added) {
        const int32_t handle=entry.first;
        if(handle<=0) continue;
        if(meshes_.size()<size_t(handle)) meshes_.resize(size_t(handle));
        auto mesh=std::make_unique<Mesh>();
        mesh->skinned=entry.second && entry.second->skinned;
        mesh->handle=handle; mesh->source=std::move(entry.second);
        pending_.push_back(mesh.get());
        meshes_[size_t(handle)-1]=std::move(mesh);
    }
    return cleared;
}

void GpuScene::set_lists(std::shared_ptr<const SceneList> list, std::shared_ptr<const MovingList> moving) {
    if(list && list!=sceneList_) {
        // PT-99: whether it is ANOTHER list, byte for byte (the plugin makes a new list object at every scan of the scene,
        // every 0.25 to 2 s, also when nothing in it changed). listEpoch_ is counted on only then: what is kept between
        // frames from the list (the plants' list, plan_plants) is kept across a scan that changed nothing.
        const bool same=sceneList_ && list->instances.size()==sceneList_->instances.size() && list->materialIndices.size()==sceneList_->materialIndices.size() &&
            (list->instances.empty() || std::memcmp(list->instances.data(), sceneList_->instances.data(), list->instances.size()*sizeof(TGPT_Instance))==0) &&
            (list->materialIndices.empty() || std::memcmp(list->materialIndices.data(), sceneList_->materialIndices.data(), list->materialIndices.size()*sizeof(uint32_t))==0);
        if(!same) ++listEpoch_;
        sceneList_=list; instances_=list->instances; materialIndices_=list->materialIndices; indicesDirty_=true;
        // HDRP's water in it: those meshes keep their triangles on the CPU, for the plan of the water's surface (scene_water.cpp).
        sceneHasWater_=false;
        for(const auto& inst:instances_) {
            if(!(inst.flags&TGPT_INSTANCE_WATER) || (inst.flags&TGPT_INSTANCE_HIDDEN)) continue;
            sceneHasWater_=true;
            if(inst.mesh>0 && size_t(inst.mesh)<=meshes_.size() && meshes_[size_t(inst.mesh)-1]) meshes_[size_t(inst.mesh)-1]->water=true;
        }
    }
    moving_=std::move(moving);
}

void GpuScene::pump_d3d11(ID3D11Device* device, ID3D11DeviceContext* context, uint64_t frame) {
    readBytesThisFrame_=0;
    const uint64_t readBudget=uint64_t(double(kReadBudget)*std::min(clock_, kMemoryClockMost)); // PT-98: by the clock
    const auto now=std::chrono::steady_clock::now();
    std::vector<Mesh*> toConvert;
    for(Mesh* mesh:pending_) {
        if(mesh->state==Mesh::State::Queued) {
            if(mesh->source->cpu) { mesh->state=Mesh::State::Converting; toConvert.push_back(mesh); continue; }
            if(readBytesThisFrame_>=readBudget) continue;
            try {
                auto stage=[&](ID3D11Buffer* source, ComPtr<ID3D11Buffer>& staging) {
                    if(!source) return;
                    D3D11_BUFFER_DESC d{}; source->GetDesc(&d);
                    D3D11_BUFFER_DESC s{}; s.ByteWidth=d.ByteWidth; s.Usage=D3D11_USAGE_STAGING; s.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
                    s.MiscFlags=d.MiscFlags&D3D11_RESOURCE_MISC_BUFFER_STRUCTURED; s.StructureByteStride=s.MiscFlags ? d.StructureByteStride : 0;
                    HRESULT hr=device->CreateBuffer(&s, nullptr, &staging);
                    if(FAILED(hr)) { s.MiscFlags=0; s.StructureByteStride=0; hr=device->CreateBuffer(&s, nullptr, &staging); }
                    check(hr, "Create mesh read-back buffer");
                    context->CopyResource(staging.Get(), source);
                    readBytesThisFrame_+=d.ByteWidth;
                };
                const auto& src=*mesh->source;
                std::array<bool, 4> used{};
                used[src.positionStream]=true;
                if(src.uvStream<4) used[src.uvStream]=true;
                if(src.colorStream<4) used[src.colorStream]=true;
                if(src.tangentStream<4) used[src.tangentStream]=true;
                if(src.normalStream<4) used[src.normalStream]=true;
                if(src.uv2Stream<4) used[src.uv2Stream]=true;
                for(size_t i=0;i<4;++i) if(used[i]) stage(src.vertexBuffers[i].Get(), mesh->staging[i]);
                stage(src.indexBuffer.Get(), mesh->staging[4]);
                mesh->copiedFrame=frame; mesh->copiedAt=now; mesh->state=Mesh::State::Copied;
            } catch(const std::exception& e) {
                log_throttled(std::string("Mesh read-back could not start: ")+e.what());
                mesh->state=Mesh::State::Failed;
                std::lock_guard<std::mutex> lock(workMutex_); done_.push_back(mesh);
            }
        } else if(mesh->state==Mesh::State::Copied && (frame>=mesh->copiedFrame+2 ||
                  (frame>mesh->copiedFrame && std::chrono::duration<double, std::milli>(now-mesh->copiedAt).count()>=kReadMapMs))) {
            bool all=true, failed=false;
            for(size_t i=0;i<5;++i) {
                auto& staging=mesh->staging[i];
                if(!staging) continue;
                D3D11_MAPPED_SUBRESOURCE mapped{};
                const HRESULT hr=context->Map(staging.Get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
                if(hr==DXGI_ERROR_WAS_STILL_DRAWING) { all=false; continue; }
                if(FAILED(hr)) { log_throttled("Mesh read-back map failed: "+tgb::hexhr(hr)); failed=true; break; }
                D3D11_BUFFER_DESC d{}; staging->GetDesc(&d);
                mesh->bytes[i].assign(static_cast<const uint8_t*>(mapped.pData), static_cast<const uint8_t*>(mapped.pData)+d.ByteWidth);
                context->Unmap(staging.Get(), 0);
                staging.Reset();
            }
            if(failed) {
                mesh->state=Mesh::State::Failed;
                std::lock_guard<std::mutex> lock(workMutex_); done_.push_back(mesh);
            } else if(all) {
                for(auto& b:mesh->source->vertexBuffers) b.Reset();
                mesh->source->indexBuffer.Reset();
                mesh->state=Mesh::State::Converting;
                toConvert.push_back(mesh);
            }
        }
    }
    if(!toConvert.empty()) {
        { std::lock_guard<std::mutex> lock(workMutex_); for(auto* m:toConvert) work_.push_back(m); }
        workSignal_.notify_all();
    }
}

// Allocates every mesh's ranges first, so the arena chunks (new ones included) can all go to COPY_DEST
// before any copy is recorded, and back to shader reads before the builds.
void GpuScene::upload_meshes(ID3D12Device5* device, ID3D12GraphicsCommandList4* list, DescriptorHeap& heap, DeferredRelease& release,
                             uint64_t retireValue, const std::vector<Mesh*>& meshes, Barriers& barriers) {
    struct Copy { const void* data; uint64_t bytes; Arena::Range range; };
    std::vector<Copy> copies;
    uint64_t total=0;
    for(Mesh* m:meshes) {
        auto& d=m->data;
        auto place=[&](const void* data, uint64_t bytes, uint32_t& srv) -> D3D12_GPU_VIRTUAL_ADDRESS {
            if(!bytes) { srv=kInvalid; return 0; }
            auto range=geometryArena.allocate(device, bytes, 16, "Create geometry arena");
            m->geometryAllocations.push_back(range.va());
            copies.push_back(Copy{data, bytes, range});
            total+=bytes;
            const uint32_t i=heap.allocate();
            D3D12_SHADER_RESOURCE_VIEW_DESC v{}; v.Format=DXGI_FORMAT_R32_TYPELESS; v.ViewDimension=D3D12_SRV_DIMENSION_BUFFER;
            v.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; v.Buffer.FirstElement=range.offset/4; v.Buffer.NumElements=UINT(bytes/4);
            v.Buffer.Flags=D3D12_BUFFER_SRV_FLAG_RAW;
            device->CreateShaderResourceView(range.chunk->resource.Get(), &v, heap.cpu(i));
            srv=i;
            return range.va();
        };
        if(m->skinned) {
            // Written every frame by the skin pass before the rebuild; nothing to upload.
            const uint64_t bytes=std::max<uint64_t>(d.positions.size()*4, 16);
            m->skinPositions=make_buffer(device, bytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                                         D3D12_RESOURCE_STATE_COMMON, "Create character positions");
            m->positionsSrv=raw_srv(device, heap, m->skinPositions.resource.Get(), bytes);
            m->skinUav=raw_uav(device, heap, m->skinPositions.resource.Get(), bytes);
            m->skinPrevious=make_buffer(device, bytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                                        D3D12_RESOURCE_STATE_COMMON, "Create character positions of the frame before");
            m->skinPreviousSrv=raw_srv(device, heap, m->skinPrevious.resource.Get(), bytes);
            m->skinPreviousUav=raw_uav(device, heap, m->skinPrevious.resource.Get(), bytes);
            // ON TRIAL (PT-92): its smooth normals, 4 bytes a vertex, written with the positions when the skinning brings them.
            const uint64_t normalBytes=std::max<uint64_t>(uint64_t(d.vertexCount)*4, 16);
            m->skinNormals=make_buffer(device, normalBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                                       D3D12_RESOURCE_STATE_COMMON, "Create character normals");
            m->skinNormalsSrv=raw_srv(device, heap, m->skinNormals.resource.Get(), normalBytes);
            m->skinNormalsUav=raw_uav(device, heap, m->skinNormals.resource.Get(), normalBytes);
            m->positionsVa=m->skinPositions.va();
        } else m->positionsVa=place(d.positions.data(), d.positions.size()*4, m->positionsSrv);
        place(d.uvs.data(), d.uvs.size()*4, m->uvsSrv);
        place(d.colors.data(), d.colors.size()*4, m->colorsSrv);
        place(d.normals.data(), d.normals.size()*4, m->normalsSrv);
        place(d.uvs2.data(), d.uvs2.size()*4, m->uvs2Srv);
        place(d.tangents.data(), d.tangents.size()*4, m->tangentsSrv);
        place(d.decalColors.data(), d.decalColors.size()*4, m->decalColorsSrv);
        m->indicesVa=place(d.indices.data(), d.indices.size()*4, m->indicesSrv);
        m->bytesOnGpu=(d.positions.size()+d.uvs.size()+d.uvs2.size()+d.colors.size()+d.normals.size()+d.indices.size()+d.tangents.size()+d.decalColors.size())*4;
    }
    if(copies.empty()) return;
    Buffer upload=make_buffer(device, total, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, "Create mesh upload buffer");
    for(auto& c:geometryArena.chunks) barriers.transition(c, D3D12_RESOURCE_STATE_COPY_DEST);
    barriers.flush(list);
    uint64_t at=0;
    for(const auto& c:copies) {
        std::memcpy(static_cast<uint8_t*>(upload.mapped)+at, c.data, c.bytes);
        list->CopyBufferRegion(c.range.chunk->resource.Get(), c.range.offset, upload.resource.Get(), at, c.bytes);
        at+=c.bytes;
    }
    for(auto& c:geometryArena.chunks) barriers.transition(c, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    barriers.flush(list);
    release.add(retireValue, upload.resource.Get());
}

// A mesh's positions and indices back from the geometry arena, for the plan of the water's surface (scene_water.cpp): the
// mesh was uploaded before any water instance named it (the CPU's copies go then), which a mesh shared with something
// else in the scene can be. Recorded once; read kWaterReadbackFrames frames later, when every frame in flight has run.
constexpr uint64_t kWaterReadbackFrames=8;
bool GpuScene::water_triangles(ID3D12Device5* device, ID3D12GraphicsCommandList4* list, Mesh& mesh, DeferredRelease& release, uint64_t retireValue, Barriers& barriers) {
    if(!mesh.data.positions.empty() && !mesh.data.indices.empty()) return true;
    if(mesh.skinned || !mesh.positionsVa || !mesh.indicesVa) return false;
    auto found=waterReadbacks_.find(mesh.handle);
    if(found==waterReadbacks_.end()) {
        uint64_t indices=0;
        for(const auto& g:mesh.data.geometries) indices=std::max<uint64_t>(indices, uint64_t(g.indexStart)+g.indexCount);
        WaterReadback r;
        r.positionBytes=uint64_t(mesh.data.vertexCount)*12; r.indexBytes=indices*4; r.frame=waterFrame_;
        if(!r.positionBytes || !r.indexBytes) return false;
        auto chunk_of=[](D3D12_GPU_VIRTUAL_ADDRESS va, uint64_t bytes, uint64_t& offset) -> Buffer* {
            for(auto& c:geometryArena.chunks) if(va>=c.va() && va+bytes<=c.va()+c.size) { offset=va-c.va(); return &c; }
            return nullptr;
        };
        uint64_t positionsAt=0, indicesAt=0;
        Buffer* positions=chunk_of(mesh.positionsVa, r.positionBytes, positionsAt);
        Buffer* indexChunk=chunk_of(mesh.indicesVa, r.indexBytes, indicesAt);
        if(!positions || !indexChunk) return false;
        r.buffer=make_buffer(device, r.positionBytes+r.indexBytes, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST, "Create a water mesh's read-back");
        barriers.transition(*positions, D3D12_RESOURCE_STATE_COPY_SOURCE); barriers.transition(*indexChunk, D3D12_RESOURCE_STATE_COPY_SOURCE); barriers.flush(list);
        list->CopyBufferRegion(r.buffer.resource.Get(), 0, positions->resource.Get(), positionsAt, r.positionBytes);
        list->CopyBufferRegion(r.buffer.resource.Get(), r.positionBytes, indexChunk->resource.Get(), indicesAt, r.indexBytes);
        barriers.transition(*positions, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE); barriers.transition(*indexChunk, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE); barriers.flush(list);
        waterReadbacks_.emplace(mesh.handle, std::move(r));
        return false;
    }
    WaterReadback& r=found->second;
    if(waterFrame_-r.frame<kWaterReadbackFrames) return false;
    mesh.data.positions.resize(size_t(r.positionBytes/4)); mesh.data.indices.resize(size_t(r.indexBytes/4));
    std::memcpy(mesh.data.positions.data(), r.buffer.mapped, r.positionBytes);
    std::memcpy(mesh.data.indices.data(), static_cast<const uint8_t*>(r.buffer.mapped)+r.positionBytes, r.indexBytes);
    mesh.water=true;
    release.add(retireValue, r.buffer.resource.Get());
    waterReadbacks_.erase(found);
    return true;
}

D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS GpuScene::blas_inputs(const Mesh& mesh, std::vector<D3D12_RAYTRACING_GEOMETRY_DESC>& storage,
                                                                           D3D12_GPU_VIRTUAL_ADDRESS positions) const {
    const auto& d=mesh.data;
    storage.assign(d.geometries.size(), D3D12_RAYTRACING_GEOMETRY_DESC{});
    for(size_t i=0;i<storage.size();++i) {
        auto& g=storage[i];
        g.Type=D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
        g.Flags=D3D12_RAYTRACING_GEOMETRY_FLAG_NONE; // instances decide: forced opaque unless alpha-tested
        // (PT-110, M3: but a character's structure is its own, and marks its submeshes that are no cut-out solid itself:
        // Mesh::skinOpaque, which record sets before it builds or refits.)
        if(mesh.skinned && !positions && i<mesh.skinOpaque.size() && mesh.skinOpaque[i]) g.Flags=D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
        g.Triangles.VertexBuffer.StartAddress=positions ? positions : mesh.positionsVa; g.Triangles.VertexBuffer.StrideInBytes=12;
        g.Triangles.VertexFormat=DXGI_FORMAT_R32G32B32_FLOAT; g.Triangles.VertexCount=d.vertexCount;
        g.Triangles.IndexFormat=DXGI_FORMAT_R32_UINT; g.Triangles.IndexCount=d.geometries[i].indexCount;
        g.Triangles.IndexBuffer=mesh.indicesVa ? mesh.indicesVa+uint64_t(d.geometries[i].indexStart)*4 : 0;
    }
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS in{};
    in.Type=D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    // A character's is updated every frame: fast to build, and refitted in between (kCharacterRebuild).
    in.Flags=mesh.skinned ? D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD|D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE
                          : D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE|D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_COMPACTION;
    // A swaying cloth's copy: built once, then refitted every frame its pose moves (the wind bends it around where it hangs).
    if(positions) in.Flags=D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE|D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;
    in.NumDescs=UINT(storage.size()); in.DescsLayout=D3D12_ELEMENTS_LAYOUT_ARRAY; in.pGeometryDescs=storage.data();
    return in;
}

void GpuScene::build_blas(ID3D12Device5* device, ID3D12GraphicsCommandList4* list, Mesh& mesh, uint64_t& scratchOffset, uint64_t compactSize) {
    std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geometries;
    const auto in=blas_inputs(mesh, geometries);
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info{};
    device->GetRaytracingAccelerationStructurePrebuildInfo(&in, &info);
    if(!info.ResultDataMaxSizeInBytes) throw DxError(E_FAIL, "BLAS prebuild reported no size");
    if(!mesh.skinned && info.ScratchDataSizeInBytes>scratch_.size) throw DxError(E_FAIL, "BLAS scratch too small");
    // A still mesh's full-size build goes where it will soon be gone from (compact_meshes); a character's stays as built.
    Arena& arena=!mesh.skinned && compactSize && !(cutsOff&kMemory2BuildChunks) ? buildArena : accelerationArena;
    auto result=arena.allocate(device, info.ResultDataMaxSizeInBytes, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT, "Create acceleration structure arena");
    mesh.blasVa=result.va(); mesh.accelerationAllocations.push_back(result.va());
#if TGPT_MEGA_GEOMETRY
    mega_fresh(result.va()); // PT-109: a partitioned top level writes this address's instances again
#endif
    if(mesh.skinned) {
        // Built every frame from that frame's positions (none are there yet): only the storage now, and the scratch a build
        // needs and a refit needs.
        mesh.skinScratchBytes=std::max(info.ScratchDataSizeInBytes, info.UpdateScratchDataSizeInBytes);
        mesh.skinUpdateBytes=info.UpdateScratchDataSizeInBytes;
        mesh.sinceBuild=kInvalid;
    } else {
        // Beside the frame's other new meshes in the scratch (scratch_place): record ends them with one barrier.
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC desc{};
        desc.DestAccelerationStructureData=result.va(); desc.Inputs=in;
        desc.ScratchAccelerationStructureData=scratch_place(list, scratch_, scratchOffset, info.ScratchDataSizeInBytes);
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_DESC post{};
        post.DestBuffer=compactSize; post.InfoType=D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_COMPACTED_SIZE;
        list->BuildRaytracingAccelerationStructure(&desc,compactSize ? 1u : 0u,compactSize ? &post : nullptr);
    }
    mesh.blasBytes=result.size;
    std::lock_guard<std::mutex> lock(statsMutex_);
    stats_.accelerationBytes+=result.size;
}

void GpuScene::upload_structured(ID3D12Device5* device, ID3D12GraphicsCommandList4* list, DescriptorHeap& heap, UploadRing& ring,
                                 DeferredRelease& release, uint64_t retireValue, Structured& target, const void* data, uint32_t count, uint32_t stride, Barriers& barriers,
                                 ID3D12Resource* staged, uint64_t stagedAt) {
    const uint32_t needed=std::max<uint32_t>(count, 1);
    if(!target.buffer || target.capacity<needed || target.stride!=stride) {
        if(target.buffer) release.add(retireValue, target.buffer.resource.Get());
        heap.release(target.srv);
        uint32_t capacity=64; while(capacity<needed) capacity*=2;
        target.buffer=make_buffer(device, uint64_t(capacity)*stride, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COMMON, "Create scene record buffer");
        target.capacity=capacity; target.stride=stride;
        target.srv=structured_srv(device, heap, target.buffer.resource.Get(), capacity, stride);
    }
    if(!count) { barriers.transition(target.buffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE); return; }
    const uint64_t bytes=uint64_t(count)*stride;
    barriers.transition(target.buffer, D3D12_RESOURCE_STATE_COPY_DEST); barriers.flush(list);
    void* cpu=nullptr; D3D12_GPU_VIRTUAL_ADDRESS gpu=0; uint64_t offset=0;
    // (PT-99, `staged`: the same bytes already lie in an upload buffer of the caller's, at stagedAt: copied from there, and
    // `data` is not read. The light grid's, which its build leaves there: record.)
    if(staged) list->CopyBufferRegion(target.buffer.resource.Get(), 0, staged, stagedAt, bytes);
    else if(ring.allocate(bytes, 16, &cpu, &gpu, &offset)) {
        std::memcpy(cpu, data, bytes);
        list->CopyBufferRegion(target.buffer.resource.Get(), 0, ring.resource(), offset, bytes);
    } else {
        Buffer upload=make_buffer(device, bytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, "Create record upload buffer");
        std::memcpy(upload.mapped, data, bytes);
        list->CopyBufferRegion(target.buffer.resource.Get(), 0, upload.resource.Get(), 0, bytes);
        release.add(retireValue, upload.resource.Get());
    }
    barriers.transition(target.buffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
}

void GpuScene::set_wind(float time, const std::vector<TGPT_ClothWind>& winds) {
    windTime_=time;
    winds_.clear();
    for(const auto& w:winds) winds_[w.material]=w;
    // (PT-99: which materials have a cloth wind, by a table, for the cloth's plan, which asks it of every submesh of the
    // scene every frame. A wind of a material numbered a million or more stays in the map alone, and the plan then asks
    // the map for such numbers.)
    clothWindy_.clear(); clothWindPastTable_=false;
    for(const auto& w:winds) {
        if(w.material>=(1u<<20)) { clothWindPastTable_=true; continue; }
        if(clothWindy_.size()<=w.material) clothWindy_.resize(size_t(w.material)+1, 0);
        clothWindy_[w.material]=1;
    }
}

void GpuScene::release_sway_copy(SwayCopy& c, DescriptorHeap* heap, DeferredRelease& release, uint64_t retireValue) {
    if(heap) { heap->release(c.positionsSrv); heap->release(c.positionsUav); heap->release(c.previousSrv); }
    c.positionsSrv=c.positionsUav=c.previousSrv=kInvalid;
    if(c.positions) release.add(retireValue, c.positions.resource.Get());
    if(c.blas) release.add(retireValue, c.blas.resource.Get());
    c.positions=Buffer{}; c.blas=Buffer{};
}

// The game's Cloth_Wind moves a vertex by its object-space position, texture coordinates and color, the material's values, the
// shader time and the instance's world-to-object rotation (TGPT_ClothWind; pt_sway.hlsl): one record copies the still mesh,
// then one for each submesh the wind moves. A pose already made for this shader time (the game paused) is kept as it is.
void GpuScene::sway(ID3D12Device5* device, ID3D12GraphicsCommandList4* list, DescriptorHeap& heap, UploadRing& ring, DeferredRelease& release,
                    uint64_t retireValue, const std::vector<std::pair<SwayCopy*, size_t>>& work, Barriers& barriers) {
    std::vector<SwayRecordGpu> records;
    std::vector<std::pair<SwayCopy*, Mesh*>> moved;
    std::vector<SwayCopy*> touched; // every copy a dispatch writes: the moved, and those whose kept pose follows a move
    for(const auto& [c, source]:work) {
        // Its previous pose (modes 3 and 4, the camera rays' motion): the pose of the frame before is in its buffer when it
        // was posed or kept last frame. (A copy two instances share is looked at once.)
        if(c->posedFrame==swayFrame_) continue;
        const bool continuous=c->built && c->posedFrame+1==swayFrame_;
        c->posedFrame=swayFrame_;
        if(c->built && c->builtTime==windTime_) {
            // The pose kept (the game paused): no move this frame.
            if(c->moving) {
                SwayRecordGpu keep{}; keep.mode=4; keep.positionsOut=c->positionsUav; keep.previousAt=c->previousAt;
                keep.vertexCount=meshRecords_[size_t(c->mesh)].vertexCount;
                records.push_back(keep); touched.push_back(c); c->moving=false;
            }
            continue;
        }
        const auto& inst=instances_[source];
        Mesh* m=meshes_[size_t(inst.mesh)-1].get();
        const MeshRecordGpu& own=meshRecords_[size_t(inst.mesh)];
        // World to object: the inverse of the instance's 3x3.
        const double a[9]={inst.transform[0], inst.transform[1], inst.transform[2], inst.transform[4], inst.transform[5], inst.transform[6],
                           inst.transform[8], inst.transform[9], inst.transform[10]};
        // A flat transform has no inverse: the wind's push then moves nothing (the copy is still made: its instance uses its BLAS).
        const double det=a[0]*(a[4]*a[8]-a[5]*a[7])-a[1]*(a[3]*a[8]-a[5]*a[6])+a[2]*(a[3]*a[7]-a[4]*a[6]);
        const double rdet=std::fabs(det)>1e-12 ? 1.0/det : 0.0;
        const double inv[9]={(a[4]*a[8]-a[5]*a[7])*rdet, (a[2]*a[7]-a[1]*a[8])*rdet, (a[1]*a[5]-a[2]*a[4])*rdet,
                             (a[5]*a[6]-a[3]*a[8])*rdet, (a[0]*a[8]-a[2]*a[6])*rdet, (a[2]*a[3]-a[0]*a[5])*rdet,
                             (a[3]*a[7]-a[4]*a[6])*rdet, (a[1]*a[6]-a[0]*a[7])*rdet, (a[0]*a[4]-a[1]*a[3])*rdet};
        SwayRecordGpu base{};
        for(int r=0;r<3;++r) { for(int k=0;k<3;++k) base.toObject[r*4+k]=float(inv[r*3+k]); base.toObject[r*4+3]=0.f; }
        base.time=windTime_;
        base.positionsIn=m->positionsSrv; base.uvs=own.uvs; base.colors=own.colors; base.indices=own.indices;
        base.positionsOut=c->positionsUav; base.vertexCount=own.vertexCount; base.previousAt=c->previousAt;
        if(continuous) { SwayRecordGpu save=base; save.mode=3; records.push_back(save); }
        SwayRecordGpu still=base; still.mode=1;
        records.push_back(still);
        for(uint32_t g=0;g<own.geometryCount;++g) {
            const auto found=winds_.find(materialIndices_[inst.materialBase+g]);
            if(found==winds_.end() || own.geometryBase+g>=geometryRecords_.size()) continue;
            const TGPT_ClothWind& w=found->second;
            SwayRecordGpu r=base; r.mode=2;
            r.speed=w.speed; r.wind=w.wind; r.noiseScale=w.noiseScale;
            for(int k=0;k<3;++k) r.intensity[k]=w.intensity[k];
            r.flags=w.flags; r.direction[0]=w.direction[0]; r.direction[1]=w.direction[1];
            r.indexStart=geometryRecords_[own.geometryBase+g].indexStart; r.indexCount=geometryRecords_[own.geometryBase+g].indexCount;
            records.push_back(r);
        }
        if(!continuous) { SwayRecordGpu same=base; same.mode=4; records.push_back(same); } // (no pose before it: no move)
        c->moving=continuous;
        moved.emplace_back(c, m); touched.push_back(c);
    }
    if(records.empty()) return;
    upload_structured(device, list, heap, ring, release, retireValue, swayRecordBuffer_, records.data(), uint32_t(records.size()), sizeof(SwayRecordGpu), barriers);
    for(SwayCopy* c:touched) barriers.transition(c->positions, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    barriers.flush(list);
    list->SetPipelineState(swayPass);
    // The previous pose kept first (3), the still copies (1), then the moved vertices over them (2), then the previous pose of
    // a copy with none before and of a kept pose (4).
    bool ran=false;
    for(const uint32_t mode:{3u, 1u, 2u, 4u}) {
        bool any=false;
        for(const auto& r:records) any|=r.mode==mode;
        if(!any) continue;
        if(ran) { barriers.uav(nullptr); barriers.flush(list); }
        for(uint32_t i=0;i<uint32_t(records.size());++i) {
            if(records[i].mode!=mode) continue;
            const uint32_t values[8]={swayRecordBuffer_.srv, i, 0, 0, 0, 0, 0, 0};
            list->SetComputeRoot32BitConstants(1, 8, values, 0);
            const uint32_t n=mode==2 ? records[i].indexCount : records[i].vertexCount;
            list->Dispatch((n+63)/64, 1, 1);
        }
        ran=true;
    }
    for(SwayCopy* c:touched) barriers.transition(c->positions, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    barriers.flush(list);
    if(moved.empty()) return;
    // Each copy's BLAS: built the first time, refitted to the new pose after; side by side in the scratch (scratch_place).
    uint64_t scratchTotal=0, scratchLargest=0;
    for(auto& [c, m]:moved) {
        const uint64_t bytes=c->built ? c->updateBytes : c->scratchBytes;
        scratchTotal+=scratch_aligned(bytes); scratchLargest=std::max(scratchLargest, bytes);
    }
    fit_scratch(device, release, retireValue, swayScratch_, swayScratchPeak_, scratchTotal, scratchLargest, "swaying cloth");
    uint64_t offset=0;
    for(auto& [c, m]:moved) {
        std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geos;
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC desc{};
        desc.Inputs=blas_inputs(*m, geos, c->positions.va());
        desc.DestAccelerationStructureData=c->blas.va();
        desc.ScratchAccelerationStructureData=scratch_place(list, swayScratch_, offset, c->built ? c->updateBytes : c->scratchBytes);
        if(c->built) { desc.Inputs.Flags|=D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE; desc.SourceAccelerationStructureData=c->blas.va(); }
        list->BuildRaytracingAccelerationStructure(&desc, 0, nullptr);
        { auto& w=structureWork[kStructureCloth]; if(c->built) ++w.refits; else ++w.builds; w.triangles+=m->triangles; w.bytes+=c->blas.size; }
        c->built=true; c->builtTime=windTime_;
    }
    barriers.uav(nullptr); barriers.flush(list);
}

void GpuScene::set_plant_wind(const TGPT_PlantWindFrame& frame, const std::vector<TGPT_PlantWind>& winds, std::shared_ptr<const WindNoise> noise) {
    plantFrame_=frame;
    plantWinds_=winds;
    plantWindOf_.clear();
    for(size_t i=0;i<plantWinds_.size();++i) plantWindOf_[plantWinds_[i].material]=uint32_t(i);
    // (PT-99: the same answers by a table, for the pose's lookup a range a frame: a material's wind, kInvalid without one.
    // A wind of a material numbered a million or more stays in the map alone, and the lookup then asks the map.)
    plantWindTable_.clear(); plantWindPastTable_=false;
    for(size_t i=0;i<plantWinds_.size();++i) {
        const uint32_t material=plantWinds_[i].material;
        if(material>=(1u<<20)) { plantWindPastTable_=true; continue; }
        if(plantWindTable_.size()<=material) plantWindTable_.resize(size_t(material)+1, kInvalid);
        plantWindTable_[material]=uint32_t(i);
    }
    windNoise_=std::move(noise);
}

// A plant group's structure: refitted to the wind's pose every frame, and (PT-87) compacted once after its build where the
// group was made for that. The flags a build and every refit of it name (a refit must name its build's).
static D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS plant_flags(const PlantGroup& g) {
    return D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE|D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE|
        (g.compactable ? D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_COMPACTION : D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE);
}

// A plant group's submeshes as its structure's build and every refit name them (a refit must name what its build named):
// each as its material is, solid or a cut-out; and (PT-110's M1, on trial) a cut-out linked to its member mesh's
// micromaps where the group was made with them (PlantGroup::links): the mesh's micromap array, and the part of the
// mesh's index buffer that holds this submesh's triangles, in the member's own order, which is the group's.
// The descriptions point into `out`: it must live until the build is recorded, and must not be copied in between.
// (struct PlantGeometry: scene.hpp, since 2026-10-12 a group keeps the description its refits name: pose_plants.)
static void plant_geometry(const PlantGroup& g, PlantGeometry& out) {
    const size_t n=g.geometries.size();
    out.geos.assign(n, D3D12_RAYTRACING_GEOMETRY_DESC{});
    out.triangles.assign(n, D3D12_RAYTRACING_GEOMETRY_TRIANGLES_DESC{});
    out.links.assign(n, D3D12_RAYTRACING_GEOMETRY_OMM_LINKAGE_DESC{});
    for(size_t i=0;i<n;++i) {
        auto& t=out.triangles[i];
        t.VertexBuffer.StartAddress=g.data.va()+g.positionsAt; t.VertexBuffer.StrideInBytes=12;
        t.VertexFormat=DXGI_FORMAT_R32G32B32_FLOAT; t.VertexCount=g.vertexCount;
        t.IndexFormat=DXGI_FORMAT_R32_UINT; t.IndexCount=g.geometries[i].indexCount;
        t.IndexBuffer=g.data.va()+g.indicesAt+uint64_t(g.geometries[i].indexStart)*4;
        auto& d=out.geos[i];
        if(i>=g.links.size() || !g.links[i].array) {
            d.Type=D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
            d.Flags=g.opaque[i] ? D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE : D3D12_RAYTRACING_GEOMETRY_FLAG_NONE;
            d.Triangles=t;
            continue;
        }
        auto& l=out.links[i];
        l.OpacityMicromapIndexBuffer.StartAddress=g.links[i].indices; l.OpacityMicromapIndexBuffer.StrideInBytes=4;
        l.OpacityMicromapIndexFormat=DXGI_FORMAT_R32_UINT; l.OpacityMicromapBaseLocation=0;
        l.OpacityMicromapArray=g.links[i].array;
        d.Type=D3D12_RAYTRACING_GEOMETRY_TYPE_OMM_TRIANGLES; d.Flags=D3D12_RAYTRACING_GEOMETRY_FLAG_NONE;
        d.OmmTriangles.pTriangles=&t; d.OmmTriangles.pOmmLinkage=&l;
    }
}

// Leaves and grass (round 20): an alpha-tested translucent material, not thin cloth: its covered micromap pieces are opaque
// (pt_omm_bake), and its instance carries kMaskFoliage so that a light's ray meets it in a pass of its own.
static bool is_foliage(uint32_t flags) {
    return (flags&TGPT_MATERIAL_TRANSLUCENT)!=0 && (flags&TGPT_MATERIAL_ALPHA_TEST)!=0 && (flags&TGPT_MATERIAL_THIN_CLOTH)==0;
}
void GpuScene::release_plant_group(PlantGroup& g, DescriptorHeap* heap, DeferredRelease& release, uint64_t retireValue) {
    if(heap) for(uint32_t* view:{&g.dataUav, &g.positionsSrv, &g.uvsSrv, &g.colorsSrv, &g.normalsSrv, &g.indicesSrv, &g.previousSrv}) heap->release(*view);
    g.dataUav=g.positionsSrv=g.uvsSrv=g.colorsSrv=g.normalsSrv=g.indicesSrv=g.previousSrv=kInvalid;
    if(g.data) release.add(retireValue, g.data.resource.Get());
    if(g.blas) release.add(retireValue, g.blas.resource.Get());
    g.data=Buffer{}; g.blas=Buffer{};
#if TGPT_MEGA_GEOMETRY
    if(g.clas) release.add(retireValue, g.clas.resource.Get()); // (PT-109 on trial: its clusters' room)
    g.clas=Buffer{};
#endif
}

#if TGPT_MEGA_GEOMETRY
// PT-109 on trial (the hidden "Mega Geometry trial" 4 to 6; docs/agents/megageo-4.md): a plant is small by its height in the
// world (kMegaSmallPlantHeight): its mesh's box (the union of MeshData::clusters' boxes, made for every mesh and kept),
// turned and scaled by its instance (the box's height in the world: each side's length times its share of the world's up).
bool GpuScene::mega_small(const Mesh& mesh, const float transform[12]) const {
    // (Each mesh's box once, kept by its handle until it goes: the plan asks for every plant every frame.)
    auto found=megaBoxes_.find(mesh.handle);
    if(found==megaBoxes_.end()) {
        std::array<float, 6> box{3e38f, 3e38f, 3e38f, -3e38f, -3e38f, -3e38f};
        for(const auto& c:mesh.data.clusters) for(int a=0;a<3;++a) { box[a]=std::min(box[a], c.lo[a]); box[3+a]=std::max(box[3+a], c.hi[a]); }
        found=megaBoxes_.emplace(mesh.handle, box).first;
    }
    const auto& box=found->second;
    double height=0;
    for(int k=0;k<3;++k) { if(!(box[3+k]>=box[k])) return false; height+=std::fabs(double(transform[4+k]))*double(box[3+k]-box[k]); }
    return height<=kMegaSmallPlantHeight;
}
void GpuScene::mega_fail(const std::string& what) {
    if(megaClustersFailed_) return;
    megaClustersFailed_=true;
    log("Mega Geometry trial "+std::to_string(megaTrial)+": "+what+"; the plants' clusters are off for good (until the game starts again), their groups are made plain");
}
// A mesh cut into clusters (mega_cut) from its positions and indices (its own CPU copies, or read back), kept by its handle.
// One that cannot be cut is kept with no cluster: its plants' groups stay plain.
bool GpuScene::mega_cut_mesh(Mesh& mesh, const float* positions, const uint32_t* indices, size_t indexCount) {
    std::vector<MegaSubmesh> submeshes;
    for(const auto& g:mesh.data.geometries) submeshes.push_back(MegaSubmesh{g.indexStart, g.indexCount, g.flags>>kGeometrySolidShift});
    const auto began=std::chrono::steady_clock::now();
    MegaMeshClusters cut; std::string error;
    const bool ok=mega_cut(positions, mesh.data.vertexCount, indices, indexCount, submeshes, cut, error);
    megaCutSeconds_+=std::chrono::duration<double>(std::chrono::steady_clock::now()-began).count();
    if(!ok) { log_throttled("Mega Geometry trial: a mesh could not be cut into clusters ("+error+"); its plants' groups stay plain"); cut=MegaMeshClusters{}; }
    else { ++megaCutMeshes_; megaCutTriangles_+=cut.triangles; }
    megaMeshes_[mesh.handle]=std::move(cut);
    return ok;
}
// The meshes the plan wanted cut, as far as the frame's budget goes: cut from their CPU copies where they still have them,
// else read back from the geometry arena first (a few frames: the GPU must be past the copy), and their cut's order put on
// the GPU (the groups that wait for them are made of clusters from the next frame on). Before plan_plants.
void GpuScene::mega_prepare(ID3D12Device5* device, ID3D12GraphicsCommandList4* list, DescriptorHeap& heap, DeferredRelease& release, uint64_t retireValue, Barriers& barriers) {
    if(!mega_clusters_on()) { megaWanted_.clear(); return; }
    // (PT-98: by the clock; a mesh of the plants is a few thousand triangles, cut in well under a millisecond.)
    const uint64_t budget=by_clock(200000);
    uint64_t done=0;
    for(auto it=megaWanted_.begin();it!=megaWanted_.end();) {
        const int32_t handle=*it;
        Mesh* m=handle>0 && size_t(handle)<=meshes_.size() ? meshes_[size_t(handle)-1].get() : nullptr;
        if(!m || m->state!=Mesh::State::Ready || m->skinned || !m->positionsVa || !m->indicesVa) { it=megaWanted_.erase(it); continue; }
        auto found=megaMeshes_.find(handle);
        if(found==megaMeshes_.end()) {
            if(done>=budget) { ++it; continue; }
            if(!m->data.positions.empty() && !m->data.indices.empty()) mega_cut_mesh(*m, m->data.positions.data(), m->data.indices.data(), m->data.indices.size());
            else {
                auto r=megaReadbacks_.find(handle);
                if(r==megaReadbacks_.end()) {
                    uint64_t indices=0;
                    for(const auto& g:m->data.geometries) indices=std::max<uint64_t>(indices, uint64_t(g.indexStart)+g.indexCount);
                    MegaReadback rb; rb.positionBytes=uint64_t(m->data.vertexCount)*12; rb.indexBytes=indices*4; rb.fence=retireValue;
                    auto chunk_of=[](D3D12_GPU_VIRTUAL_ADDRESS va, uint64_t bytes, uint64_t& offset) -> Buffer* {
                        for(auto& c:geometryArena.chunks) if(va>=c.va() && va+bytes<=c.va()+c.size) { offset=va-c.va(); return &c; }
                        return nullptr;
                    };
                    uint64_t positionsAt=0, indicesAt=0;
                    Buffer* positions=rb.positionBytes ? chunk_of(m->positionsVa, rb.positionBytes, positionsAt) : nullptr;
                    Buffer* indexChunk=rb.indexBytes ? chunk_of(m->indicesVa, rb.indexBytes, indicesAt) : nullptr;
                    if(!positions || !indexChunk) { megaMeshes_[handle]=MegaMeshClusters{}; it=megaWanted_.erase(it); continue; } // (no cut: plain)
                    rb.buffer=make_buffer(device, rb.positionBytes+rb.indexBytes, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST, "Create a plant mesh's read-back");
                    barriers.transition(*positions, D3D12_RESOURCE_STATE_COPY_SOURCE); barriers.transition(*indexChunk, D3D12_RESOURCE_STATE_COPY_SOURCE); barriers.flush(list);
                    list->CopyBufferRegion(rb.buffer.resource.Get(), 0, positions->resource.Get(), positionsAt, rb.positionBytes);
                    list->CopyBufferRegion(rb.buffer.resource.Get(), rb.positionBytes, indexChunk->resource.Get(), indicesAt, rb.indexBytes);
                    barriers.transition(*positions, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE); barriers.transition(*indexChunk, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE); barriers.flush(list);
                    megaReadbacks_.emplace(handle, std::move(rb));
                    ++it; continue;
                }
                if(r->second.fence>completed_) { ++it; continue; }
                const auto* bytes=static_cast<const uint8_t*>(r->second.buffer.mapped);
                mega_cut_mesh(*m, reinterpret_cast<const float*>(bytes), reinterpret_cast<const uint32_t*>(bytes+r->second.positionBytes), size_t(r->second.indexBytes/4));
                release.add(retireValue, r->second.buffer.resource.Get());
                megaReadbacks_.erase(r);
            }
            done+=m->triangles;
            found=megaMeshes_.find(handle);
            if(found==megaMeshes_.end()) { ++it; continue; }
        }
        if(found->second.clusters.empty()) { it=megaWanted_.erase(it); continue; } // (it could not be cut: plain)
        if(!found->second.order) {
            std::string error;
            if(!mega_upload_order(device, list, heap, release, retireValue, found->second, barriers, error)) { mega_fail("a mesh's cut on the GPU: "+error); return; }
        }
        found->second.indices=std::vector<uint32_t>(); // (on the GPU now)
        it=megaWanted_.erase(it);
    }
}
#endif

// The plants by cell (PT-99; plan_plants says what it is made from and when it is made again): an instance is one of
// them when its mesh is ready, every submesh has a material and one of them a plant wind; its key is what its group's
// signature is made of; its cell its place's. The cells in the order a std::map of them would hold them, each cell's
// members in the order of (key, place in the scene's list), with the signature of those keys and their count and the
// members' vertices and indices together: all as plan_plants made them every frame until 2026-10-12 (plan_plant_list_reference
// keeps that way, as what this is held against). With less work where it is made: a table says which materials have a
// wind (a search a submesh before); an instance's key is kept while the scene's list and the materials are the ones it
// was hashed from and its mesh has as many submeshes (the key is the same FNV number: a group's members stand in their
// keys' order, so another hash would lay a group's vertices out another way); one sort in place of a map of vectors.
void GpuScene::plan_plant_list(PlantPlan& plan, bool split) {
    plan.cells.clear(); plan.members.clear(); plan.order.clear(); plan.candidates.clear();
    plan.windy.assign(materials_.size(), 0);
    for(const auto& [material, wind]:plantWindOf_) if(material<plan.windy.size()) plan.windy[material]=1;
    if(plan.off || plan.keyListEpoch!=listEpoch_ || plan.keyMaterialsEpoch!=materialsEpoch_ || plan.keys.size()!=instances_.size()) {
        plan.keys.assign(instances_.size(), 0); plan.keyGeometries.assign(instances_.size(), 0);
        plan.keyListEpoch=listEpoch_; plan.keyMaterialsEpoch=materialsEpoch_;
    }
    for(size_t source=0;source<instances_.size();++source) {
        const auto& inst=instances_[source];
        if(inst.flags&(TGPT_INSTANCE_HIDDEN|TGPT_INSTANCE_WATER|TGPT_INSTANCE_STAND_IN) || inst.mesh<=0 || size_t(inst.mesh)>meshes_.size()) continue;
        Mesh* m=meshes_[size_t(inst.mesh)-1].get();
        if(!m || m->state!=Mesh::State::Ready || m->skinned || meshRecords_.size()<=size_t(inst.mesh)) continue;
        const MeshRecordGpu& own=meshRecords_[size_t(inst.mesh)];
        if(!own.geometryCount || !own.vertexCount || uint64_t(inst.materialBase)+own.geometryCount>materialIndices_.size() ||
           uint64_t(own.geometryBase)+own.geometryCount>geometryRecords_.size()) continue;
        bool windy=false, ok=true;
        for(uint32_t gi=0;gi<own.geometryCount;++gi) {
            const uint32_t material=materialIndices_[inst.materialBase+gi];
            if(material>=materials_.size()) { ok=false; break; }
            windy|=plan.windy[material]!=0;
        }
        if(!ok || !windy) continue;
        for(float v:inst.transform) ok&=std::isfinite(v);
        if(!ok) continue;
        uint64_t key=plan.keys[source];
        if(plan.keyGeometries[source]!=own.geometryCount) {
            key=1469598103934665603ull;
            auto mix=[&key](const void* p, size_t n) { for(size_t b=0;b<n;++b) { key^=static_cast<const uint8_t*>(p)[b]; key*=1099511628211ull; } };
            mix(&inst.mesh, 4); mix(inst.transform, sizeof(inst.transform)); mix(&inst.flags, 4); mix(&inst.renderingLayerMask, 4);
            for(uint32_t gi=0;gi<own.geometryCount;++gi) {
                mix(&materialIndices_[inst.materialBase+gi], 4);
                // (M3: and whether that material is a cut-out or glass, which the group's structure marks its submesh by: a
                // group whose material's alpha test is switched is made anew.)
                const uint32_t cut=materials_[materialIndices_[inst.materialBase+gi]].flags&(TGPT_MATERIAL_ALPHA_TEST|TGPT_MATERIAL_GLASS);
                mix(&cut, 4);
            }
            plan.keys[source]=key; plan.keyGeometries[source]=own.geometryCount;
        }
        std::pair<int64_t, int64_t> cell{int64_t(std::floor(double(inst.transform[3])/kPlantCell)), int64_t(std::floor(double(inst.transform[11])/kPlantCell))};
#if TGPT_MEGA_GEOMETRY
        if(split && mega_small(*m, inst.transform)) cell.second+=kMegaSmallTag; // (PT-109 on trial: the cell's small plants, a group of their own)
#endif
        (void)split;
        plan.candidates.push_back(PlantPlan::Candidate{cell, key, uint32_t(source), uint32_t(plan.candidates.size())});
    }
    std::sort(plan.candidates.begin(), plan.candidates.end(), [](const PlantPlan::Candidate& a, const PlantPlan::Candidate& b) {
        return a.cell!=b.cell ? a.cell<b.cell : a.key!=b.key ? a.key<b.key : a.source<b.source; });
    plan.members.resize(plan.candidates.size()); plan.order.resize(plan.candidates.size());
    for(size_t i=0;i<plan.candidates.size();) {
        PlantPlan::Cell cell; cell.cell=plan.candidates[i].cell; cell.first=uint32_t(i);
        uint64_t signature=1469598103934665603ull;
        size_t end=i;
        for(;end<plan.candidates.size() && plan.candidates[end].cell==cell.cell;++end) {
            const PlantPlan::Candidate& c=plan.candidates[end];
            plan.members[end]=PlantPlan::Member{c.key, c.source, 0, false};
            plan.order[c.number]=uint32_t(end);
            signature^=c.key; signature*=1099511628211ull;
            const MeshRecordGpu& own=meshRecords_[size_t(instances_[c.source].mesh)];
            cell.vertices+=own.vertexCount;
            for(uint32_t gi=0;gi<own.geometryCount;++gi) cell.indices+=geometryRecords_[own.geometryBase+gi].indexCount;
        }
        cell.count=uint32_t(end-i);
        signature^=uint64_t(cell.count); signature*=1099511628211ull;
        cell.signature=signature;
        plan.cells.push_back(cell);
        i=end;
    }
}

// The plants by cell as plan_plants made them every frame until 2026-10-12, in its own lines (a search of the winds a
// submesh, the key hashed afresh, a std::map of vectors, each sorted, its signature, its vertices and indices), laid
// out as plan_plant_list lays them out. What the kept list is held against (plan_plants: with the environment's
// TGPT_TEST_RECORD_CHECK at every frame, without it at every 512th), and the list itself once a check found a difference.
void GpuScene::plan_plant_list_reference(PlantPlan& out, bool split) {
    struct Member { uint64_t key; uint32_t source; };
    std::map<std::pair<int64_t, int64_t>, std::vector<Member>> cells;
    std::vector<uint32_t> plants; // in the order of the scene's list
    for(size_t source=0;source<instances_.size();++source) {
        const auto& inst=instances_[source];
        if(inst.flags&(TGPT_INSTANCE_HIDDEN|TGPT_INSTANCE_WATER|TGPT_INSTANCE_STAND_IN) || inst.mesh<=0 || size_t(inst.mesh)>meshes_.size()) continue;
        Mesh* m=meshes_[size_t(inst.mesh)-1].get();
        if(!m || m->state!=Mesh::State::Ready || m->skinned || meshRecords_.size()<=size_t(inst.mesh)) continue;
        const MeshRecordGpu& own=meshRecords_[size_t(inst.mesh)];
        if(!own.geometryCount || !own.vertexCount || uint64_t(inst.materialBase)+own.geometryCount>materialIndices_.size() ||
           uint64_t(own.geometryBase)+own.geometryCount>geometryRecords_.size()) continue;
        bool windy=false, ok=true;
        for(uint32_t gi=0;gi<own.geometryCount;++gi) {
            const uint32_t material=materialIndices_[inst.materialBase+gi];
            if(material>=materials_.size()) { ok=false; break; }
            windy|=plantWindOf_.count(material)!=0;
        }
        for(float v:inst.transform) ok&=std::isfinite(v);
        if(!ok || !windy) continue;
        uint64_t key=1469598103934665603ull;
        auto mix=[&key](const void* p, size_t n) { for(size_t b=0;b<n;++b) { key^=static_cast<const uint8_t*>(p)[b]; key*=1099511628211ull; } };
        mix(&inst.mesh, 4); mix(inst.transform, sizeof(inst.transform)); mix(&inst.flags, 4); mix(&inst.renderingLayerMask, 4);
        for(uint32_t gi=0;gi<own.geometryCount;++gi) {
            mix(&materialIndices_[inst.materialBase+gi], 4);
            const uint32_t cut=materials_[materialIndices_[inst.materialBase+gi]].flags&(TGPT_MATERIAL_ALPHA_TEST|TGPT_MATERIAL_GLASS);
            mix(&cut, 4);
        }
        bool isSmall=false;
#if TGPT_MEGA_GEOMETRY
        isSmall=split && mega_small(*m, inst.transform);
#endif
        (void)split;
        std::pair<int64_t, int64_t> cell{int64_t(std::floor(double(inst.transform[3])/kPlantCell)), int64_t(std::floor(double(inst.transform[11])/kPlantCell))};
#if TGPT_MEGA_GEOMETRY
        if(isSmall) cell.second+=kMegaSmallTag; // (PT-109 on trial: the cell's small plants, a group of their own)
#endif
        (void)isSmall;
        cells[cell].push_back(Member{key, uint32_t(source)});
        plants.push_back(uint32_t(source));
    }
    out.cells.clear(); out.members.clear(); out.order.clear();
    std::vector<uint32_t> where(instances_.size(), 0u); // an instance's place among the members
    for(auto& [cell, list]:cells) {
        std::sort(list.begin(), list.end(), [](const Member& a, const Member& b) { return a.key!=b.key ? a.key<b.key : a.source<b.source; });
        uint64_t signature=1469598103934665603ull;
        for(const auto& member:list) { signature^=member.key; signature*=1099511628211ull; }
        signature^=list.size(); signature*=1099511628211ull;
        uint64_t vertices=0, indices=0;
        for(const auto& member:list) {
            const MeshRecordGpu& own=meshRecords_[size_t(instances_[member.source].mesh)];
            vertices+=own.vertexCount;
            for(uint32_t gi=0;gi<own.geometryCount;++gi) indices+=geometryRecords_[own.geometryBase+gi].indexCount;
        }
        PlantPlan::Cell made; made.cell=cell; made.first=uint32_t(out.members.size()); made.count=uint32_t(list.size());
        made.signature=signature; made.vertices=vertices; made.indices=indices;
        out.cells.push_back(made);
        for(const auto& member:list) { where[member.source]=uint32_t(out.members.size()); out.members.push_back(PlantPlan::Member{member.key, member.source, 0, false}); }
    }
    for(const uint32_t source:plants) out.order.push_back(where[source]);
}
// Whether two lists of the plants say the same of every cell and member (not what each frame sets: link, pending).
bool GpuScene::same_plant_list(const PlantPlan& a, const PlantPlan& b) {
    if(a.cells.size()!=b.cells.size() || a.members.size()!=b.members.size() || a.order!=b.order) return false;
    for(size_t i=0;i<a.cells.size();++i) {
        const auto& x=a.cells[i]; const auto& y=b.cells[i];
        if(x.cell!=y.cell || x.first!=y.first || x.count!=y.count || x.signature!=y.signature || x.vertices!=y.vertices || x.indices!=y.indices) return false;
    }
    for(size_t i=0;i<a.members.size();++i) if(a.members[i].key!=b.members[i].key || a.members[i].source!=b.members[i].source) return false;
    return true;
}

// Grass and leaves in the wind: which instances are posed this frame, and in which group. An instance is one of them when
// a material of its has a plant wind; its group is its cell's. A group whose members are the ones it was made of is used
// as it is; one whose members changed is made anew here (its buffers and records; pose_plants does the GPU's part), as
// far as the frame's budget goes: the rest wait, traced still, and are counted.
void GpuScene::plan_plants(ID3D12Device5* device, DescriptorHeap& heap, DeferredRelease& release, uint64_t retireValue,
                           std::vector<uint8_t>& grouped, std::vector<PlantUse>& uses) {
    uint32_t waiting=0;
    if(plantWinds_.empty() || !swayPass) {
        for(auto& [cell, g]:plantGroups_) release_plant_group(g, &heap, release, retireValue);
        plantGroups_.clear(); uploadedPlantGroups_.clear();
        std::lock_guard<std::mutex> s(statsMutex_); stats_.plantWaiting=0;
        return;
    }
    record_cpu.section("the plants' plan: the list of plants by cell");
    // The plants by cell, each with the key its group's signature is made of.
    // M1 (PT-110, docs/agents/omm-1.md; ON TRIAL: the plugin's hidden "Micromaps trial" 32, off until the game has
    // answered): a plant in the wind's pose traces with its mesh's micromaps. Until now no such plant had any (a town's
    // 6.4 million posed triangles, every cut-out one of them handed to the shader's alpha test by every ray that crossed
    // it): micromaps were "made for a still mesh". But a micromap hangs on a triangle's texture coordinates, which the
    // wind does not move, and DirectX Raytracing lets a structure with micromaps be refitted. So each plant asks for its
    // mesh's micromaps, as a still instance does (`link`: which build of them it can link, 0 none; `pending`: they have
    // been asked for and are on their way), and its group's structure links them (below, and plant_geometry).
    using Member=PlantPlan::Member;
    const bool plantMaps=(ommTrial&TGPT_OMM_TRIAL_NO_PLANT_MAPS)==0 && ommEnabled && ommAllowed; // (M1, the rule since round 20)
#if TGPT_MEGA_GEOMETRY
    // PT-109 on trial (docs/agents/megageo-4.md, megageo-5.md; the hidden "Mega Geometry trial"): 4, 5 and 6 put a cell's
    // small plants in a group of their own (kMegaSmallPlantHeight); 5 builds such a group from clusters once its members'
    // meshes are cut (mega_prepare), with their micromaps as the clusters' own micromap arrays (megageo-5); 6 is the split without the small
    // plants' micromaps. 7: every group (one a cell, as it ships) made of clusters with micromaps, each member its own
    // bottom level and instance (NVIDIA's layout), in the partitioned top level; 8: 7 without micromaps. Every other
    // number: the groups as they were.
    const bool megaSplit=megaTrial>=4 && megaTrial<=6;
    const bool megaNoMaps=megaTrial==6 || megaTrial==8;
    const bool megaClusters=mega_clusters_on(), megaMaps=mega_maps_on();
#endif
    // PT-99 (docs/agents/reccpu.md): the plants by cell are a LIST KEPT BETWEEN FRAMES (plan_plant_list: which instances
    // are plants in the wind, each one's key, its cell, each cell's members in their order, the signature of their keys,
    // their vertices and indices). Until 2026-10-12 it was made anew every frame: every instance of the scene asked, 60
    // bytes and more hashed a plant, a std::map of vectors filled and sorted (3.8 ms a frame standing in a forest). It is
    // made again whenever one of the things it is made from is no longer the one it was made from, and only then:
    //   the scene's list (listEpoch_: set_lists is the one place that writes instances_ and materialIndices_, and
    //   counts it on unless the new list is the old one byte for byte; clear, which empties them, drops this list),
    //   the materials (materialsEpoch_: intake is the one place that writes materials_),
    //   the meshes (meshEpoch_: counted on wherever a mesh becomes ready, fails, goes or is replaced, which are also
    //   the places that write meshRecords_ and geometryRecords_: intake, record's new meshes, retire_meshes, clear),
    //   the materials that have a wind this frame (set_plant_wind: every frame; compared here),
    //   and never kept at all while the Mega Geometry trial splits a cell's small plants off (their cell then depends
    //   on mega_small's boxes).
    // What depends on anything else is done every frame as before: a plant's turn at its mesh's micromaps, in the order
    // of the scene's list (below), and everything about the groups (the cells' loop).
    PlantPlan& plan=plantPlan_;
    bool split=false;
#if TGPT_MEGA_GEOMETRY
    split=megaSplit;
#endif
    plan.windNow.clear();
    for(const auto& w:plantWinds_) plan.windNow.push_back(w.material);
    std::sort(plan.windNow.begin(), plan.windNow.end());
    plan.windNow.erase(std::unique(plan.windNow.begin(), plan.windNow.end()), plan.windNow.end());
    const bool sameList=plan.listEpoch==listEpoch_, sameMaterials=plan.materialsEpoch==materialsEpoch_, sameMeshes=plan.meshEpoch==meshEpoch_,
               sameWinds=plan.windMaterials==plan.windNow;
    const bool kept=plan.valid && !plan.off && !split && sameList && sameMaterials && sameMeshes && sameWinds;
    if(plan.off) plan_plant_list_reference(plan, split); // (a check found a difference: the way it was made until 2026-10-12, every frame)
    else {
        if(!kept) {
            if(plan.valid) { if(!sameList) ++plan.whyList; if(!sameMaterials) ++plan.whyMaterials; if(!sameMeshes) ++plan.whyMeshes; if(!sameWinds) ++plan.whyWinds; }
            plan_plant_list(plan, split);
            plan.valid=true; plan.listEpoch=listEpoch_; plan.materialsEpoch=materialsEpoch_; plan.meshEpoch=meshEpoch_; plan.windMaterials=plan.windNow;
            ++plan.made;
        } else ++plan.kept;
        // FOR A CHECK: the list as it was made until 2026-10-12 (plan_plant_list_reference), made beside this one and
        // compared, cell by cell and member by member: at every frame with the environment's TGPT_TEST_RECORD_CHECK, and
        // without it at every 512th frame that kept its list. A difference is said in the log, and from that frame on the
        // list is the old way's, made every frame, for the rest of the session.
        if(recordCheck_ || (kept && (++plan.sinceCheck&511u)==0)) {
            RecordCpu::Carve carve{record_cpu, "the plants' plan: the check of the kept list"};
            ++plan.checks;
            PlantPlan was;
            plan_plant_list_reference(was, split);
            if(!same_plant_list(plan, was)) {
                log("PT record check: THE PLANTS' KEPT LIST DIFFERS from the list made the old way ("+std::to_string(plan.members.size())+" plants in "+std::to_string(plan.cells.size())+
                    " cells against "+std::to_string(was.members.size())+" in "+std::to_string(was.cells.size())+"; it was "+(kept ? "kept" : "made")+
                    " this frame); the list is made the old way, every frame, from here on");
                plan.off=true; plan.cells=std::move(was.cells); plan.members=std::move(was.members); plan.order=std::move(was.order);
            } else if(recordCheck_ && !recordCheckSaid_[1]) {
                recordCheckSaid_[1]=true;
                log("PT record check: the plants' kept list is the list made the old way ("+std::to_string(plan.members.size())+" plants in "+std::to_string(plan.cells.size())+" cells); checked at every frame");
            }
        }
    }
    if(const uint64_t tick=GetTickCount64(); !plan.since) plan.since=tick;
    else if(tick-plan.since>=10000) {
        char b[768];
        const double timedFrames=double(std::max<uint64_t>(plantMarkFrames_, 1));
        std::snprintf(b, sizeof b, "PT plants' plan 10s: %zu plants in %zu cells of the scene's %zu instances; their list kept in %llu frames and made anew in %llu "
            "(what was no longer what it was made from: the meshes %llu times, the scene's list %llu, the materials %llu, the materials with a wind %llu); %llu checks%s. "
            "The plants' steps' marks (PT-125), timed in one frame of 64: %.3f ms a frame for %.0f marks",
            plan.members.size(), plan.cells.size(), instances_.size(), (unsigned long long)plan.kept, (unsigned long long)plan.made, (unsigned long long)plan.whyMeshes,
            (unsigned long long)plan.whyList, (unsigned long long)plan.whyMaterials, (unsigned long long)plan.whyWinds, (unsigned long long)plan.checks,
            plan.off ? "; KEEPING IS OFF (a check found a difference)" : "", plantMarksMs_/timedFrames, double(plantMarks_)/timedFrames);
        log(b);
        plan.since=tick; plan.kept=plan.made=plan.checks=plan.whyMeshes=plan.whyList=plan.whyMaterials=plan.whyWinds=0;
        plantMarksMs_=0.0; plantMarks_=plantMarkFrames_=0;
    }
    record_cpu.section("the plants' plan: each plant's micromaps");
    // Every frame, in the order of the scene's list (the order micromaps are asked for in is the order they are baked in):
    for(const uint32_t at:plan.order) {
        Member& member=plan.members[at];
        member.link=0; member.pending=false;
        const auto& inst=instances_[member.source];
        Mesh* m=meshes_[size_t(inst.mesh)-1].get();
        const MeshRecordGpu& own=meshRecords_[size_t(inst.mesh)];
        // (M1: its turn at its mesh's micromaps, as a still instance's (omm_instance: true when they were made for these
        // very materials as they are now). Micromaps baked before the mesh was known to sway have no index buffer kept:
        // they are let go and asked for again, which keeps it.)
        bool isSmall=false;
#if TGPT_MEGA_GEOMETRY
        isSmall=megaSplit && mega_small(*m, inst.transform);
        if(!(megaNoMaps && (isSmall || megaTrial==8)))
#endif
        if(plantMaps && (inst.flags&TGPT_INSTANCE_ALPHA_TEST)) {
            const uint32_t* mats=materialIndices_.data()+inst.materialBase;
            bool mapped=omm_instance(*m, mats, retireValue);
            if(mapped && (!m->ommIndices || m->ommFirst.size()!=own.geometryCount)) { release_omm(*m, retireValue); mapped=omm_instance(*m, mats, retireValue); }
#if TGPT_MEGA_GEOMETRY
            // (megageo-5: a plant the trial makes of clusters links its mesh's micromaps as the clusters' own micromap array, made from the same
            // bake (record_omm). Micromaps baked before the trial asked for that array have none: they are let go and asked
            // for again, as M1 does for the index buffer, and the next bake makes both. One whose array could not be made
            // keeps that build's number with no array: its clusters take none, and it is not baked again for it.)
            if(mapped && megaMaps && megaClusters && mega_cluster_plant(*m, inst.transform) && m->megaMaps.serial!=m->ommSerial) {
                release_omm(*m, retireValue); mapped=omm_instance(*m, mats, retireValue); ++megaRebaked_;
            }
#endif
            if(mapped) member.link=m->ommSerial;
            member.pending=!mapped && m->omm==Mesh::Omm::Waiting && swayFrame_-m->ommWantedFrame<kPlantMapWait;
        }
        (void)isSmall;
    }
    record_cpu.section("the plants' plan: the cells' groups");
    // Groups whose cell has no plant any more go.
    for(auto it=plantGroups_.begin();it!=plantGroups_.end();) {
        const auto planned=std::lower_bound(plan.cells.begin(), plan.cells.end(), it->first, [](const PlantPlan::Cell& c, const std::pair<int64_t, int64_t>& cell) { return c.cell<cell; });
        if(planned!=plan.cells.end() && planned->cell==it->first) { ++it; continue; }
        release_plant_group(it->second, &heap, release, retireValue);
        it=plantGroups_.erase(it);
    }
    uint32_t builds=0; uint64_t buildTriangles=0;
    // (FOR MEASURING, PT-110: the plugin's hidden "Micromaps trial" 16, the groups' plain submeshes tested as before M3.)
    const bool plainTested=(ommTrial&TGPT_OMM_TRIAL_PLAIN_TESTED)!=0;
    // (A cell's members as the cells' loop reads them: the kept list's, in the order of their keys.)
    struct MemberList {
        const Member* first; size_t count;
        const Member* begin() const { return first; }
        const Member* end() const { return first+count; }
        size_t size() const { return count; }
        const Member& operator[](size_t i) const { return first[i]; }
    };
    for(const PlantPlan::Cell& planned:plan.cells) {
        const std::pair<int64_t, int64_t>& cell=planned.cell;
        const MemberList list{plan.members.data()+planned.first, planned.count};
        // (Its members in the order of their keys, then of their places in the scene's list, and the signature of those
        // keys and their count: plan_plant_list.)
        uint64_t signature=planned.signature;
        if(plainTested) { signature^=0x706C61696Eull; signature*=1099511628211ull; } // (FOR MEASURING: made anew when the trial's 16 comes or goes)
        if(plantMaps && (ommTrial&TGPT_OMM_TRIAL_PLANT_REBUILD)) { signature^=0x72656275696C64ull; signature*=1099511628211ull; } // (and M1's 64)
#if TGPT_MEGA_GEOMETRY
        // (PT-109 on trial, 5, 7, 8: a group of the trial's plants is made of clusters when every member's mesh is cut and its
        // cut is on the GPU (mega_prepare: a mesh not yet cut is asked for here, and the group is plain meanwhile), no member
        // holds glass (whose checks read a plain group's records) and (megageo-5) every member that links micromaps has them
        // as the clusters' own micromap array with the bake's indices back on the CPU (a few frames after its bake); made anew, as clusters, when
        // the last of those is there. Its trial's number is in its signature: 7 and 8 make other groups.)
        bool clusterGroup=megaClusters && (megaTrial!=5 || cell.second>kMegaSmallTag/2);
        uint64_t clusterCount=0;
        if(clusterGroup) for(const auto& member:list) {
            const auto& inst=instances_[member.source];
            const MeshRecordGpu& own=meshRecords_[size_t(inst.mesh)];
            const auto cut=megaMeshes_.find(inst.mesh);
            if(cut==megaMeshes_.end() || !cut->second.order || cut->second.firstCluster.size()!=size_t(own.geometryCount)+1) { megaWanted_.insert(inst.mesh); clusterGroup=false; continue; }
            clusterCount+=cut->second.clusters.size();
            for(uint32_t gi=0;gi<own.geometryCount;++gi) if(materials_[materialIndices_[inst.materialBase+gi]].flags&TGPT_MATERIAL_GLASS) clusterGroup=false;
            const Mesh* mm=meshes_[size_t(inst.mesh)-1].get();
            if(megaMaps && member.link && mm->megaMaps.serial==member.link && mm->megaMaps.array && !mm->megaMaps.ready()) clusterGroup=false; // (its indices on their way back)
        }
        if(clusterCount>kMegaGeometryMost+1ull) clusterGroup=false;
        if(clusterGroup) { signature^=0x636C757374657273ull^uint64_t(megaTrial); signature*=1099511628211ull; }
#endif
        auto found=plantGroups_.find(cell);
        // Its members changed (or it is new): made anew, if the frame still has room for it.
        const uint64_t vertices=planned.vertices, indices=planned.indices; // (its members' together: plan_plant_list)
        // (PT-98: the frame's room goes by the clock; its triangles are memory in flight, kMemoryClockMost.)
        const bool room=!(builds>=by_clock(kPlantBuildsPerFrame) || (builds && buildTriangles+indices/3>uint64_t(double(kPlantBuildTriangles)*std::min(clock_, kMemoryClockMost))));
        bool pending=false; // (M1: a member's micromaps are on their way)
        for(const auto& member:list) pending|=member.pending;
        if(found!=plantGroups_.end() && found->second.signature==signature && found->second.data && found->second.blas) {
            PlantGroup& g=found->second;
            // M1: the same members; and the same micromaps linked as it was built with? `lost`: a member's micromaps it
            // links are gone or were made anew (their array goes when the GPU is past the frame that let them go): it
            // must never be traced again, and is made anew like a group whose members changed. Otherwise it is whole,
            // and only lacks micromaps that have come since: it serves as it is until every member's are in and the
            // frame has room to make it anew with them.
            bool same=g.memberMaps.size()==list.size(), lost=!same;
            for(size_t i=0;i<list.size() && i<g.memberMaps.size();++i) { same&=g.memberMaps[i]==list[i].link; lost|=g.memberMaps[i]!=0 && g.memberMaps[i]!=list[i].link; }
            if(same || (!lost && (pending || !room))) {
                for(size_t i=0;i<list.size();++i) { g.sources[i]=list[i].source; grouped[list[i].source]=1; }
                g.usedFrame=swayFrame_;
                uses.push_back(PlantUse{&g, 0, false});
                continue;
            }
        } else if(pending && room) {
            // (M1: a new group whose members' micromaps are on their way waits for them, kPlantMapWait frames at most,
            // its members traced where they stand still meanwhile: built once, with them, in place of twice.)
            waiting+=uint32_t(list.size());
            continue;
        }
        if(found!=plantGroups_.end()) { release_plant_group(found->second, &heap, release, retireValue); plantGroups_.erase(found); }
        // One buffer: positions (12 bytes a vertex), texture coordinates (8), colors, normals and each vertex's range (4
        // each), then the indices, then each vertex's move since the frame before (8: the camera rays' motion). Each part
        // starts on 16 bytes: a raw view may start nowhere else (D3D12_RAW_UAV_SRV_BYTE_ALIGNMENT; a view that does not
        // removes the device).
        auto aligned=[](uint64_t v) { return (v+15)/16*16; };
        const uint64_t uvsAt=aligned(vertices*12), colorsAt=aligned(uvsAt+vertices*8), normalsAt=aligned(colorsAt+vertices*4),
                       rangeAt=aligned(normalsAt+vertices*4), indicesAt=aligned(rangeAt+vertices*4), previousAt=aligned(indicesAt+indices*4);
        const uint64_t dataBytes=aligned(previousAt+vertices*8);
        // (PT-98: the frame's room goes by the clock; its triangles are memory in flight, kMemoryClockMost.)
        if(builds>=by_clock(kPlantBuildsPerFrame) || (builds && buildTriangles+indices/3>uint64_t(double(kPlantBuildTriangles)*std::min(clock_, kMemoryClockMost))) ||
           !vertices || !indices || dataBytes>=(1ull<<32)) {
            waiting+=uint32_t(list.size());
            continue;
        }
        RecordCpu::Carve carve{record_cpu, "the plants' plan: groups made anew"};
        PlantGroup g;
        g.signature=signature; g.serial=++plantSerial_;
        g.compactable=!(cutsOff&kMemory2Plants); // PT-87: built so that it may be compacted (compact_plants)
        int64_t cellZ=cell.second;
#if TGPT_MEGA_GEOMETRY
        if(cellZ>kMegaSmallTag/2) cellZ-=kMegaSmallTag; // (PT-109 on trial: a group of a cell's small plants stands in that cell)
#endif
        g.origin[0]=double(cell.first)*kPlantCell; g.origin[1]=double(instances_[list[0].source].transform[7]); g.origin[2]=double(cellZ)*kPlantCell;
        g.vertexCount=uint32_t(vertices); g.indexCount=uint32_t(indices); g.triangles=indices/3;
        uint32_t vertexBase=0, indexBase=0;
        // M3 (PT-110, docs/agents/omm-1.md): a group's structure is its own, so each submesh says in it what its material
        // is to a ray: solid, unless the material is alpha-tested. Until 2026-10-10 a member with one alpha-tested material
        // had every submesh non-opaque (a tree's trunk and branches with its leaves), and a ray's query handed all of
        // them to the shader's alpha test, which answered "solid". The same hits (record's note on characters). A group
        // that holds glass keeps the old way whole (pt_common caustic_clear passes its own instance's candidates), and
        // so does every group FOR MEASURING (plainTested).
        bool wholeTested=plainTested;
        for(const auto& member:list) {
            const auto& inst=instances_[member.source];
            const uint32_t count=meshRecords_[size_t(inst.mesh)].geometryCount;
            for(uint32_t gi=0;gi<count && !wholeTested;++gi) wholeTested=(materials_[materialIndices_[inst.materialBase+gi]].flags&TGPT_MATERIAL_GLASS)!=0;
        }
        for(const auto& member:list) {
            const auto& inst=instances_[member.source];
            Mesh* m=meshes_[size_t(inst.mesh)-1].get();
            const MeshRecordGpu& own=meshRecords_[size_t(inst.mesh)];
            PlantMemberGpu pm{};
            for(int r=0;r<3;++r) for(int k=0;k<4;++k) pm.toGroup[r*4+k]=k==3 ? float(double(inst.transform[r*4+3])-g.origin[r]) : inst.transform[r*4+k];
            // World to object: the inverse of the instance's 3x3 (a flat one has none: the wind then moves nothing of it).
            const double a[9]={inst.transform[0], inst.transform[1], inst.transform[2], inst.transform[4], inst.transform[5], inst.transform[6],
                               inst.transform[8], inst.transform[9], inst.transform[10]};
            const double det=a[0]*(a[4]*a[8]-a[5]*a[7])-a[1]*(a[3]*a[8]-a[5]*a[6])+a[2]*(a[3]*a[7]-a[4]*a[6]);
            const double rdet=std::fabs(det)>1e-12 ? 1.0/det : 0.0;
            const double inv[9]={(a[4]*a[8]-a[5]*a[7])*rdet, (a[2]*a[7]-a[1]*a[8])*rdet, (a[1]*a[5]-a[2]*a[4])*rdet,
                                 (a[5]*a[6]-a[3]*a[8])*rdet, (a[0]*a[8]-a[2]*a[6])*rdet, (a[2]*a[3]-a[0]*a[5])*rdet,
                                 (a[3]*a[7]-a[4]*a[6])*rdet, (a[1]*a[6]-a[0]*a[7])*rdet, (a[0]*a[4]-a[1]*a[3])*rdet};
            for(int r=0;r<3;++r) { for(int k=0;k<3;++k) pm.toObject[r*4+k]=float(inv[r*3+k]); pm.toObject[r*4+3]=0.f; }
            pm.positionsIn=m->positionsSrv; pm.uvsIn=own.uvs; pm.colorsIn=own.colors; pm.normalsIn=own.normals; pm.indicesIn=own.indices;
            pm.vertexBase=vertexBase; pm.vertexCount=own.vertexCount;
#if TGPT_MEGA_GEOMETRY
            // (PT-109 on trial: a member of a group of clusters reads its indices in its cut's order, step 4: each submesh's
            // triangles in its clusters' order, in the submesh's own place, so its ranges stay as they are.)
            const MegaMeshClusters* cut=clusterGroup ? &megaMeshes_.at(inst.mesh) : nullptr;
            if(cut) pm.indicesIn=cut->orderSrv;
            PlantGroup::MegaMember megaMember{inst.mesh, 0, 0, vertexBase, uint32_t(g.geometries.size()), 0};
#endif
            const bool alpha=(inst.flags&TGPT_INSTANCE_ALPHA_TEST)!=0;
            // (M1: the member links the micromaps plan_plants found for it this frame, if they are the ones its mesh has
            // now, with their index buffer: every cut-out submesh of it then names the mesh's array and its own part of
            // that buffer. The mesh's micromaps were made for these very materials: omm_instance.)
            const bool maps=member.link!=0 && m->omm==Mesh::Omm::Linked && m->ommSerial==member.link && m->ommArrayVa && m->ommIndices && m->ommFirst.size()==own.geometryCount;
            g.memberMaps.push_back(member.link); // (what the plan found, linked or not: the same answer next frame is the same group)
            if(maps) {
                g.linked=true;
                auto known=std::find_if(g.mapMeshes.begin(), g.mapMeshes.end(), [&](const auto& pair) { return pair.first==inst.mesh; });
                if(known==g.mapMeshes.end()) g.mapMeshes.emplace_back(inst.mesh, 1u); else ++known->second;
            }
            for(uint32_t gi=0;gi<own.geometryCount;++gi) {
                const GeometryRecordGpu& source=geometryRecords_[own.geometryBase+gi];
                PlantRangeGpu range{}; range.member=uint32_t(g.members.size()); range.wind=kInvalid;
                range.indexStartIn=source.indexStart; range.indexCount=source.indexCount; range.indexStartOut=indexBase;
                g.ranges.push_back(range);
                g.rangeMaterials.push_back(materialIndices_[inst.materialBase+gi]);
                g.foliage|=is_foliage(materials_[g.rangeMaterials.back()].flags);
                const bool cutout=(materials_[g.rangeMaterials.back()].flags&TGPT_MATERIAL_ALPHA_TEST)!=0;
                const bool tested=alpha && (cutout || wholeTested); // (M3: a plain submesh of a member with cut-outs is solid)
#if TGPT_MEGA_GEOMETRY
                // (PT-109 on trial: one record a cluster, its triangles where step 4 puts them, as many of its first closed as
                // the cut put first; its template's clusters solid where the submesh is.)
                if(cut) {
                    for(uint32_t c=cut->firstCluster[gi];c<cut->firstCluster[gi+1];++c) {
                        const auto& k=cut->clusters[c];
                        g.geometries.push_back(GeometryRecordGpu{indexBase+(k.firstIndex-source.indexStart), k.triangles*3, inst.renderingLayerMask,
                                                                 kGeometryOwnLayers|(source.flags&kGeometryClosed)|(std::min(k.solid, kGeometrySolidMax)<<kGeometrySolidShift)});
                        g.geometryMaterials.push_back(g.rangeMaterials.back());
                    }
                    if(!tested && gi<64) megaMember.opaque|=1ull<<gi;
                    // (megageo-5: a cut-out that links its micromaps as a plain group would, here as the clusters' own micromap array of the same build.)
                    if(megaMaps && maps && alpha && cutout && gi<64 && m->ommFirst[gi]!=kInvalid && m->megaMaps.serial==member.link && m->megaMaps.ready()) {
                        megaMember.linked|=1ull<<gi; megaMember.serial=member.link;
                    }
                } else
#endif
                g.geometries.push_back(GeometryRecordGpu{indexBase, source.indexCount, inst.renderingLayerMask, kGeometryOwnLayers|(source.flags&(kGeometryClosed|kGeometrySolidBits))});
                g.opaque.push_back(tested ? 0 : 1);
                PlantGroup::Link link;
                if(maps && alpha && cutout && m->ommFirst[gi]!=kInvalid) { link.array=m->ommArrayVa; link.indices=m->ommIndices.va()+uint64_t(m->ommFirst[gi])*4; }
                g.links.push_back(link);
                // (PT-110, for the log: a cut-out's triangles, or a plain submesh's: asked about by the shader's test, or spared.)
                if(alpha) (cutout ? g.cutoutTriangles : tested ? g.plainTested : g.plainSpared)+=source.indexCount/3;
                indexBase+=source.indexCount;
            }
            g.alpha|=alpha; g.cutoutMembers+=alpha ? 1u : 0u;
#if TGPT_MEGA_GEOMETRY
            if(cut) g.megaMembers.push_back(megaMember);
#endif
            g.members.push_back(pm);
            g.sources.push_back(member.source);
            vertexBase+=own.vertexCount;
        }
        // (M1's number 64, FOR FINDING A CAUSE: a group with micromaps that is to be built anew at every pose is never
        // compacted: pose_plants.)
        if(g.linked && (ommTrial&TGPT_OMM_TRIAL_PLANT_REBUILD)) g.compactable=false;
        try {
            g.positionsAt=0; g.uvsAt=uint32_t(uvsAt); g.colorsAt=uint32_t(colorsAt); g.normalsAt=uint32_t(normalsAt);
            g.rangeAt=uint32_t(rangeAt); g.indicesAt=uint32_t(indicesAt); g.previousAt=uint32_t(previousAt);
            g.data=make_buffer(device, dataBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON, "Create a group of plants in the wind");
            auto part=[&](uint32_t at, uint64_t bytes) {
                const uint32_t i=heap.allocate();
                D3D12_SHADER_RESOURCE_VIEW_DESC d{}; d.Format=DXGI_FORMAT_R32_TYPELESS; d.ViewDimension=D3D12_SRV_DIMENSION_BUFFER;
                d.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; d.Buffer.FirstElement=at/4; d.Buffer.NumElements=UINT(std::max<uint64_t>(bytes/4, 1));
                d.Buffer.Flags=D3D12_BUFFER_SRV_FLAG_RAW;
                device->CreateShaderResourceView(g.data.resource.Get(), &d, heap.cpu(i)); return i;
            };
            g.dataUav=raw_uav(device, heap, g.data.resource.Get(), dataBytes);
            g.positionsSrv=part(g.positionsAt, uint64_t(g.vertexCount)*12); g.uvsSrv=part(g.uvsAt, uint64_t(g.vertexCount)*8);
            g.colorsSrv=part(g.colorsAt, uint64_t(g.vertexCount)*4); g.normalsSrv=part(g.normalsAt, uint64_t(g.vertexCount)*4);
            g.indicesSrv=part(g.indicesAt, uint64_t(g.indexCount)*4); g.previousSrv=part(g.previousAt, uint64_t(g.vertexCount)*8);
#if TGPT_MEGA_GEOMETRY
            // (PT-109 on trial: a group of clusters: a room for each cluster's stamp and for its bottom level, of the driver's
            // own sizes; never compacted, since it is built anew every frame it is posed. Its stamps wait for its templates:
            // pose_plants.)
            // (megageo-5: with micromaps (5, 7) every call of the trial carries FLAG_ALLOW_OMM, so the rooms are asked for with it.
            // 7 and 8, NVIDIA's layout: each member's bottom level in a room of its own in `blas`, from its own clusters; the
            // member's stamps are its clusters, from its first record on.)
            if(clusterGroup) {
                g.clusters=true; g.compactable=false; g.mapped=megaMaps; g.perCopy=megaTrial==7 || megaTrial==8;
                const uint32_t clusters=uint32_t(g.geometries.size());
                g.clas=make_buffer(device, megaBuilder_.stamp_bytes(device, g.mapped)*clusters, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                                   D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, "Create a group of plants' clusters");
                uint64_t bottomRoom=0;
                if(g.perCopy) for(size_t i=0;i<g.megaMembers.size();++i) {
                    const uint32_t first=g.megaMembers[i].geometryBase, last=i+1<g.megaMembers.size() ? g.megaMembers[i+1].geometryBase : clusters;
                    const uint64_t bytes=megaBuilder_.bottom_bytes(device, last-first, g.mapped);
                    g.copies.push_back(MegaClusterBuilder::Bottom{bottomRoom, first, last-first, bytes});
                    bottomRoom+=bytes; // (each aligned to D3D12's 256 bytes)
                } else {
                    bottomRoom=megaBuilder_.bottom_bytes(device, clusters, g.mapped);
                    g.copies.push_back(MegaClusterBuilder::Bottom{0, 0, clusters, bottomRoom});
                }
                g.blas=make_buffer(device, bottomRoom, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                                   D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, g.perCopy ? "Create a group of plants' BLAS of clusters, one a plant" : "Create a group of plants' BLAS of clusters");
                g.megaRoom=allocation_bytes(g.clas.resource.Get())+allocation_bytes(g.blas.resource.Get());
                g.scratchBytes=g.updateBytes=0;
            } else {
#endif
            PlantGeometry geometry; plant_geometry(g, geometry); // (each submesh as its material is; M1: with its micromaps)
            const auto& geos=geometry.geos;
            D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS in{};
            in.Type=D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
            in.Flags=plant_flags(g);
            in.NumDescs=UINT(geos.size()); in.DescsLayout=D3D12_ELEMENTS_LAYOUT_ARRAY; in.pGeometryDescs=geos.data();
            D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info{}; device->GetRaytracingAccelerationStructurePrebuildInfo(&in, &info);
            g.blas=make_buffer(device, info.ResultDataMaxSizeInBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                               D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, "Create a group of plants' BLAS");
            g.scratchBytes=std::max(info.ScratchDataSizeInBytes, info.UpdateScratchDataSizeInBytes);
            g.updateBytes=info.UpdateScratchDataSizeInBytes;
#if TGPT_MEGA_GEOMETRY
            }
#endif
        } catch(const std::exception& e) {
#if TGPT_MEGA_GEOMETRY
            if(clusterGroup) mega_fail(std::string("a group's room: ")+e.what()); // (the next frame makes it plain)
#endif
            log_throttled(std::string("A group of plants in the wind could not be made: ")+e.what()+" ("+std::to_string(vertices)+" vertices, "+std::to_string(indices)+" indices)");
            release_plant_group(g, &heap, release, retireValue);
            waiting+=uint32_t(list.size());
            continue;
        }
        g.usedFrame=swayFrame_;
        ++builds; buildTriangles+=g.triangles;
        for(const auto& member:list) grouped[member.source]=1;
        auto placed=plantGroups_.emplace(cell, std::move(g)).first;
        uses.push_back(PlantUse{&placed->second, 0, true});
    }
    std::lock_guard<std::mutex> s(statsMutex_); stats_.plantWaiting=waiting;
}

// The groups in use this frame: a new one's texture coordinates, colors, normals and indices merged from its members'
// meshes (pt_sway steps 3 and 4), every one posed as the game's vertex shaders pose it this frame (step 5: the wind's
// time, speed and direction, the camera for leaves' fade), and its BLAS built (new) or refitted. A pose already made for
// the same wind and camera (the game paused) is kept.
void GpuScene::pose_plants(ID3D12Device5* device, ID3D12GraphicsCommandList4* list, DescriptorHeap& heap, UploadRing& ring, DeferredRelease& release,
                           uint64_t retireValue, const std::vector<PlantUse>& uses, const double camera[3], Barriers& barriers) {
#if TGPT_MEGA_GEOMETRY
    MegaCpuTimer poseTimer(megaCpu_[1]);
#endif
    record_cpu.section("the plants' pose: winds, ranges and group records");
    // The winds, with each one's noise map; the maps' bytes when a map was sent since they were uploaded.
    const WindNoise* noise=windNoise_.get();
    std::vector<PlantWindGpu> winds(plantWinds_.size());
    for(size_t i=0;i<plantWinds_.size();++i) {
        const TGPT_PlantWind& w=plantWinds_[i];
        PlantWindGpu& o=winds[i];
        o.kind=w.kind; o.flags=w.flags&TGPT_PLANT_BEND;
        o.strength=w.strength; o.bendHeight=w.bendHeight; o.pivotY=w.pivotOffsetY; o.fadeX=w.fade[0]; o.fadeY=w.fade[1];
        if(noise && w.noise) {
            const auto found=noise->maps.find(w.noise);
            if(found!=noise->maps.end() && uint64_t(found->second.offset)+uint64_t(found->second.width)*found->second.height<=noise->bytes.size()) {
                o.noiseOffset=found->second.offset; o.noiseSize=found->second.width|(found->second.height<<16);
                o.flags|=(found->second.flags&7u)<<8;
            }
        }
    }
    if(!plantWindBuffer_.buffer || winds.size()!=uploadedPlantWinds_.size() ||
       (!winds.empty() && std::memcmp(winds.data(), uploadedPlantWinds_.data(), winds.size()*sizeof(PlantWindGpu))!=0)) {
        upload_structured(device, list, heap, ring, release, retireValue, plantWindBuffer_, winds.data(), uint32_t(winds.size()), sizeof(PlantWindGpu), barriers);
        uploadedPlantWinds_=winds;
        for(auto& [cell, g]:plantGroups_) g.posedFor[9]=-1.f; // every pose is made again with the new winds
    }
    if(!noiseBuffer_.buffer || windNoise_!=uploadedNoise_) {
        static const uint32_t none[4]={};
        const bool have=noise && noise->bytes.size()>=4;
        std::vector<uint32_t> words;
        if(have) { words.resize((noise->bytes.size()+3)/4, 0u); std::memcpy(words.data(), noise->bytes.data(), noise->bytes.size()); }
        upload_structured(device, list, heap, ring, release, retireValue, noiseBuffer_, have ? words.data() : none, have ? uint32_t(words.size()) : 1u, 4, barriers);
        uploadedNoise_=windNoise_;
        for(auto& [cell, g]:plantGroups_) g.posedFor[9]=-1.f;
    }
    // The member and range records of the groups in use, one after another, uploaded when the groups or their order changed;
    // each range's wind is looked up in this frame's winds.
    std::vector<PlantGroup*> order; order.reserve(uses.size());
    for(const auto& u:uses) order.push_back(u.group);
    std::vector<uint32_t> memberFirst(uses.size()), rangeFirst(uses.size());
    {
        uint32_t members=0, ranges=0;
        for(size_t i=0;i<uses.size();++i) { memberFirst[i]=members; rangeFirst[i]=ranges; members+=uint32_t(uses[i].group->members.size()); ranges+=uint32_t(uses[i].group->ranges.size()); }
        bool changed=order!=uploadedPlantGroups_ || !plantMemberBuffer_.buffer || !plantRangeBuffer_.buffer;
        for(const auto& u:uses) changed|=u.fresh;
        std::vector<PlantRangeGpu> allRanges; allRanges.reserve(ranges);
        for(size_t i=0;i<uses.size();++i) {
            const PlantGroup& g=*uses[i].group;
            for(size_t r=0;r<g.ranges.size();++r) {
                PlantRangeGpu range=g.ranges[r];
                range.member+=memberFirst[i];
                // (plantWindOf_'s answer, from set_plant_wind's table; the map itself for a number the table leaves out.)
                const uint32_t material=g.rangeMaterials[r];
                if(material<plantWindTable_.size()) range.wind=plantWindTable_[material];
                else if(!plantWindPastTable_) range.wind=kInvalid;
                else { const auto wind=plantWindOf_.find(material); range.wind=wind==plantWindOf_.end() ? kInvalid : wind->second; }
                allRanges.push_back(range);
            }
        }
        if(changed) {
            std::vector<PlantMemberGpu> allMembers; allMembers.reserve(members);
            for(const auto& u:uses) allMembers.insert(allMembers.end(), u.group->members.begin(), u.group->members.end());
            upload_structured(device, list, heap, ring, release, retireValue, plantMemberBuffer_, allMembers.data(), uint32_t(allMembers.size()), sizeof(PlantMemberGpu), barriers);
            uploadedPlantGroups_=order;
        }
        // The ranges every frame: small (32 bytes a submesh), and a material's wind may come or go.
        upload_structured(device, list, heap, ring, release, retireValue, plantRangeBuffer_, allRanges.data(), uint32_t(allRanges.size()), sizeof(PlantRangeGpu), barriers);
        // (The offline check of PT-125 alone, orderTest: the frame's groups in order, each with its cell's corner, its
        // serial and where its member and range records start in this frame's buffers, said whenever any of it changes.
        // The check reads it to know that a group's place did move under a group that was not made anew.)
        if(orderTest) {
            std::string said="PT test, the plants' groups in this frame's order: "+std::to_string(uses.size());
            for(size_t i=0;i<uses.size();++i) {
                const PlantGroup& g=*uses[i].group;
                char part[200]; std::snprintf(part, sizeof part, " | x %.0f z %.0f serial %llu members %u+%zu ranges %u+%zu", g.origin[0], g.origin[2],
                                              (unsigned long long)g.serial, memberFirst[i], g.members.size(), rangeFirst[i], g.ranges.size());
                said+=part;
            }
            static std::string told;
            if(said!=told) { told=said; log(said); }
        }
    }
    // This frame's group records, and which groups need a pose.
    const float posedFor[10]={plantFrame_.time, plantFrame_.speed, plantFrame_.direction, plantFrame_.forward[0], plantFrame_.forward[1], plantFrame_.forward[2],
                              float(camera[0]), float(camera[1]), float(camera[2]), 1.f};
    std::vector<PlantGroupGpu> records(uses.size());
    std::vector<uint8_t> pose(uses.size(), 0), keep(uses.size(), 0);
    bool any=false;
    for(size_t i=0;i<uses.size();++i) {
        PlantGroup& g=*uses[i].group;
        PlantGroupGpu& r=records[i];
        r.members=plantMemberBuffer_.srv; r.memberFirst=memberFirst[i]; r.memberCount=uint32_t(g.members.size());
        r.ranges=plantRangeBuffer_.srv; r.rangeFirst=rangeFirst[i]; r.rangeCount=uint32_t(g.ranges.size());
        r.vertexCount=g.vertexCount; r.indexCount=g.indexCount;
        r.dataOut=g.dataUav; r.positionsAt=g.positionsAt; r.uvsAt=g.uvsAt; r.colorsAt=g.colorsAt; r.normalsAt=g.normalsAt; r.rangeAt=g.rangeAt; r.indicesAt=g.indicesAt;
        r.winds=plantWindBuffer_.srv; r.noise=noiseBuffer_.srv;
        r.time=plantFrame_.time; r.speed=plantFrame_.speed; r.direction=plantFrame_.direction;
        for(int k=0;k<3;++k) { r.camera[k]=float(camera[k]-g.origin[k]); r.forward[k]=plantFrame_.forward[k]; r.origin[k]=float(g.origin[k]); }
        // Each vertex's move since the frame before (step 5, the camera rays' motion): from the pose in its buffer when it
        // was posed or kept last frame, else none.
        const bool continuous=g.merged && g.posedFrame+1==swayFrame_;
        g.posedFrame=swayFrame_;
        r.previousAt=g.previousAt; r.flags=continuous ? 1u : 0u;
        // (FOR FINDING A CAUSE, PT-110's M1: with the plugin's hidden "Micromaps trial" 64 beside 32, a group with
        // micromaps counts as not built whenever it is to be posed, so its structure is built anew in place of
        // refitted. Slow; it says whether a fault is the refit's. Only a group made while that number was set, which is
        // never compacted (plan_plants): a build needs its structure's full room.)
        if(g.built && g.linked && !g.compactable && (ommTrial&TGPT_OMM_TRIAL_PLANT_REBUILD) && std::memcmp(g.posedFor, posedFor, sizeof(posedFor))!=0) g.built=false;
        pose[i]=!g.built || std::memcmp(g.posedFor, posedFor, sizeof(posedFor))!=0;
        // A pose kept (the game paused, the wind still) after a move: no move this frame (step 6).
        keep[i]=!pose[i] && g.moving;
        if(pose[i]) g.moving=continuous; else if(keep[i]) g.moving=false;
        any|=pose[i] || keep[i];
    }
    upload_structured(device, list, heap, ring, release, retireValue, plantGroupBuffer_, records.data(), uint32_t(records.size()), sizeof(PlantGroupGpu), barriers);
    if(!any) return;
    record_cpu.section("the plants' pose: the pose pass's dispatches");
    for(size_t i=0;i<uses.size();++i) if(pose[i] || keep[i]) barriers.transition(uses[i].group->data, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    barriers.flush(list);
    list->SetPipelineState(swayPass);
    auto step=[&](uint32_t which, bool freshOnly) {
        bool ran=false; uint32_t dispatches=0;
        for(size_t i=0;i<uses.size();++i) {
            const PlantGroup& g=*uses[i].group;
            if(!(which==6 ? keep[i] : pose[i]) || (freshOnly && g.merged)) continue;
            const uint32_t values[8]={plantGroupBuffer_.srv, uint32_t(i), which, 0, 0, 0, 0, 0};
            list->SetComputeRoot32BitConstants(1, 8, values, 0);
            const uint32_t n=which==4 ? g.indexCount : g.vertexCount;
            list->Dispatch((n+63)/64, 1, 1);
            ran=true; ++dispatches;
            // (PT-125, FOR FINDING THE CAUSE: a mark after every dispatch. The pose pass 5 of 247 dispatches did not end on
            // 2026-10-12; this names the group. "bytes": its members, "scratch": its buffer's size; kind 20 + the pass.)
            plant_step(uint8_t(20u+which), g.origin[0], g.origin[2], n, g.members.size(), g.data.size);
        }
        if(ran) { barriers.uav(nullptr); barriers.flush(list); plant_step(uint8_t(10u+which), 0.0, 0.0, 0, dispatches, 0); } // (PT-125)
    };
    step(3, true);  // a new group's attributes, and every vertex's range cleared
    step(4, true);  // its indices, and the range of each vertex a triangle names
    step(5, false); // this frame's pose
    step(6, false); // no move where the pose is kept
    for(size_t i=0;i<uses.size();++i) if(pose[i] || keep[i]) barriers.transition(uses[i].group->data, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    barriers.flush(list);
    // Each group's BLAS: built the first time, refitted to the new pose after; side by side in the scratch (scratch_place).
    record_cpu.section("the plants' pose: builds and refits");
    uint64_t scratchTotal=0, scratchLargest=0;
    for(size_t i=0;i<uses.size();++i) {
        if(!pose[i]) continue;
#if TGPT_MEGA_GEOMETRY
        if(uses[i].group->clusters) continue; // (PT-109 on trial: built from its clusters, below)
#endif
        const uint64_t bytes=uses[i].group->built ? uses[i].group->updateBytes : uses[i].group->scratchBytes;
        scratchTotal+=scratch_aligned(bytes); scratchLargest=std::max(scratchLargest, bytes);
    }
    fit_scratch(device, release, retireValue, plantScratch_, plantScratchPeak_, scratchTotal, scratchLargest, "plants in the wind");
    // PT-87: each new group's build writes the size its structure compacts to (8 bytes a group, read back some frames on:
    // compact_plants).
    uint32_t fresh=0;
    for(size_t i=0;i<uses.size();++i) if(pose[i] && !uses[i].group->built && uses[i].group->compactable) ++fresh;
    Buffer compactSizes; PlantCompactBatch compact; compact.fence=retireValue;
    if(fresh) {
        compactSizes=make_buffer(device, uint64_t(fresh)*8, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                 "Create the plants' compact sizes");
        compact.readback=make_buffer(device, uint64_t(fresh)*8, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST, "Read the plants' compact sizes");
    }
    uint64_t offset=0;
    for(size_t i=0;i<uses.size();++i) {
        if(!pose[i]) continue;
        PlantGroup& g=*uses[i].group;
#if TGPT_MEGA_GEOMETRY
        if(g.clusters) continue; // (PT-109 on trial: built from its clusters, below)
#endif
        // (The same description at its build and at every refit; M1: a cut-out's links to its member mesh's micromaps
        // are passed again unchanged, as DirectX Raytracing asks of a refit.)
        // PT-99: made once a group and kept with it (PlantGroup::geometry), where it was made anew for every group at
        // every frame (three arrays allocated, zeroed and filled: 136 bytes a submesh of every plant in the wind). What
        // it is made from is the group's own and is written only where the group is made (plan_plants): its buffer and
        // where the positions and indices start in it, its vertex count, its submeshes' index ranges, which of them are
        // solid, and its links to micromaps. A group made anew is another PlantGroup, with none kept. Made here, on the
        // group where it stays (plantGroups_ never moves it): the descriptions point into the kept arrays.
        if(!g.geometryMade) { plant_geometry(g, g.geometry); g.geometryMade=true; }
        const auto& geos=g.geometry.geos;
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC desc{};
        desc.Inputs.Type=D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
        desc.Inputs.Flags=plant_flags(g);
        desc.Inputs.NumDescs=UINT(geos.size()); desc.Inputs.DescsLayout=D3D12_ELEMENTS_LAYOUT_ARRAY; desc.Inputs.pGeometryDescs=geos.data();
        desc.DestAccelerationStructureData=g.blas.va();
        desc.ScratchAccelerationStructureData=scratch_place(list, plantScratch_, offset, g.built ? g.updateBytes : g.scratchBytes);
        if(g.built) { desc.Inputs.Flags|=D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE; desc.SourceAccelerationStructureData=g.blas.va(); }
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_DESC post{};
        const bool sized=!g.built && g.compactable && compactSizes;
        if(sized) {
            post.DestBuffer=compactSizes.va()+compact.entries.size()*8; post.InfoType=D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_COMPACTED_SIZE;
            for(const auto& [cell, group]:plantGroups_) if(&group==&g) { compact.entries.push_back(PlantCompactEntry{cell, g.serial}); break; }
        }
        list->BuildRaytracingAccelerationStructure(&desc, sized ? 1u : 0u, sized ? &post : nullptr);
        plant_step(g.built ? 4 : 3, g.origin[0], g.origin[2], g.triangles, g.blas.size, g.built ? g.updateBytes : g.scratchBytes); // (PT-125)
        { auto& w=structureWork[kStructurePlants]; if(g.built) ++w.refits; else ++w.builds; w.triangles+=g.triangles; w.bytes+=g.blas.size; }
        g.merged=true; g.built=true; std::memcpy(g.posedFor, posedFor, sizeof(posedFor));
    }
#if TGPT_MEGA_GEOMETRY
    // PT-109 on trial (5, 7, 8; mega_geometry.hpp): the posed groups of plants made of clusters. A new group's stamps are
    // made once its members' templates are built (once a mesh, pattern of solid submeshes and build of the micromaps they
    // link); then every posed group's clusters are stamped from their templates with this frame's pose, and each group's
    // bottom level (7 and 8: each member's) is built from its clusters: one stamp call, then a call per size class. A failure turns the
    // clusters off for good (said once): such groups are not traced this frame and are made plain at the next.
    try {
        MegaCpuTimer clusterTimer(megaCpu_[2]);
        record_cpu.section("the plants' pose: Mega Geometry's clusters");
        barriers.uav(nullptr); barriers.flush(list);
        std::vector<MegaClusterBuilder::Part> parts; parts.reserve(uses.size());
        std::vector<PlantGroup*> stamped; stamped.reserve(uses.size());
        const bool omm=mega_maps_on(); // (megageo-5: every call of the frame carries FLAG_ALLOW_OMM while the trial links micromaps)
        for(size_t i=0;i<uses.size() && !megaClustersFailed_;++i) {
            PlantGroup& g=*uses[i].group;
            if(!g.clusters || !pose[i] || g.mapped!=omm) continue;
            if(g.stamps.empty()) {
                const uint64_t stampBytes=megaBuilder_.stamp_bytes(device, g.mapped);
                bool ok=true;
                for(const auto& mm:g.megaMembers) {
                    auto cut=megaMeshes_.find(mm.mesh);
                    Mesh* mesh=mm.mesh>0 && size_t(mm.mesh)<=meshes_.size() ? meshes_[size_t(mm.mesh)-1].get() : nullptr;
                    std::string error;
                    // (megageo-5: the member's micromaps, if they are still the build the plan linked; else its clusters take
                    // none: the plan makes the group anew at its next frame.)
                    MegaTemplateKey key; key.opaque=mm.opaque; key.omm=g.mapped;
                    const MegaMicromaps* maps=nullptr; const std::vector<uint32_t>* first=nullptr;
                    if(cut!=megaMeshes_.end() && mesh && g.mapped && mm.linked && mesh->megaMaps.serial==mm.serial && mesh->megaMaps.ready() && mesh->ommSerial==mm.serial &&
                       mesh->ommFirst.size()+1==cut->second.firstCluster.size()) {
                        key.linked=mm.linked; key.serial=mm.serial; maps=&mesh->megaMaps; first=&mesh->ommFirst;
                    }
                    if(cut==megaMeshes_.end() || !mesh || !mega_templates(device, list, heap, release, retireValue, cut->second, key, mesh->positionsVa, mesh->data.vertexCount, maps, first, barriers, error)) {
                        mega_fail("a mesh's templates: "+(error.empty() ? std::string("its cut or mesh is gone") : error)); ok=false; break;
                    }
                    const auto& templates=cut->second.templates.at(key);
                    for(uint32_t c=0;c<uint32_t(cut->second.clusters.size());++c) {
                        const auto& k=cut->second.clusters[c];
                        MegaClusterBuilder::Stamp s;
                        s.templ=templates.at[c]; s.vertices=g.data.va()+g.positionsAt+uint64_t(mm.vertexBase)*12;
                        s.geometryOffset=mm.geometryBase; s.triangles=k.triangles; s.vertexCount=k.vertices;
                        s.dest=g.clas.va()+stampBytes*(uint64_t(mm.geometryBase)+c);
                        g.stamps.push_back(s);
                    }
                }
                if(!ok || g.stamps.size()!=g.geometries.size()) { g.stamps.clear(); if(ok) mega_fail("a group's stamps do not match its records"); continue; }
            }
            // (7 and 8: a bottom level a member, from its own clusters; else the group's from all of them.)
            parts.push_back(MegaClusterBuilder::Part{g.stamps.data(), uint32_t(g.stamps.size()), g.copies.data(), uint32_t(g.copies.size()),
                g.blas.va(), g.clas.va(), g.blas.size, g.clas.size, g.serial});
            megaCpu_[6]+=double(g.copies.size());
            stamped.push_back(&g);
        }
        std::string error;
        if(!megaClustersFailed_ && !parts.empty()) {
            if(!megaBuilder_.build(device, list, ring, release, retireValue, parts, omm, barriers, error)) mega_fail("the frame's stamps: "+error);
            megaCpu_[5]=megaBuilder_.rewrote ? 1.0 : 0.0; megaCpu_[7]=megaBuilder_.bottomCalls;
        }
        if(megaClustersFailed_) { for(auto* g:stamped) g->built=false; stamped.clear(); }
        uint64_t clusters=0, triangles=0, room=0;
        for(PlantGroup* g:stamped) {
            { auto& w=structureWork[kStructurePlants]; w.builds+=g->perCopy ? uint32_t(g->copies.size()) : 1u; w.triangles+=g->triangles; w.bytes+=g->blas.size+g->clas.size; }
            g->merged=true; g->built=true; std::memcpy(g->posedFor, posedFor, sizeof(posedFor));
            clusters+=g->stamps.size(); triangles+=g->triangles; room+=g->megaRoom;
        }
        // In the log every 10 s while it serves, and (megageo-5) when the trial's number or the count of its plants that link
        // micromaps changed: what the frame stamped, and what the cut has made so far.
        static uint64_t toldAt=0, toldMapped=~0ull; static uint32_t toldTrial=~0u;
        const uint64_t now=GetTickCount64();
        // (The trial's plants: 5 the small ones' groups, 7 and 8 every group. megageo-5: their members that link micromaps, the
        // meshes whose micromaps are the clusters' own micromap arrays, and the bottom levels a frame: a group's, or one a plant.)
        uint32_t groups=0, candidates=0; uint64_t clusterRoom=0, members=0, mapped=0, bottomsMade=0;
        if(!stamped.empty()) for(const auto& [cell, group]:plantGroups_) if(megaTrial!=5 || cell.second>kMegaSmallTag/2) {
            ++candidates;
            if(!group.clusters) continue;
            ++groups; clusterRoom+=group.megaRoom;
            for(const auto& mm:group.megaMembers) { ++members; if(mm.linked) ++mapped; }
        }
        if(!stamped.empty() && (now-toldAt>=10000 || toldTrial!=megaTrial || toldMapped!=mapped)) {
            toldAt=now; toldTrial=megaTrial; toldMapped=mapped;
            for(PlantGroup* g:stamped) bottomsMade+=g->perCopy ? g->copies.size() : 1u;
            uint64_t arrays=0, arrayBytes=0;
            for(const auto& m:meshes_) if(m && m->megaMaps.array) { ++arrays; arrayBytes+=allocation_bytes(m->megaMaps.array.resource.Get()); }
            char b[1024];
            std::snprintf(b, sizeof b, "Mega Geometry trial %u: %zu groups of plants stamped from clusters this frame (%llu clusters, %llu triangles, %llu bottom levels%s; their clusters' and bottom "
                          "levels' room %.1f MB); of %u groups of the trial's plants, %u are made of clusters (%.1f MB); micromaps in clusters: %s, %llu of their %llu plants link them, "
                          "%llu meshes' as the clusters' own micromap arrays (%.1f MB; %llu built, %llu failed, %llu baked again for them); %llu meshes cut (%llu triangles, %.2f s), %zu waiting for their cut",
                          megaTrial, stamped.size(), (unsigned long long)clusters, (unsigned long long)triangles, (unsigned long long)bottomsMade,
                          megaTrial==7 || megaTrial==8 ? ", one a plant" : "", double(room)/1048576.0, candidates, groups, double(clusterRoom)/1048576.0,
                          omm ? "on" : (megaTrial==8 ? "off (the trial's 8)" : "off (the driver reports no micromaps for clusters)"), (unsigned long long)mapped, (unsigned long long)members,
                          (unsigned long long)arrays, double(arrayBytes)/1048576.0, (unsigned long long)megaMapsBuilt_, (unsigned long long)megaMapsFailed_, (unsigned long long)megaRebaked_,
                          (unsigned long long)megaCutMeshes_, (unsigned long long)megaCutTriangles_, megaCutSeconds_, megaWanted_.size());
            log(b);
        }
    } catch(const std::exception& e) {
        mega_fail(std::string("the frame's clusters: ")+e.what());
        for(size_t i=0;i<uses.size();++i) if(uses[i].group->clusters && pose[i]) uses[i].group->built=false; // (not traced this frame)
    }
#endif
    barriers.uav(nullptr); barriers.flush(list);
    if(compactSizes) {
        barriers.transition(compactSizes, D3D12_RESOURCE_STATE_COPY_SOURCE); barriers.flush(list);
        list->CopyResource(compact.readback.resource.Get(), compactSizes.resource.Get());
        release.add(retireValue, compactSizes.resource.Get());
        if(!compact.entries.empty()) plantCompactBatches_.push_back(std::move(compact));
        else release.add(retireValue, compact.readback.resource.Get());
    }
}

// PT-87: the plants' groups whose compacted size has been read back are copied compacted into a buffer of that size, which
// is their structure from here on (the frame's refit, recorded after this, is the first in it; the top level names it). The
// full-size one goes when the GPU is past this frame. The same triangles in the same tree: no ray can tell.
// PT-125 (scene.hpp structureNote): one step of the plants' work recorded, and its number marked on the list after it.
// kind: 1 a group's structure copied to its compacted size, 3 a group built, 4 refitted, 10 + n the pose pass n (bytes:
// its dispatches). x, z: the group's origin, metres.
void GpuScene::plant_step(uint8_t kind, double x, double z, uint64_t triangles, uint64_t bytes, uint64_t scratch) {
    auto& f=plantStepFrames_[plantStepAt_];
    // (PT-99, for the log's "PT plants' plan 10s": what these marks cost the CPU that records: two markers on the list
    // after every dispatch and every refit of every group, every frame. Timed in one frame of 64 only: two clock reads
    // a mark in every frame would themselves be felt.)
    const bool timed=(swayFrame_&63u)==0;
    const auto began=timed ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
    f.steps.push_back(PlantStep{kind, int32_t(x), int32_t(z), uint32_t(triangles), bytes, scratch});
    if(structureNote) structureNote(uint32_t(f.steps.size()));
    if(timed) { plantMarksMs_+=std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now()-began).count(); ++plantMarks_; }
}
std::string GpuScene::describe_plant_steps(uint32_t frame24, uint32_t done, bool sameFrame) const {
    for(const auto& f:plantStepFrames_) {
        if(f.frame==~0u || (f.frame&0xFFFFFFu)!=frame24) continue;
        auto name=[](const PlantStep& s) {
            char b[200];
            if(s.kind>=20) std::snprintf(b, sizeof b, "the pose pass %u's dispatch for the group at x %d z %d (%u vertices or indices, %llu members, its buffer %llu bytes)",
                                         unsigned(s.kind-20), s.x, s.z, s.triangles, (unsigned long long)s.bytes, (unsigned long long)s.scratch);
            else if(s.kind>=10) std::snprintf(b, sizeof b, "the end of the pose pass %u (%llu dispatches)", unsigned(s.kind-10), (unsigned long long)s.bytes);
            else std::snprintf(b, sizeof b, "%s the group at x %d z %d (%u triangles, its structure %llu bytes, scratch %llu)",
                               s.kind==1 ? "the compacting copy of" : s.kind==3 ? "the BUILD of" : "the refit of", s.x, s.z, s.triangles,
                               (unsigned long long)s.bytes, (unsigned long long)s.scratch);
            return std::string(b);
        };
        uint32_t copies=0, passes=0, builds=0, refits=0;
        uint32_t dispatches=0;
        for(const auto& s:f.steps) { if(s.kind==1) ++copies; else if(s.kind>=20) ++dispatches; else if(s.kind>=10) ++passes; else if(s.kind==3) ++builds; else ++refits; }
        const uint32_t n=sameFrame ? done : 0u; // (a mark of an earlier frame: none of this frame's steps ended)
        std::string t="the plants' work of that frame was "+std::to_string(f.steps.size())+" steps ("+std::to_string(copies)+" compacting copies, "+
            std::to_string(passes)+" pose passes of "+std::to_string(dispatches)+" dispatches, "+std::to_string(builds)+" builds, "+std::to_string(refits)+
            " refits), of which the GPU finished "+std::to_string(n);
        // Was the group whose step did not end compacted in this very frame?
        if(n<f.steps.size() && f.steps[n].kind!=1) {
            bool compactedNow=false;
            for(const auto& s:f.steps) compactedNow|=s.kind==1 && s.x==f.steps[n].x && s.z==f.steps[n].z;
            t+=compactedNow ? " (THAT GROUP'S STRUCTURE WAS COMPACTED IN THIS FRAME)" : " (that group's structure was not compacted in this frame)";
        }
        if(n<f.steps.size()) {
            t+="; THE STEP THAT DID NOT END: number "+std::to_string(n+1)+", "+name(f.steps[n]);
            if(n) t+="; the one before it: "+name(f.steps[n-1]);
            if(n+1<f.steps.size()) t+="; the one after it: "+name(f.steps[n+1]);
        } else t+=": all of them (what did not end comes after them: the last barriers, the groups' instances)";
        return t;
    }
    return "the plants' steps of that frame are not kept";
}

void GpuScene::compact_plants(ID3D12Device5* device, ID3D12GraphicsCommandList4* list, DeferredRelease& release, uint64_t retireValue, Barriers& barriers) {
    uint64_t before=0, after=0; uint32_t count=0;
    while(!plantCompactBatches_.empty() && plantCompactBatches_.front().fence<=completed_) {
        auto& batch=plantCompactBatches_.front(); const auto* sizes=static_cast<const uint64_t*>(batch.readback.mapped);
        for(size_t i=0;i<batch.entries.size();++i) {
            const auto found=plantGroups_.find(batch.entries[i].cell);
            if(found==plantGroups_.end()) continue;
            PlantGroup& g=found->second;
            const uint64_t bytes=(sizes[i]+255)&~255ull;
            // (Another group in the cell since, or nothing to gain: a buffer is at least 64 KB, as the device counts it.)
            if(g.serial!=batch.entries[i].serial || !g.built || g.compacted || !g.compactable || !g.blas || !bytes) continue;
            g.compacted=true; // asked once, whatever comes of it
            if(((bytes+65535)&~65535ull)>=allocation_bytes(g.blas.resource.Get())) continue;
            Buffer compact;
            try {
                compact=make_buffer(device, bytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                                    D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, "Create a group of plants' compacted BLAS");
            } catch(const std::exception& e) { log_throttled(std::string("A group of plants' structure could not be compacted: ")+e.what()); continue; }
            list->CopyRaytracingAccelerationStructure(compact.va(), g.blas.va(), D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE_COMPACT);
            plant_step(1, g.origin[0], g.origin[2], g.triangles, bytes, g.blas.size); // (PT-125; "scratch": the size it was built with)
            before+=allocation_bytes(g.blas.resource.Get()); after+=allocation_bytes(compact.resource.Get()); ++count;
            { auto& w=structureWork[kStructureCompaction]; ++w.builds; w.triangles+=g.triangles; w.bytes+=compact.size; }
            release.add(retireValue, g.blas.resource.Get());
            // (PT-109's partitioned top level needs no word of this: a group's instance is among those it writes again every
            // frame, after megaStill, never one of its kept slots.)
            g.blas=std::move(compact);
        }
        release.add(retireValue, batch.readback.resource.Get());
        plantCompactBatches_.pop_front();
    }
    if(!count) return;
    barriers.uav(nullptr); barriers.flush(list);
    plantCompactBefore_+=before; plantCompactAfter_+=after; plantCompacted_+=count;
    // In the log at most every 5 s (a town makes 600 groups over some dozens of frames); every time for the check.
    static uint64_t toldAt=0;
    const uint64_t now=GetTickCount64();
    if(cutsTest || now-toldAt>=5000) {
        toldAt=now;
        log("The plants' structures compacted: "+std::to_string(count)+" groups, "+std::to_string(before)+" -> "+std::to_string(after)+" bytes; since the traced scene started: groups="+
            std::to_string(plantCompacted_)+" built="+std::to_string(plantCompactBefore_)+" compacted="+std::to_string(plantCompactAfter_)+
            " bytes (the same triangles; refitted to the wind as before)");
    }
}

Buffer* GpuScene::skinned_positions(int32_t handle, uint32_t vertexCount, uint32_t& uav, uint64_t frame, uint32_t& previousUav, bool& fresh, Buffer*& previous,
                                    bool smooth, uint32_t& normalsUav, Buffer*& normals) {
    uav=kInvalid; previousUav=kInvalid; fresh=true; previous=nullptr; normalsUav=kInvalid; normals=nullptr;
    if(handle<=0 || size_t(handle)>meshes_.size()) return nullptr;
    Mesh* m=meshes_[size_t(handle)-1].get();
    if(!m || !m->skinned || m->state!=Mesh::State::Ready || !m->skinPositions || meshRecords_.size()<=size_t(handle) ||
       meshRecords_[size_t(handle)].vertexCount!=vertexCount) return nullptr;
    uav=m->skinUav;
    if(m->skinPrevious) { previousUav=m->skinPreviousUav; previous=&m->skinPrevious; fresh=m->skinFrame==0 || m->skinFrame+1!=frame; }
    m->skinFrame=frame;
    // ON TRIAL (PT-92): its record names its normals in the frames they are written, so a hit never reads another frame's.
    if(smooth && m->skinNormals) { normalsUav=m->skinNormalsUav; normals=&m->skinNormals; }
    const uint32_t named=normals ? m->skinNormalsSrv : kInvalid;
    if(meshRecords_[size_t(handle)].normals!=named) { meshRecords_[size_t(handle)].normals=named; meshRecordsDirty_=true; }
    return &m->skinPositions;
}

void GpuScene::record(ID3D12Device5* device, ID3D12GraphicsCommandList4* list, DescriptorHeap& heap, UploadRing& ring,
                      DeferredRelease& release, uint64_t retireValue, const double origin[3], const double camera[3],
                      const std::vector<TGPT_SkinnedInstance>& skinned) {
#if TGPT_MEGA_GEOMETRY
    std::fill(std::begin(megaCpu_), std::end(megaCpu_), 0.0);
    MegaCpuRecord cpuRecord{megaCpu_, megaTrial};
#endif
    RecordCpuFrame cpuFrame;
    // PT-114 (water_tiles): until this frame's plan of the water has them, nothing is known of where the water's triangles lie.
    for(int slot=0;slot<4;++slot) { waterCover_[slot].clear(); waterCoverKnown_[slot]=false; }
    record_cpu.section("meshes retired and compacted");
    // FOR A CHECK (PT-99, docs/agents/reccpu.md; scene.hpp recordCheck_): asked for by the environment's
    // TGPT_TEST_RECORD_CHECK (the offline test), or by a file named "record-check" beside this DLL (the game is started
    // through Steam, where a process's environment is not easily set). Read once, at the first recorded frame.
    if(!recordCheckRead_) {
        recordCheckRead_=true;
        recordCheck_=GetEnvironmentVariableA("TGPT_TEST_RECORD_CHECK", nullptr, 0)>0;
        if(!recordCheck_) {
            const std::wstring folder=native_root();
            recordCheck_=!folder.empty() && GetFileAttributesW((folder+L"\\record-check").c_str())!=INVALID_FILE_ATTRIBUTES;
        }
        if(recordCheck_) log("PT record check is ON (TGPT_TEST_RECORD_CHECK, or the file record-check beside the DLL): the light grid's cells, the plants' kept list and the "
                             "water's index are also made the old way and compared, every time; the record is slower by that work");
    }
    plantStepAt_=(plantStepAt_+1)%4u; plantStepFrames_[plantStepAt_].frame=noteFrame; plantStepFrames_[plantStepAt_].steps.clear(); // (PT-125)
    ensure_arenas();
    cutsOff=memory2_off(); cutsTest=GetEnvironmentVariableA("TGPT_TEST_MEMORY2_OFF", nullptr, 0)>0;
    orderTest=GetEnvironmentVariableA("TGPT_TEST_SCENE_ORDER", nullptr, 0)>0;
    retire_meshes(heap, release, retireValue, skinned);
    compact_meshes(device,list,retireValue);
    structure_stamp(kStructureCompaction);
    Barriers barriers;
    record_cpu.section("new meshes' uploads and builds");
    // Converted meshes: upload and build, within this frame's budget.
    std::vector<Mesh*> converted;
    {
        std::lock_guard<std::mutex> lock(workMutex_);
        uint64_t triangles=0;
        // PT-98: by the clock (the triangles are memory in flight: kMemoryClockMost).
        const uint64_t builds=by_clock(kBuildsPerFrame), buildTriangles=uint64_t(double(kTrianglesPerFrame)*std::min(clock_, kMemoryClockMost));
        while(!done_.empty() && converted.size()<builds && triangles<buildTriangles) {
            Mesh* m=done_.front(); done_.pop_front();
            if(m->state==Mesh::State::Converted) { converted.push_back(m); triangles+=m->data.indices.size()/3; }
            else if(m->state==Mesh::State::Failed) {
                std::lock_guard<std::mutex> s(statsMutex_); ++stats_.meshesFailed;
                log_throttled("A mesh could not be read back (key "+std::to_string(m->source ? m->source->key : 0)+"); it is left out of the traced scene");
            }
        }
    }
    if(converted.empty() && scratch_) { release.add(retireValue, scratch_.resource.Get()); scratch_=Buffer{}; }
    // The emitters follow the scene: its instances, materials and meshes as they become ready.
    if(!converted.empty() || materialsDirty_ || indicesDirty_) emittersDirty_=true;
    if(!converted.empty()) {
        ++meshEpoch_; // (PT-99: meshes become ready or fail below, and their records are written: the plants' kept list, plan_plants)
        upload_meshes(device, list, heap, release, retireValue, converted, barriers);
        // Scratch for this frame's builds, side by side (scratch_place; a character's is built later, from its first pose). It
        // goes when a frame has no new mesh (above).
        uint64_t scratchTotal=0, scratchLargest=0;
        for(Mesh* m:converted) {
            if(m->skinned) continue;
            std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geos;
            const auto in=blas_inputs(*m, geos);
            D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info{}; device->GetRaytracingAccelerationStructurePrebuildInfo(&in, &info);
            scratchTotal+=scratch_aligned(info.ScratchDataSizeInBytes); scratchLargest=std::max(scratchLargest, info.ScratchDataSizeInBytes);
        }
        const uint64_t scratchWanted=std::max<uint64_t>(std::max(scratchLargest, std::min(scratchTotal, kScratchWindow)), 65536);
        if(scratch_.size<scratchWanted) {
            if(scratch_) release.add(retireValue, scratch_.resource.Get());
            scratch_=make_buffer(device, scratchWanted, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, "Create BLAS scratch");
        }
        Buffer compactSizes=make_buffer(device,converted.size()*8,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,"Create BLAS compact sizes");
        CompactBatch compact; compact.fence=retireValue;
        compact.readback=make_buffer(device,converted.size()*8,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_FLAG_NONE,D3D12_RESOURCE_STATE_COPY_DEST,"Read BLAS compact sizes");
        uint64_t scratchOffset=0;
        // (PT-99: the meshes an instance of HDRP's water names, found once for the frame's new meshes; the scene's whole
        // list was walked for every one of them, 24,000 instances for each of up to hundreds of meshes in a frame.)
        std::vector<int32_t> waterMeshes;
        for(const auto& inst:instances_) if(inst.flags&TGPT_INSTANCE_WATER) waterMeshes.push_back(inst.mesh);
        std::sort(waterMeshes.begin(), waterMeshes.end());
        for(Mesh* m:converted) {
            try {
                build_blas(device,list,*m,scratchOffset,m->skinned ? 0 : compactSizes.va()+compact.entries.size()*8);
                if(!m->skinned) compact.entries.push_back({m->handle,m->blasVa});
                m->geometryBase=uint32_t(geometryRecords_.size());
                for(const auto& g:m->data.geometries) geometryRecords_.push_back(g);
                if(meshRecords_.size()<=size_t(m->handle)) meshRecords_.resize(size_t(m->handle)+1, MeshRecordGpu{kInvalid, kInvalid, kInvalid, kInvalid, 0, 0, 0, 0, kInvalid});
                meshRecords_[size_t(m->handle)]=MeshRecordGpu{m->positionsSrv, m->uvsSrv, m->colorsSrv, m->indicesSrv, m->geometryBase,
                    uint32_t(m->data.geometries.size()), m->data.vertexCount, m->normalsSrv, m->skinned ? m->skinPreviousSrv : kInvalid, m->uvs2Srv, m->tangentsSrv, m->decalColorsSrv};
                m->triangles=m->data.indices.size()/3;
                if(!m->skinned) { auto& w=structureWork[kStructureNewMeshes]; ++w.builds; w.triangles+=m->triangles; w.bytes+=m->blasBytes; }
                m->state=Mesh::State::Ready;
                meshRecordsDirty_=true;
                std::lock_guard<std::mutex> s(statsMutex_);
                ++stats_.meshesReady; stats_.triangles+=m->triangles; stats_.geometryBytes+=m->bytesOnGpu;
            } catch(const std::exception& e) {
                m->state=Mesh::State::Failed;
                log_throttled(std::string("A mesh's acceleration structure could not be built: ")+e.what());
                std::lock_guard<std::mutex> s(statsMutex_); ++stats_.meshesFailed;
            }
            // The CPU copies have served their purpose; the GPU has its own (the geometry records stay). HDRP's water keeps its
            // positions and indices: the water's surface is planned from its triangles every frame (scene_water.cpp).
            if(!m->water && std::binary_search(waterMeshes.begin(), waterMeshes.end(), m->handle)) m->water=true;
#if TGPT_MEGA_GEOMETRY
            // (PT-109 on trial, 5, 7, 8: a mesh a plant of the trial's clusters is made of is cut into clusters here, from these
            // CPU copies before they go; its cut goes on the GPU in mega_prepare, below. Any other is cut later from a read-back.)
            if(mega_clusters_on() && m->state==Mesh::State::Ready && !m->skinned && !m->water &&
               !megaMeshes_.count(m->handle) && !m->data.positions.empty() && !m->data.indices.empty()) {
                bool wanted=false;
                for(const auto& inst:instances_) {
                    if(inst.mesh!=m->handle || (inst.flags&(TGPT_INSTANCE_HIDDEN|TGPT_INSTANCE_STAND_IN))) continue;
                    for(size_t gi=0;gi<m->data.geometries.size() && !wanted;++gi)
                        wanted=inst.materialBase+gi<materialIndices_.size() && plantWindOf_.count(materialIndices_[inst.materialBase+gi]) && mega_cluster_plant(*m, inst.transform);
                    if(wanted) break;
                }
                if(wanted && mega_cut_mesh(*m, m->data.positions.data(), m->data.indices.data(), m->data.indices.size())) megaWanted_.insert(m->handle);
            }
#endif
            if(!m->water) { m->data.positions=std::vector<float>(); m->data.indices=std::vector<uint32_t>(); }
            m->data.uvs=std::vector<float>(); m->data.colors=std::vector<uint32_t>();
            m->data.normals=std::vector<uint32_t>(); m->data.uvs2=std::vector<float>(); m->data.tangents.clear(); m->data.tangents.shrink_to_fit(); m->data.decalColors.clear(); m->data.decalColors.shrink_to_fit();
            m->source.reset();
        }
        barriers.uav(nullptr); barriers.transition(compactSizes,D3D12_RESOURCE_STATE_COPY_SOURCE); barriers.flush(list);
        list->CopyResource(compact.readback.resource.Get(),compactSizes.resource.Get());
        release.add(retireValue,compactSizes.resource.Get());
        if(!compact.entries.empty()) compactBatches_.push_back(std::move(compact));
        else release.add(retireValue,compact.readback.resource.Get());
    }
    structure_stamp(kStructureNewMeshes);
    pending_.erase(std::remove_if(pending_.begin(), pending_.end(), [](Mesh* m){ return m->state==Mesh::State::Ready || m->state==Mesh::State::Failed; }), pending_.end());
    {
        std::lock_guard<std::mutex> s(statsMutex_);
        stats_.meshesQueued=uint32_t(pending_.size());
    }
    record_cpu.section("decals");
    // The game's own textures (PT-46): each material's handles become the numbers their textures have in the heap this
    // frame, or -1 (the atlas tiles), checked again whenever a texture came or died (share::generation). A texture that died
    // is named by no frame recorded after the registry learned of it, which is what lets the registry free it.
    if(!converted.empty()) decalsDirty_=true;
    upload_decals(device, list, heap, ring, release, retireValue, origin, barriers);
    // (The offline check of PT-134 alone, orderTest: each mesh decal's top level that a record names, with its mesh's bottom
    // level's address as it is now and the address the top level was built with, said whenever any of it changes. Read on
    // the CPU side after upload_decals, whatever way that took out: the check sees a top level that names a place its
    // mesh has left in the very frame it happens, and no ray has to go there to show it.)
    if(orderTest) {
        uint32_t named=0, stale=0; std::string each;
        for(size_t i=0;i<decalMeshes_.size();++i) {
            const DecalMesh& cache=decalMeshes_[i];
            if(!cache.mesh) continue;
            const Mesh* mesh=cache.mesh<=meshes_.size() ? meshes_[cache.mesh-1].get() : nullptr;
            const uint64_t now=mesh && mesh->state==Mesh::State::Ready ? uint64_t(mesh->blasVa) : 0ull, built=uint64_t(cache.instance.AccelerationStructure);
            ++named; if(now!=built) ++stale;
            char part[160]; std::snprintf(part, sizeof part, " | decal %zu mesh %u at 0x%llx built with 0x%llx", i, cache.mesh, (unsigned long long)now, (unsigned long long)built);
            each+=part;
        }
        const std::string said="PT test, mesh decals' top levels: "+std::to_string(named)+" named by a record, "+std::to_string(stale)+
            " built with an address that is not their mesh's now"+each;
        static std::string told;
        if(said!=told) { told=said; log(said); }
    }
    structure_stamp(kStructureDecals);
    record_cpu.section("materials' textures and programs");
    const uint64_t shareGeneration=share::generation();
    const bool materialsChanged=materialsDirty_ || !materialBuffer_.buffer; // read before the upload below makes the buffer
    // The numbers the materials' textures have this frame were looked up again, or the frame reads the game's own textures
    // where the last did not: what a cut-out's alpha test reads may have changed with it (the signatures below).
    const bool texturesChanged=materialsChanged || shareGeneration!=shareGeneration_ || sharedTextures!=sharedLast_;
    if(materialsDirty_ || !materialBuffer_.buffer || shareGeneration!=shareGeneration_) {
        shareGeneration_=shareGeneration;
        gpuMaterials_=materials_;
        // After the scene's own, one material of no kind (no glass, no cut-out, no light, no texture): the water's surface
        // names it, so a ray that asks a hit for its material's kind finds none (its optics are the water path's own).
        // Decals leave it alone (TGPT_MATERIAL_NO_DECALS: HDRP's water takes none of its decal buffer either), which is no kind.
        { TGPT_Material none{}; none.albedoTile=none.emissiveTile=none.maskTile=none.alphaTile=-1; none.colorTexture=none.normalTexture=none.maskTexture=-1;
          none.flags=TGPT_MATERIAL_NO_DECALS;
          gpuMaterials_.push_back(none); }
        share::resolve(gpuMaterials_.data(), gpuMaterials_.size());
        upload_structured(device, list, heap, ring, release, retireValue, materialBuffer_, gpuMaterials_.data(), uint32_t(gpuMaterials_.size()), sizeof(TGPT_Material), barriers);
    }
    // Hair's and eyes' map records, their handles resolved the same way.
    if(!mapRecords_.empty() && (mapRecordsDirty_ || shareGeneration!=mapShareGeneration_ || !mapRecordsBuffer_.buffer)) {
        mapShareGeneration_=shareGeneration; mapRecordsDirty_=false;
        gpuMapRecords_=mapRecords_;
        std::vector<int32_t> handles; std::vector<size_t> at;
        auto slot=[&](size_t i) { if(gpuMapRecords_[i]!=0xFFFFFFFFu) { at.push_back(i); handles.push_back(int32_t(gpuMapRecords_[i])); } };
        for(uint32_t d=0;d<kMaxDetailTiles;++d) { const size_t r=size_t(d)*kDetailRecordRows*4; for(size_t k=0;k<5;++k) slot(r+k); }
        for(uint32_t t=0;t<kMaxTiles;++t) { const size_t r=(size_t(kLobeRecordBase)+size_t(t)*kLobeRecordRows)*4; for(size_t k=0;k<3;++k) slot(r+k); }
        share::resolve(handles.data(), handles.size());
        for(size_t i=0;i<at.size();++i) gpuMapRecords_[at[i]]=uint32_t(handles[i]);
        upload_structured(device, list, heap, ring, release, retireValue, mapRecordsBuffer_, gpuMapRecords_.data(), kMapRecordRows, 16, barriers);
    }
    // The material programs' records (PT-64): a row of globals, then the plugin's rows, each record's texture handles
    // resolved to this frame's numbers (its first row names how many: tgpt_api.h TGPT_PROGRAM_*).
    if(programsDirty_ || shareGeneration!=programShareGeneration_ || !programBuffer_.buffer) {
        programShareGeneration_=shareGeneration; programsDirty_=false;
        gpuProgramRows_.assign(4, 0u);
        std::memcpy(gpuProgramRows_.data(), &programSaturation_, 4);
        gpuProgramRows_.insert(gpuProgramRows_.end(), programRows_.begin(), programRows_.end());
        std::vector<int32_t> handles; std::vector<size_t> at;
        const size_t rows=programRows_.size()/4;
        for(size_t r=0;r<rows;) {
            const uint32_t* head=&programRows_[r*4];
            if(head[0]>=TGPT_PROGRAM_COUNT || head[1]>TGPT_PROGRAM_MAX_TEXTURES) { ++r; continue; } // no record starts here
            const size_t textureRows=(size_t(head[1])+3)/4, valueRows=head[2];
            if(r+1+textureRows+valueRows>rows) break;
            for(uint32_t k=0;k<head[1];++k) { at.push_back(4+(r+1)*4+k); handles.push_back(int32_t(programRows_[(r+1)*4+k])); }
            r+=1+textureRows+valueRows;
        }
        share::resolve(handles.data(), handles.size());
        for(size_t i=0;i<at.size();++i) gpuProgramRows_[at[i]]=uint32_t(handles[i]);
        upload_structured(device, list, heap, ring, release, retireValue, programBuffer_, gpuProgramRows_.data(), uint32_t(gpuProgramRows_.size()/4), 16, barriers);
    }
    if(materialsChanged) materialsDirty_=false;
    if(texturesChanged) {
        sharedLast_=sharedTextures;
        // What a material's micromaps depend on: its alpha, tiling, texture, cutoff and flags. A cut-out whose alpha test reads
        // the game's own texture (shared_cutout): that texture, by its handle. A handle names one texture object for as long
        // as it lives, and mip streaming makes a new object (TextureShare.cs), so micromaps baked from a texture are never
        // used with another: the mesh's are made anew (omm_instance).
        materialSignatures_.resize(materials_.size());
        for(size_t i=0;i<materials_.size();++i) {
            const TGPT_Material& m=materials_[i];
            uint64_t h=1469598103934665603ull;
            auto mix=[&h](const void* p, size_t n) { for(size_t b=0;b<n;++b) { h^=static_cast<const uint8_t*>(p)[b]; h*=1099511628211ull; } };
            mix(&m.baseColor[3], 4); mix(m.uvScaleOffset, 16); mix(&m.albedoTile, 4); mix(&m.alphaCutoff, 4); mix(&m.flags, 4); mix(&m.alphaTile, 4);
            if((m.flags&TGPT_MATERIAL_ALPHA_TEST) && shared_cutout(uint32_t(i))) { const uint32_t own=0x6F776E21u; mix(&own, 4); mix(&m.colorTexture, 4); }
            materialSignatures_[i]=h;
        }
    }
    record_cpu.section("the plants' plan: before it (scratch, Mega Geometry's cuts)");
    // Grass and leaves in the wind (PT-63): the instances traced in a group's pose this frame, and the groups.
    ++swayFrame_;
    if((swayFrame_&63u)==0) ++plantMarkFrames_; // (plant_step times its marks in these frames)
    // A stage's scratch four times larger than any frame of the last 256 asked of it goes (the characters left, the wind
    // stopped): the next build makes one that fits. Looked at this seldom, a buffer is not made and dropped frame by frame
    // where a frame's need swings (a character is rebuilt one frame in four).
    if((swayFrame_&255u)==0) {
        const std::pair<Buffer*, uint64_t*> stages[]={{&skinScratch_, &skinScratchPeak_}, {&plantScratch_, &plantScratchPeak_}, {&swayScratch_, &swayScratchPeak_}};
        for(const auto& [buffer, peak]:stages) {
            if(*buffer && buffer->size>std::max<uint64_t>(*peak*4, 1ull<<20)) { release.add(retireValue, buffer->resource.Get()); *buffer=Buffer{}; }
            *peak=0;
        }
    }
    std::vector<uint8_t> grouped(instances_.size(), 0);
    std::vector<PlantUse> plantUses;
#if TGPT_MEGA_GEOMETRY
    mega_prepare(device, list, heap, release, retireValue, barriers); // (PT-109 on trial, 5: the small plants' meshes cut into clusters)
#endif
    {
#if TGPT_MEGA_GEOMETRY
        MegaCpuTimer planTimer(megaCpu_[0]);
#endif
        plan_plants(device, heap, release, retireValue, grouped, plantUses);
    }
    record_cpu.section("the material index list (moving things, characters, plants, water) and its upload");
    // The things the game moves this frame (TGPT_SetMoving): the scene's instances they stand in for are not traced (their
    // records stay, so every other record keeps its number), and the emitters are built anew without them when that set
    // of glowing instances changes (their light would stay where the scene's list has them).
    replaced_.assign(instances_.size(), 0);
    uint32_t movingReplaced=0;
    {
        uint64_t glowHash=1469598103934665603ull;
        if(moving_) for(uint32_t index:moving_->replaced) {
            if(index>=instances_.size() || replaced_[index]) continue;
            replaced_[index]=1; ++movingReplaced;
            const auto& inst=instances_[index];
            if(inst.mesh<=0 || size_t(inst.mesh)>=meshRecords_.size()) continue;
            const uint32_t geometryCount=meshRecords_[size_t(inst.mesh)].geometryCount;
            bool glows=false;
            for(uint32_t gi=0;gi<geometryCount && !glows;++gi) {
                const uint64_t at=uint64_t(inst.materialBase)+gi;
                if(at>=materialIndices_.size() || materialIndices_[at]>=materials_.size()) break;
                const TGPT_Material& mat=materials_[materialIndices_[at]];
                glows=mat.emissive[0]>0.f || mat.emissive[1]>0.f || mat.emissive[2]>0.f;
            }
            if(glows) glowHash=mix64(glowHash, index);
        }
        if(glowHash!=replacedGlowHash_) { replacedGlowHash_=glowHash; emittersDirty_=true; }
    }
    // Characters' materials go after the static list, freshly each frame they are traced; the groups' after them. The things
    // the game moves first (TGPT_Moving.materialBase counts from the end of the static list).
    const uint32_t staticIndexCount=uint32_t(materialIndices_.size());
    std::vector<uint32_t> characterBase(skinned.size(), kInvalid);
    const bool movingIndices=moving_ && !moving_->materialIndices.empty();
    const bool waterWanted=sceneHasWater_ && waterPass;
    const bool extraIndices=!skinned.empty() || !plantUses.empty() || movingIndices || waterWanted;
    if(extraIndices) combinedIndices_=materialIndices_;
    const uint32_t movingBase=staticIndexCount;
    if(movingIndices) combinedIndices_.insert(combinedIndices_.end(), moving_->materialIndices.begin(), moving_->materialIndices.end());
    if(!skinned.empty()) {
        for(size_t i=0;i<skinned.size();++i) {
            const auto& s=skinned[i];
            if(s.mesh<=0 || size_t(s.mesh)>=meshRecords_.size()) continue;
            const uint32_t geometries=meshRecords_[size_t(s.mesh)].geometryCount;
            characterBase[i]=uint32_t(combinedIndices_.size());
            for(uint32_t g=0;g<geometries;++g) {
                const uint32_t n=std::clamp(s.submeshCount, 1u, TGPT_SKINNED_MATERIALS);
                combinedIndices_.push_back(s.materials[std::min(g, n-1)]);
            }
        }
    }
    for(auto& u:plantUses) {
        u.materialBase=uint32_t(combinedIndices_.size());
#if TGPT_MEGA_GEOMETRY
        // (PT-109 on trial: a group of clusters names a material a cluster, its records' order.)
        if(u.group->clusters) { combinedIndices_.insert(combinedIndices_.end(), u.group->geometryMaterials.begin(), u.group->geometryMaterials.end()); continue; }
#endif
        combinedIndices_.insert(combinedIndices_.end(), u.group->rangeMaterials.begin(), u.group->rangeMaterials.end());
    }
    // The water's surface: every unit names the material of no kind (after the scene's own, above). A unit is a submesh of
    // its group (PT-99, scene.hpp WaterGroup), and a hit reads its material at the group's base plus the submesh's number:
    // as many entries of that one material as a group can have units, shared by every group.
    const uint32_t waterMaterialBase=uint32_t(combinedIndices_.size());
    if(waterWanted) combinedIndices_.insert(combinedIndices_.end(), kWaterGroupMembers, uint32_t(materials_.size()));
    if(indicesDirty_ || !indexListBuffer_.buffer || extraIndices || combinedLast_) {
        const auto& list_=extraIndices ? combinedIndices_ : materialIndices_;
        upload_structured(device, list, heap, ring, release, retireValue, indexListBuffer_, list_.data(), uint32_t(list_.size()), 4, barriers);
        indicesDirty_=false; combinedLast_=extraIndices;
    }
    record_cpu.section(kInstancesLoop);
    // Instances whose mesh is ready, relative to the origin; each one's record is its TLAS instance ID.
    std::vector<D3D12_RAYTRACING_INSTANCE_DESC> descs; descs.reserve(instances_.size());
    std::vector<InstanceRecordGpu> records; records.reserve(instances_.size());
    std::vector<uint32_t> sources; sources.reserve(instances_.size()); // each record's instance (instances_ index)
    // PT-109 on trial (mega_geometry.hpp): this frame's top level is a partitioned one, where the build has it, the
    // plugin's hidden setting asks for it (1, 2 or 3: its cells' size), the driver reports it and no call of it failed. A partitioned top level has no
    // "no micromaps" instance flag: an instance that goes without its mesh's micromaps is given the mesh's plain
    // bottom level, the same triangles, tested the same way (the rule under either top level since PT-110's M5).
#if TGPT_MEGA_GEOMETRY
    const bool megaTopLevel=((megaTrial>=1 && megaTrial<=3) || megaTrial==7 || megaTrial==8) && megaCaps.partitionedTop && !megaTop_.failed();
    // (PT-110: the "Micromaps trial" changes the flags of instances a partitioned top level keeps from frame to frame:
    // when it changes, that structure goes and is written whole at its next use.)
    if(ommTrial!=ommTrialLast_ && megaTop_.bytes()) { megaTop_.release(release, retireValue); megaFresh_.clear(); }
#endif
    ommTrialLast_=ommTrial;
    // FOR MEASURING (PT-110, docs/agents/omm-1.md; the plugin's hidden "Micromaps trial", tgpt_api.h TGPT_OMM_TRIAL_*): a
    // kind of cut-out made solid for a measuring step, to every ray but the camera's own (which makes every triangle a
    // candidate by its ray's flag, whatever an instance says). THE PICTURE IS WRONG while one of these is set: shadows,
    // bounced light and reflections take leaves, hair and fences for whole cards. 0, as it ships: none of them.
    const bool plantsSolid=(ommTrial&TGPT_OMM_TRIAL_PLANTS_SOLID)!=0, charactersSolid=(ommTrial&TGPT_OMM_TRIAL_CHARACTERS_SOLID)!=0,
               stillSolid=(ommTrial&TGPT_OMM_TRIAL_STILL_SOLID)!=0;
    // PT-110, for the log alone ("PT cut-outs", SceneStats::cutouts): an alpha-tested instance's triangles into its kind's
    // count. mapped: it traces with its mesh's micromaps this frame. marked: its own structure marks its plain submeshes
    // solid (M3: a character's), so they are spared the shader's test.
    CutoutCount cut[kCutKinds];
    auto count_cutouts=[&](CutoutCount& c, const Mesh& mesh, uint32_t handle, const uint32_t* mats, bool mapped, bool marked=false) {
        if(size_t(handle)>=meshRecords_.size()) return;
        const MeshRecordGpu& own=meshRecords_[size_t(handle)];
        uint64_t cutouts=0, plain=0, hair=0;
        for(uint32_t gi=0;gi<own.geometryCount && uint64_t(own.geometryBase)+gi<geometryRecords_.size();++gi) {
            const uint64_t triangles=geometryRecords_[own.geometryBase+gi].indexCount/3;
            const uint32_t flags=mats[gi]<materials_.size() ? materials_[mats[gi]].flags : 0u;
            if(!(flags&TGPT_MATERIAL_ALPHA_TEST)) { plain+=triangles; continue; }
            cutouts+=triangles;
            if(flags&(TGPT_MATERIAL_STRANDS|TGPT_MATERIAL_HAIR_LAYER)) hair+=triangles;
        }
        ++c.instances; c.triangles+=cutouts; c.hair+=hair;
        if(marked) c.spared+=plain;
        if(!mapped) { if(!marked) c.plain+=plain; return; } // (with micromaps a plain submesh is solid in the structure itself)
        c.mapped+=cutouts;
        if(!mesh.ommCounted) return;
        c.counted+=cutouts; c.settled+=mesh.ommSettledTriangles;
        for(int i=0;i<3;++i) c.pieces[i]+=mesh.ommPieces[i];
        if(mesh.ommLeaf) c.leafUnknown+=mesh.ommPieces[2];
    };
    const uint32_t materialCount=uint32_t(materials_.size()), indexCount=staticIndexCount;
    uint32_t ommInstances=0, swaying=0, cloth=0, seeThrough=0, water=0, hairLayer=0, swayed=0, swayedVertices=0;
    // PT-99 (docs/agents/speed-1.md), counted for the log alone: an instance with one alpha-tested material and no micromaps
    // is traced with every triangle non-opaque (FORCE_NON_OPAQUE below: its BLAS is the mesh's, shared by instances with
    // other materials, so the instance decides), and a ray's query hands every triangle it crosses there to the shader's
    // alpha test, the triangles of its plain submeshes too. Those are counted apart: tested for nothing.
    uint32_t testedInstances=0; uint64_t testedTriangles=0, testedOpaque=0;
    auto count_tested=[&](uint32_t mesh, const uint32_t* mats) {
        if(size_t(mesh)>=meshRecords_.size()) return;
        const MeshRecordGpu& own=meshRecords_[size_t(mesh)];
        ++testedInstances;
        for(uint32_t gi=0;gi<own.geometryCount && uint64_t(own.geometryBase)+gi<geometryRecords_.size();++gi) {
            const uint64_t triangles=geometryRecords_[own.geometryBase+gi].indexCount/3;
            testedTriangles+=triangles;
            if(mats[gi]<materials_.size() && !(materials_[mats[gi]].flags&TGPT_MATERIAL_ALPHA_TEST)) testedOpaque+=triangles;
        }
    };
    record_cpu.section("the cloth's plan");
    // Cloth the wind sways (a material with TGPT_SetClothWind's wind) near the camera, the nearest first: traced in the pose
    // HDRP draws it this frame, from a copy of its own (pt_sway), instead of where it hangs still.
    std::vector<uint8_t> swayChosen(instances_.size(), 0);
    if(!winds_.empty() && swayPass) {
        std::vector<std::pair<double, size_t>> nearby;
        for(size_t source=0;source<instances_.size();++source) {
            const auto& inst=instances_[source];
            if(grouped[source] || replaced_[source] || inst.flags&(TGPT_INSTANCE_HIDDEN|TGPT_INSTANCE_WATER) || inst.mesh<=0 || size_t(inst.mesh)>meshes_.size()) continue;
            Mesh* m=meshes_[size_t(inst.mesh)-1].get();
            if(!m || m->state!=Mesh::State::Ready || m->skinned || meshRecords_.size()<=size_t(inst.mesh)) continue;
            const uint32_t geometryCount=meshRecords_[size_t(inst.mesh)].geometryCount;
            if(uint64_t(inst.materialBase)+geometryCount>materialIndices_.size()) continue;
            bool windy=false;
            for(uint32_t gi=0;gi<geometryCount && !windy;++gi) {
                // (winds_.count's answer, from set_wind's table.)
                const uint32_t material=materialIndices_[inst.materialBase+gi];
                windy=material<clothWindy_.size() ? clothWindy_[material]!=0 : (clothWindPastTable_ && winds_.count(material)!=0);
            }
            if(!windy) continue;
            const double dx=double(inst.transform[3])-camera[0], dy=double(inst.transform[7])-camera[1], dz=double(inst.transform[11])-camera[2];
            nearby.emplace_back(std::sqrt(dx*dx+dy*dy+dz*dz), source);
        }
        std::sort(nearby.begin(), nearby.end());
        uint32_t vertices=0, chosen=0;
        for(const auto& [d, source]:nearby) {
            const uint32_t n=meshRecords_[size_t(instances_[source].mesh)].vertexCount;
            if(chosen>=kMaxSwayCopies || vertices+n>kSwayVertexBudget) continue;
            swayChosen[source]=1; vertices+=n; ++chosen;
        }
    }
    record_cpu.section(kInstancesLoop);
    std::vector<std::pair<SwayCopy*, size_t>> swayWork;
    swayRecords_.clear();
    std::vector<WaterRecordGpu> waterRecords;
    std::vector<uint32_t> waterSources; // each one's instance (instances_ index)
    uint32_t instancesWaiting=0;        // listed, and left out of this frame: their mesh is not built yet (PT-98)
    for(size_t source=0;source<instances_.size();++source) {
        const auto& inst=instances_[source];
        if(inst.flags&TGPT_INSTANCE_HIDDEN) continue;
        if(grouped[source]) continue; // a plant in the wind: traced in its group's pose (below), not where it stands still
        if(inst.mesh<=0 || size_t(inst.mesh)>meshes_.size()) continue;
        Mesh* m=meshes_[size_t(inst.mesh)-1].get();
        if(!m || m->state!=Mesh::State::Ready) { if(m && m->state!=Mesh::State::Failed) ++instancesWaiting; continue; }
        // Every submesh needs a material index, and every index a material.
        const uint32_t geometryCount=meshRecords_[size_t(inst.mesh)].geometryCount;
        if(uint64_t(inst.materialBase)+geometryCount>indexCount) continue;
        bool materialsOk=true;
        for(uint32_t gi=0;gi<geometryCount;++gi) if(materialIndices_[inst.materialBase+gi]>=materialCount) { materialsOk=false; break; }
        if(!materialsOk) continue;
        D3D12_RAYTRACING_INSTANCE_DESC d{};
        bool finite=true;
        for(int r=0;r<3;++r) for(int c=0;c<4;++c) {
            float v=inst.transform[r*4+c];
            if(c==3) v=float(double(v)-origin[r]);
            if(!std::isfinite(v)) finite=false;
            d.Transform[r][c]=v;
        }
        if(!finite) continue;
        d.InstanceID=uint32_t(records.size())&0xFFFFFF;
        // Stood in for by a thing the game moves this frame (TGPT_SetMoving, traced below where HDRP draws it): not traced
        // here. Its record stays, so every other record keeps its number (the emitters' instance table is by record).
        if(replaced_[source]) {
            records.push_back(InstanceRecordGpu{uint32_t(inst.mesh), inst.materialBase, inst.flags, kInvalid, inst.renderingLayerMask});
            sources.push_back(uint32_t(source));
            continue;
        }
        // HDRP's water: its own mask bit alone, solid (the water query forces it so anyway), never a micromap's.
        if(inst.flags&TGPT_INSTANCE_WATER) {
            WaterRecordGpu wr{};
            std::memcpy(wr.transform, d.Transform, sizeof(wr.transform));
            wr.mesh=uint32_t(inst.mesh); wr.instance=d.InstanceID; wr.slot=(inst.flags>>8)&3u;
            wr.cell=waterSlots_[wr.slot].live ? waterSlots_[wr.slot].cell : 0.f;
            waterRecords.push_back(wr); waterSources.push_back(uint32_t(source));
            d.InstanceMask=kMaskWater;
            // (M5, PT-110: its mesh's plain bottom level, which holds no micromaps; under either top level.)
            d.Flags=D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE|D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_OPAQUE;
            d.AccelerationStructure=m->blasVa;
            descs.push_back(d);
            records.push_back(InstanceRecordGpu{uint32_t(inst.mesh), inst.materialBase, inst.flags&(TGPT_INSTANCE_WATER|0x300u), kInvalid, inst.renderingLayerMask});
            sources.push_back(uint32_t(source));
            ++water;
            continue;
        }
        // A swaying cloth in its own pose: still to every ray (it lies where HDRP draws it), its own BLAS and mesh record.
        SwayCopy* copy=nullptr;
        if(swayChosen[source]) {
            uint64_t key=1469598103934665603ull;
            auto mix=[&key](const void* p, size_t n) { for(size_t b=0;b<n;++b) { key^=static_cast<const uint8_t*>(p)[b]; key*=1099511628211ull; } };
            mix(&inst.mesh, 4); mix(&inst.materialBase, 4); mix(inst.transform, sizeof(inst.transform));
            auto& c=swayCopies_[key];
            if(!c.positions || c.mesh!=inst.mesh) {
                try {
                    if(c.positions) release_sway_copy(c, &heap, release, retireValue);
                    c=SwayCopy{}; c.mesh=inst.mesh; c.materialBase=inst.materialBase;
                    const uint32_t vertexCount=meshRecords_[size_t(inst.mesh)].vertexCount;
                    // This frame's pose, then the previous frame's (pt_sway modes 3 and 4: the camera rays' motion), each on 16 bytes.
                    const uint64_t bytes=std::max<uint64_t>((uint64_t(vertexCount)*12+15)/16*16, 16);
                    c.positions=make_buffer(device, bytes*2, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON, "Create a swaying cloth's positions");
                    c.positionsSrv=raw_srv(device, heap, c.positions.resource.Get(), bytes);
                    c.positionsUav=raw_uav(device, heap, c.positions.resource.Get(), bytes*2);
                    c.previousAt=uint32_t(bytes); c.previousSrv=raw_srv(device, heap, c.positions.resource.Get(), bytes, bytes);
                    std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geos;
                    const auto in=blas_inputs(*m, geos, c.positions.va());
                    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info{}; device->GetRaytracingAccelerationStructurePrebuildInfo(&in, &info);
                    c.blas=make_buffer(device, info.ResultDataMaxSizeInBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                                       D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, "Create a swaying cloth's BLAS");
                    c.scratchBytes=std::max(info.ScratchDataSizeInBytes, info.UpdateScratchDataSizeInBytes);
                    c.updateBytes=info.UpdateScratchDataSizeInBytes;
                } catch(const std::exception& e) {
                    log_throttled(std::string("A swaying cloth's copy could not be made: ")+e.what());
                    release_sway_copy(c, &heap, release, retireValue);
                    c=SwayCopy{};
                }
            }
            if(c.positions && c.blas) { copy=&c; copy->usedFrame=swayFrame_; }
        }
        bool sways=geometryCount>0 && !copy;
        for(uint32_t gi=0;gi<geometryCount && sways;++gi) sways=(materials_[materialIndices_[inst.materialBase+gi]].flags&TGPT_MATERIAL_SWAYS)!=0;
        bool glass=false;
        for(uint32_t gi=0;gi<geometryCount && !glass;++gi) glass=(materials_[materialIndices_[inst.materialBase+gi]].flags&TGPT_MATERIAL_SEE_THROUGH)!=0;
        bool strands=false;
        for(uint32_t gi=0;gi<geometryCount && !strands;++gi) strands=(materials_[materialIndices_[inst.materialBase+gi]].flags&TGPT_MATERIAL_HAIR_LAYER)!=0;
        bool foliage=false;
        for(uint32_t gi=0;gi<geometryCount && !foliage;++gi) foliage=is_foliage(materials_[materialIndices_[inst.materialBase+gi]].flags);
        const uint32_t still=foliage ? kMaskFoliage : kMaskStillPlain;
        d.InstanceMask=(inst.mask ? (inst.mask&0xFF) : 0xFF)&(sways ? kMaskSways : ((glass ? (still|kMaskSeeThrough) : still)|(strands ? kMaskStrands : 0u)));
        if(strands && !sways) ++hairLayer;
        if(sways) ++swaying;
        if(glass && !sways) ++seeThrough;
        for(uint32_t gi=0;gi<geometryCount;++gi) if(materials_[materialIndices_[inst.materialBase+gi]].flags&TGPT_MATERIAL_THIN_CLOTH) { ++cloth; break; }
        // PT-40: a material light walks inside that is drawn from both sides walks only on the pieces of its mesh that
        // close a volume (mark_closed). Said once for such a material on a mesh's submesh: what the game's candles are.
        for(uint32_t gi=0;gi<geometryCount;++gi) {
            const uint32_t mi=materialIndices_[inst.materialBase+gi];
            const TGPT_Material& mat=materials_[mi];
            const uint32_t both=TGPT_MATERIAL_SUBSURFACE|TGPT_MATERIAL_DOUBLE_SIDED;
            const MeshRecordGpu& own=meshRecords_[size_t(inst.mesh)];
            if((mat.flags&both)!=both || uint64_t(own.geometryBase)+gi>=geometryRecords_.size()) continue;
            static std::unordered_set<uint64_t> said;
            uint32_t maskBits=0; std::memcpy(&maskBits, &mat.subsurfaceMask, 4);
            if(!said.insert((uint64_t(mi)<<40)^(uint64_t(uint32_t(inst.mesh))<<8)^(uint64_t(maskBits)<<17)^gi).second) continue;
            const GeometryRecordGpu& geo=geometryRecords_[own.geometryBase+gi];
            const uint32_t all=geo.indexCount/3, solid=std::min(all, geo.flags>>kGeometrySolidShift);
            char text[320];
            std::snprintf(text, sizeof(text), "Subsurface material %u (mask %.2f), drawn from both sides, on mesh %d (key %llu) submesh %u: %u of its %u triangles are in pieces that "
                "close a volume%s", mi, double(mat.subsurfaceMask), int(inst.mesh), static_cast<unsigned long long>(m->source ? m->source->key : 0), gi, solid, all,
                solid==all ? ": light walks inside all of it" : solid ? ": light walks inside those, the rest is a sheet (lit as a plain surface)" : ": a sheet, no walk (lit as a plain surface)");
            log(text);
        }
        const bool alpha=(inst.flags&TGPT_INSTANCE_ALPHA_TEST)!=0;
        if(copy) {
            // Tested texel by texel where alpha-tested (no micromaps: they were made for the still mesh).
            // (FOR MEASURING, stillSolid: its cut-outs solid for the step.)
            d.Flags=D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE|(alpha && !stillSolid ? D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_NON_OPAQUE : D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_OPAQUE);
            if(alpha) count_tested(uint32_t(inst.mesh), materialIndices_.data()+inst.materialBase);
            if(alpha) count_cutouts(cut[kCutCloth], *m, uint32_t(inst.mesh), materialIndices_.data()+inst.materialBase, false);
            d.AccelerationStructure=copy->blas.va();
            const MeshRecordGpu& own=meshRecords_[size_t(inst.mesh)];
            const uint32_t record=uint32_t(meshRecords_.size()+swayRecords_.size());
            swayRecords_.push_back(MeshRecordGpu{copy->positionsSrv, own.uvs, own.colors, own.indices, own.geometryBase, own.geometryCount, own.vertexCount, own.normals, copy->previousSrv, own.uvs2, own.tangents, own.decalColors});
            descs.push_back(d);
            records.push_back(InstanceRecordGpu{record, inst.materialBase, inst.flags&~TGPT_INSTANCE_SWAYS, kInvalid, inst.renderingLayerMask});
            sources.push_back(uint32_t(source));
            swayWork.emplace_back(copy, source);
            ++swayed; swayedVertices+=own.vertexCount;
            continue;
        }
        // With micromaps the BLAS's own flags decide (its alpha-tested submeshes non-opaque, the rest
        // opaque); an instance whose materials differ from the ones they were made for goes without them.
        // M5 (PT-110, docs/agents/omm-1.md): "without them" is the mesh's plain bottom level, which holds none (the same
        // triangles, tested the same way), under either top level. Until 2026-10-10 it was the micromap one with the
        // instance's "no micromaps" flag, for which every micromap structure had to be built with ALLOW_DISABLE_OMMS, a
        // flag DirectX Raytracing says "may result in some reductions in traversal performance" for every ray that
        // crosses it. No structure is built with it now, and no instance carries that flag.
        const bool omm=alpha && ommEnabled && omm_instance(*m, materialIndices_.data()+inst.materialBase, retireValue) && ommAllowed;
        const bool linked=m->omm==Mesh::Omm::Linked && omm;
        d.Flags=D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE;
        // (FOR MEASURING, stillSolid: its cut-outs solid for the step; with micromaps its bare pieces stay bare, by the
        // rule that an instance's flag acts after the map, and only its unknown pieces turn solid.)
        if(omm) { ++ommInstances; if(stillSolid) d.Flags|=D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_OPAQUE; }
        else d.Flags|=alpha && !stillSolid ? D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_NON_OPAQUE : D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_OPAQUE;
        if(alpha && !omm) count_tested(uint32_t(inst.mesh), materialIndices_.data()+inst.materialBase);
        if(alpha) count_cutouts(cut[kCutStill], *m, uint32_t(inst.mesh), materialIndices_.data()+inst.materialBase, omm);
        d.AccelerationStructure=linked ? m->ommBlasVa : m->blasVa;
        descs.push_back(d);
        records.push_back(InstanceRecordGpu{uint32_t(inst.mesh), inst.materialBase, (inst.flags&~TGPT_INSTANCE_SWAYS)|(sways ? TGPT_INSTANCE_SWAYS : 0u), kInvalid, inst.renderingLayerMask});
        sources.push_back(uint32_t(source));
    }
    record_cpu.section("things the game moves");
#if TGPT_MEGA_GEOMETRY
    // PT-109: the instances so far stand still (but swaying cloth's copies); what follows is moved, posed or refitted
    // each frame, which is what a partitioned top level is told every frame (MegaTopLevel::build).
    const size_t megaStill=descs.size();
#endif
    // The things the game moves (TGPT_SetMoving: a weapon in a hand, a door, a carried lantern), where HDRP draws them this
    // frame: an instance of the mesh's own BLAS each, as still to every ray as anything else, with the transform it had a frame
    // before for its own motion (pt_primary hit_motion: a row of movingPrevious each, relative to this frame's origin). Its
    // glow is met by paths, not sampled as a light: the emitters are built for the scene's list (build_emitters).
    std::vector<float> movingPrevious;
    uint32_t movingTraced=0, movingHidden=0;
    if(moving_) {
        const std::vector<uint32_t>& own=moving_->materialIndices;
        for(const TGPT_Moving& t:moving_->things) {
            if(t.flags&TGPT_INSTANCE_HIDDEN) { ++movingHidden; continue; }
            if(t.mesh<=0 || size_t(t.mesh)>meshes_.size()) continue;
            Mesh* m=meshes_[size_t(t.mesh)-1].get();
            if(!m || m->state!=Mesh::State::Ready || m->skinned) continue;
            const uint32_t geometryCount=meshRecords_[size_t(t.mesh)].geometryCount;
            if(uint64_t(t.materialBase)+geometryCount>own.size()) continue;
            const uint32_t* mats=own.data()+t.materialBase;
            bool materialsOk=true;
            for(uint32_t gi=0;gi<geometryCount;++gi) if(mats[gi]>=materialCount) { materialsOk=false; break; }
            if(!materialsOk) continue;
            D3D12_RAYTRACING_INSTANCE_DESC d{};
            float before[12];
            bool finite=true;
            for(int r=0;r<3;++r) for(int c=0;c<4;++c) {
                float v=t.transform[r*4+c], p=t.previous[r*4+c];
                if(c==3) { v=float(double(v)-origin[r]); p=float(double(p)-origin[r]); }
                if(!std::isfinite(v) || !std::isfinite(p)) finite=false;
                d.Transform[r][c]=v; before[r*4+c]=p;
            }
            if(!finite) continue;
            d.InstanceID=uint32_t(records.size())&0xFFFFFF;
            bool sways=geometryCount>0;
            for(uint32_t gi=0;gi<geometryCount && sways;++gi) sways=(materials_[mats[gi]].flags&TGPT_MATERIAL_SWAYS)!=0;
            bool glass=false;
            for(uint32_t gi=0;gi<geometryCount && !glass;++gi) glass=(materials_[mats[gi]].flags&TGPT_MATERIAL_SEE_THROUGH)!=0;
            bool strands=false;
            for(uint32_t gi=0;gi<geometryCount && !strands;++gi) strands=(materials_[mats[gi]].flags&TGPT_MATERIAL_HAIR_LAYER)!=0;
            bool foliage=false;
            for(uint32_t gi=0;gi<geometryCount && !foliage;++gi) foliage=is_foliage(materials_[mats[gi]].flags);
            const uint32_t still=foliage ? kMaskFoliage : kMaskStillPlain;
            d.InstanceMask=(t.mask ? (t.mask&0xFF) : 0xFF)&(sways ? kMaskSways : ((glass ? (still|kMaskSeeThrough) : still)|(strands ? kMaskStrands : 0u)));
            if(strands && !sways) ++hairLayer;
            if(sways) ++swaying;
            if(glass && !sways) ++seeThrough;
            const bool alpha=(t.flags&TGPT_INSTANCE_ALPHA_TEST)!=0;
            const bool omm=alpha && ommEnabled && omm_instance(*m, mats, retireValue) && ommAllowed;
            const bool linked=m->omm==Mesh::Omm::Linked && omm; // (M5, as above: without micromaps, the plain bottom level)
            d.Flags=D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE;
            if(omm) { ++ommInstances; if(stillSolid) d.Flags|=D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_OPAQUE; } // (FOR MEASURING, as above)
            else d.Flags|=alpha && !stillSolid ? D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_NON_OPAQUE : D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_OPAQUE;
            if(alpha && !omm) count_tested(uint32_t(t.mesh), mats);
            if(alpha) count_cutouts(cut[kCutStill], *m, uint32_t(t.mesh), mats, omm);
            d.AccelerationStructure=linked ? m->ommBlasVa : m->blasVa;
            descs.push_back(d);
            const uint32_t row=uint32_t(movingPrevious.size()/12);
            movingPrevious.insert(movingPrevious.end(), before, before+12);
            records.push_back(InstanceRecordGpu{uint32_t(t.mesh), movingBase+t.materialBase,
                (t.flags&~(TGPT_INSTANCE_SWAYS|TGPT_INSTANCE_CHARACTER|TGPT_INSTANCE_WATER|0x300u))|(sways ? TGPT_INSTANCE_SWAYS : 0u), row, t.renderingLayerMask});
            ++movingTraced;
        }
    }
    if(!movingPrevious.empty()) {
        upload_structured(device, list, heap, ring, release, retireValue, movingPreviousBuffer_, movingPrevious.data(), uint32_t(movingPrevious.size()/4), 16, barriers);
        movingPreviousSrv=movingPreviousBuffer_.srv;
    } else movingPreviousSrv=kInvalid;
    waterRecordCount=uint32_t(waterRecords.size());
    record_cpu.section("the water's records and plan");
    // Uploaded when they changed (the scene's list, the origin, a slot's cell), not every frame.
    if(waterRecordCount && (!waterRecordBuffer_.buffer || waterRecords.size()!=uploadedWaterRecords_.size() ||
                            std::memcmp(waterRecords.data(), uploadedWaterRecords_.data(), waterRecords.size()*sizeof(WaterRecordGpu))!=0)) {
        upload_structured(device, list, heap, ring, release, retireValue, waterRecordBuffer_, waterRecords.data(), waterRecordCount, sizeof(WaterRecordGpu), barriers);
        uploadedWaterRecords_=waterRecords;
    }
    waterRecordsSrv=waterRecordCount ? waterRecordBuffer_.srv : kInvalid;
    // The groups of plants in the wind: each a mesh record of its own after the swaying cloths', its submeshes' index ranges
    // after the registered meshes'.
    plantRecords_.clear(); plantGeometries_.clear();
    for(const auto& u:plantUses) {
        const PlantGroup& g=*u.group;
        // (previous: each vertex's move since the frame before, pose_plants. Until 2026-10-11 it was left out here, so it
        // read 0, not kInvalid, and the camera rays took heap slot 0 for the plants' positions a frame ago.)
        plantRecords_.push_back(MeshRecordGpu{g.positionsSrv, g.uvsSrv, g.colorsSrv, g.indicesSrv, uint32_t(geometryRecords_.size()+plantGeometries_.size()),
                                              uint32_t(g.geometries.size()), g.vertexCount, g.normalsSrv,
                                              g.previousSrv!=kInvalid ? (g.previousSrv|kMeshPreviousDelta) : kInvalid});
        plantGeometries_.insert(plantGeometries_.end(), g.geometries.begin(), g.geometries.end());
    }
    // The water's surface: the units this frame wants (made where new) in their groups (PT-99, scene.hpp WaterGroup), each
    // group a mesh record of its own after the plants' groups'; its units' geometry records (each one's range of the
    // shared pattern) after the plants' groups' submeshes'.
    const uint32_t waterGeometryBase=uint32_t(geometryRecords_.size()+plantGeometries_.size());
    structure_stamp(kStructureOther);
    plan_water(device, list, heap, ring, release, retireValue, waterRecords, waterSources, origin, camera, waterGeometryBase, barriers);
    structure_stamp(kStructureWater);
    plantGeometries_.insert(plantGeometries_.end(), waterGeometries_.begin(), waterGeometries_.end());
    record_cpu.section("the mesh and geometry records' upload");
    auto same=[](const auto& a, const auto& b) { return a.size()==b.size() && (a.empty() || std::memcmp(a.data(), b.data(), a.size()*sizeof(a[0]))==0); };
    // The mesh records, and after them this frame's swaying cloths', the groups' and the water units' (their instances name
    // them by that order).
    if(meshRecordsDirty_ || !meshBuffer_.buffer || !same(swayRecords_, uploadedSwayRecords_) || !same(plantRecords_, uploadedPlantRecords_) ||
       !same(plantGeometries_, uploadedPlantGeometries_) || !same(waterMeshRecords_, uploadedWaterMeshRecords_)) {
        std::vector<MeshRecordGpu> all;
        const std::vector<MeshRecordGpu>* upload=&meshRecords_;
        if(!swayRecords_.empty() || !plantRecords_.empty() || !waterMeshRecords_.empty()) {
            all=meshRecords_; all.insert(all.end(), swayRecords_.begin(), swayRecords_.end()); all.insert(all.end(), plantRecords_.begin(), plantRecords_.end());
            all.insert(all.end(), waterMeshRecords_.begin(), waterMeshRecords_.end());
            upload=&all;
        }
        upload_structured(device, list, heap, ring, release, retireValue, meshBuffer_, upload->data(), uint32_t(upload->size()), sizeof(MeshRecordGpu), barriers);
        std::vector<GeometryRecordGpu> allGeometries;
        const std::vector<GeometryRecordGpu>* geometries=&geometryRecords_;
        if(!plantGeometries_.empty()) { allGeometries=geometryRecords_; allGeometries.insert(allGeometries.end(), plantGeometries_.begin(), plantGeometries_.end()); geometries=&allGeometries; }
        upload_structured(device, list, heap, ring, release, retireValue, geometryBuffer_, geometries->data(), uint32_t(geometries->size()), sizeof(GeometryRecordGpu), barriers);
        meshRecordsDirty_=false; uploadedSwayRecords_=swayRecords_; uploadedPlantRecords_=plantRecords_; uploadedPlantGeometries_=plantGeometries_;
        uploadedWaterMeshRecords_=waterMeshRecords_;
    }
    record_cpu.section("the cloth's pose");
    // This frame's pose of each swaying cloth, and its BLAS refitted to it, before the top level is built.
    structure_stamp(kStructureOther);
    if(!swayWork.empty()) sway(device, list, heap, ring, release, retireValue, swayWork, barriers);
    structure_stamp(kStructureCloth);
    // Copies no instance traced for a while (it moved, changed its LOD or left) go.
    for(auto it=swayCopies_.begin();it!=swayCopies_.end();) {
        if(swayFrame_-it->second.usedFrame>120) { release_sway_copy(it->second, &heap, release, retireValue); it=swayCopies_.erase(it); }
        else ++it;
    }
    // The emissive submeshes among them, before the light grid is built: the clusters sampled as lights are in it.
    record_cpu.section("the emitters' build");
    build_emitters(device, list, heap, ring, release, retireValue, origin, sources, barriers);
    structure_stamp(kStructureOther);
    // The grid follows the camera in 32 m steps and is rebuilt when it moves or a light moves or changes
    // range (positions compared to a quarter metre, which the cells' padding covers); a flame's flicker changes its range
    // with its intensity (the plugin's Reach), so in a town of flames that is every frame.
    // PT-99 (docs/agents/reccpu.md): decided HERE, as soon as everything the grid is made from is this frame's (the lights
    // since intake, the emissive clusters and where their records start since build_emitters just above, the camera), and
    // where it is made anew its build is begun here on another thread, BESIDE the rest of the record (the plants', the
    // water's and the characters' poses: milliseconds in which the CPU that records waits for nothing of the grid). The
    // frame waits for it below, where the grid was built until 2026-10-12, before its lists go up: the same grid, of the
    // same frame, never a frame late. Between here and there nothing reads or writes what the build does (lights_ is
    // only read; gridCells_, gridIndices_, gridWeights_, gridScratch_ and the far lists are the build's alone; the
    // counts for the log go under statsMutex_). The build calls no D3D12 and none of record's clocks.
    record_cpu.section("the light grid: what changed");
    std::future<void> gridJob;
    {
        double corner[3];
        for(int i=0;i<3;++i) corner[i]=std::floor(camera[i]/kGridSnap)*kGridSnap-double(kGridDims[i])*kGridCell*0.5;
        // (The very numbers that were mixed into a hash until 2026-10-12, in that order, kept and compared whole: the
        // same answer without the hash's 48 multiplications a light, and the log can say WHICH of them made a build
        // necessary, "PT light grid 10s". 3 numbers of the corner, 6 a light, 2 of the clusters, 5 a cluster.)
        std::vector<uint64_t>& key=gridKeyNow_;
        key.clear();
        for(double c:corner) key.push_back(uint64_t(int64_t(c)));
        for(const auto& l:lights_) {
            for(float p:l.position) key.push_back(uint64_t(int64_t(std::llround(double(p)*4.0))));
            key.push_back(l.type); key.push_back(uint64_t(std::llround(double(l.range)*16.0))); key.push_back(l.flags&TGPT_LIGHT_NO_RANGE);
        }
        // The emissive clusters sampled as lights, and where their records start.
        key.push_back(clusterBase); key.push_back(clusterLights_.size());
        for(const auto& l:clusterLights_) {
            for(float p:l.position) key.push_back(uint64_t(int64_t(std::llround(double(p)*4.0))));
            key.push_back(uint64_t(std::llround(double(l.range)*16.0))); key.push_back(uint64_t(std::llround(double(l.color[0])*16.0)));
        }
        ++gridWhy_.frames;
        if(!gridBuilt_ || key!=gridKey_) {
            // For the log: what differs from the numbers the grid was last made for.
            ++gridWhy_.builds;
            if(gridBuilt_ && gridKey_.size()>=3) {
                const size_t now=lights_.size(), before=gridKeyLights_;
                if(std::memcmp(key.data(), gridKey_.data(), 3*sizeof(uint64_t))!=0) ++gridWhy_.corner;
                if(now!=before) ++gridWhy_.count;
                else {
                    uint32_t place=0, kind=0, range=0, flag=0;
                    for(size_t i=0;i<now;++i) {
                        const uint64_t* a=&key[3+i*6]; const uint64_t* b=&gridKey_[3+i*6];
                        if(a[0]!=b[0] || a[1]!=b[1] || a[2]!=b[2]) ++place;
                        if(a[3]!=b[3]) ++kind;
                        if(a[4]!=b[4]) ++range;
                        if(a[5]!=b[5]) ++flag;
                    }
                    if(place) ++gridWhy_.place;
                    if(kind) ++gridWhy_.kind;
                    if(range) ++gridWhy_.range;
                    if(flag) ++gridWhy_.flag;
                    gridWhy_.placeMost=std::max(gridWhy_.placeMost, place); gridWhy_.rangeMost=std::max(gridWhy_.rangeMost, range);
                    const size_t tail=3+now*6;
                    if(key.size()!=gridKey_.size() || std::memcmp(key.data()+tail, gridKey_.data()+tail, (key.size()-tail)*sizeof(uint64_t))!=0) ++gridWhy_.clusters;
                }
            } else ++gridWhy_.first;
            std::memcpy(gridJobCorner_, corner, sizeof(gridJobCorner_));
            // An upload buffer of the scene's own for the build to leave its two arrays in (build_light_grid's end): one
            // the GPU is past (the frame that last copied from it is done: its fence against completed_), a quarter
            // larger than the last build's arrays. Three of them, since up to three frames are in flight. Where none
            // is free or one cannot be made, the arrays go through the frame's ring as before.
            gridStage_=-1; gridStaged_=false;
            for(int s=0;s<int(gridStages_.size());++s) if(gridStages_[size_t(s)].fence<=completed_) { gridStage_=s; break; }
            if(gridStage_>=0) {
                GridStage& stage=gridStages_[size_t(gridStage_)];
                const uint64_t wanted=std::max<uint64_t>(gridStageWanted_+gridStageWanted_/4, 1ull<<20);
                if(!stage.buffer || stage.buffer.size<wanted) {
                    if(stage.buffer) release.add(retireValue, stage.buffer.resource.Get());
                    stage.buffer=Buffer{};
                    try { stage.buffer=make_buffer(device, wanted, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, "Create the light grid's upload"); }
                    catch(const std::exception&) { stage.buffer=Buffer{}; gridStage_=-1; }
                }
            }
            gridJob=std::async(std::launch::async, [this] { build_light_grid(gridJobCorner_); });
        }
    }
    record_cpu.section("the plants' compaction");
    // Grass and leaves in the wind: each group posed for this frame and its BLAS refitted, then one instance a group, in
    // the group's own space (its origin moved to the scene's). Still to every ray: it lies where HDRP draws it, so no ray
    // passes it by near its start (kMaskSways, TGPT_INSTANCE_SWAYS: the instances that stand still while HDRP sways them).
    uint32_t plantInstances=0, plantVertices=0, plantBuilt=0; uint64_t plantTriangles=0;
    compact_plants(device, list, release, retireValue, barriers); // PT-87: before this frame's refits, which then go into the compacted ones
    if(!plantUses.empty()) {
        pose_plants(device, list, heap, ring, release, retireValue, plantUses, camera, barriers);
        record_cpu.section("the plants' groups' instances");
        for(size_t i=0;i<plantUses.size();++i) {
            const PlantGroup& g=*plantUses[i].group;
            if(!g.built) continue;
            D3D12_RAYTRACING_INSTANCE_DESC d{};
            d.Transform[0][0]=d.Transform[1][1]=d.Transform[2][2]=1.f;
            for(int r=0;r<3;++r) d.Transform[r][3]=float(g.origin[r]-origin[r]);
            d.InstanceID=uint32_t(records.size())&0xFFFFFF;
            d.InstanceMask=g.foliage ? kMaskFoliage : kMaskStillPlain;
            // Each submesh as its material is: opaque, or tested texel by texel (no micromaps: they are made for a still mesh).
            // (FOR MEASURING, plantsSolid: every submesh solid for the step, whatever its structure says.)
            d.Flags=D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE|(plantsSolid ? D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_OPAQUE : D3D12_RAYTRACING_INSTANCE_FLAG_NONE);
            if(g.alpha) { // (PT-110, for the log; M1: and the members that trace with their meshes' micromaps)
                auto& c=cut[kCutPlants]; c.instances+=g.cutoutMembers; c.triangles+=g.cutoutTriangles; c.plain+=g.plainTested; c.spared+=g.plainSpared;
                for(const auto& [handle, members]:g.mapMeshes) {
                    const Mesh* linked=handle>0 && size_t(handle)<=meshes_.size() ? meshes_[size_t(handle)-1].get() : nullptr;
                    if(!linked) continue;
                    c.mapped+=uint64_t(members)*linked->alphaTriangles;
                    if(!linked->ommCounted) continue;
                    c.counted+=uint64_t(members)*linked->alphaTriangles; c.settled+=uint64_t(members)*linked->ommSettledTriangles;
                    for(int k=0;k<3;++k) c.pieces[k]+=uint64_t(members)*linked->ommPieces[k];
                    if(linked->ommLeaf) c.leafUnknown+=uint64_t(members)*linked->ommPieces[2];
                }
            }
            d.AccelerationStructure=g.blas.va();
#if TGPT_MEGA_GEOMETRY
            // (PT-109 on trial, 7 and 8, NVIDIA's layout: an instance a plant, each with its own bottom level of its clusters, all
            // with the group's record, place, mask and flags: a hit finds its group's records as it did, the cluster's number
            // being its record in the group.)
            if(g.clusters && g.perCopy) for(const auto& c:g.copies) { d.AccelerationStructure=g.blas.va()+c.dest; descs.push_back(d); }
            else
#endif
            descs.push_back(d);
            records.push_back(InstanceRecordGpu{uint32_t(meshRecords_.size()+swayRecords_.size()+i), plantUses[i].materialBase, g.alpha ? TGPT_INSTANCE_ALPHA_TEST : 0u, kInvalid, 0u});
            plantInstances+=uint32_t(g.sources.size()); plantVertices+=g.vertexCount; plantTriangles+=g.triangles; plantBuilt+=plantUses[i].fresh ? 1u : 0u;
        }
    }
    record_cpu.section("the water's pose");
    // The water's surface: its units posed for this frame from the game's wave data and their BLAS refitted, an instance each
    // (kMaskWaves: every ray meets it).
    structure_stamp(kStructurePlants);
    pose_water(device, list, heap, ring, release, retireValue, uint32_t(meshRecords_.size()+swayRecords_.size()+plantRecords_.size()), waterMaterialBase, descs, records, barriers);
    structure_stamp(kStructureWater);
    // Characters: each BLAS rebuilt in place from the positions the skin pass wrote this frame, then an
    // instance like any other.
    record_cpu.section("the characters");
    uint32_t characters=0; uint64_t characterTriangles=0;
    if(!skinned.empty()) {
        std::vector<std::pair<Mesh*, size_t>> todo;
        // Refitted in place to this frame's vertices, or rebuilt from them: the first time, and on its frame of the cycle. Its
        // scratch this frame: a refit's or a build's.
        const auto rebuilt=[](const Mesh& m) { return m.sinceBuild>=kCharacterRebuild-1; };
        // M3 (PT-110, docs/agents/omm-1.md): a character's structure is its own, so each submesh says in it what its
        // material is to a ray: solid, unless the material is alpha-tested (hair, torn cloth, the eyes, the plugin's
        // invisible one). Until 2026-10-10 one alpha-tested material made the whole instance non-opaque, and a ray's
        // query handed every triangle of the body, the armour and the clothes to the shader's alpha test, which read
        // three records to answer "solid" (109,800 such triangles in a tavern's frame). The same hits: the alpha test's
        // first line answers "solid" for every material that is not alpha-tested (pt_common alpha_opaque), and the
        // walks that look for glass, thin cloth and leaves among the candidates take none of these either.
        // caustic_clear explicitly requests candidates when it needs to skip its own glass instance. Glass no longer
        // sends the character's solid skin/armour through every other ray's alpha test. Trial 16 restores the old marks.
        const bool plainTested=(ommTrial&TGPT_OMM_TRIAL_PLAIN_TESTED)!=0;
        const auto character_marks=[&](const uint32_t* mats, uint32_t count) {
            std::vector<uint8_t> marks(count, 0);
            if(!plainTested) for(uint32_t g=0;g<count;++g) marks[g]=(materials_[mats[g]].flags&TGPT_MATERIAL_ALPHA_TEST) ? 0 : 1;
            return marks;
        };
        uint64_t scratchTotal=0, scratchLargest=0;
        for(size_t i=0;i<skinned.size();++i) {
            const auto& s=skinned[i];
            if(characterBase[i]==kInvalid || s.mesh<=0 || size_t(s.mesh)>meshes_.size()) continue;
            Mesh* m=meshes_[size_t(s.mesh)-1].get();
            if(!m || !m->skinned || m->state!=Mesh::State::Ready || !m->blasVa) continue;
            bool materialsOk=true;
            const uint32_t geometries=meshRecords_[size_t(s.mesh)].geometryCount;
            for(uint32_t g=0;g<geometries;++g) if(combinedIndices_[characterBase[i]+g]>=materialCount) { materialsOk=false; break; }
            if(!materialsOk) continue;
            // (M3: its submeshes' marks. A refit must name the marks of its build, so a change of them, as when a
            // material's alpha test is switched, builds the structure anew this frame.)
            if(std::vector<uint8_t> marks=character_marks(combinedIndices_.data()+characterBase[i], geometries); marks!=m->skinOpaque) {
                m->skinOpaque=std::move(marks); m->sinceBuild=kInvalid;
            }
            todo.emplace_back(m, i);
            const uint64_t bytes=rebuilt(*m) ? m->skinScratchBytes : m->skinUpdateBytes;
            scratchTotal+=scratch_aligned(bytes); scratchLargest=std::max(scratchLargest, bytes);
        }
        if(!todo.empty()) {
            // Side by side in the scratch (scratch_place), one barrier after the last.
            fit_scratch(device, release, retireValue, skinScratch_, skinScratchPeak_, scratchTotal, scratchLargest, "characters");
            uint64_t offset=0;
            for(auto& [m, i]:todo) {
                std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geos;
                const auto in=blas_inputs(*m, geos);
                D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC desc{};
                const bool rebuild=rebuilt(*m);
                desc.DestAccelerationStructureData=m->blasVa; desc.Inputs=in;
                desc.ScratchAccelerationStructureData=scratch_place(list, skinScratch_, offset, rebuild ? m->skinScratchBytes : m->skinUpdateBytes);
                if(!rebuild) { desc.Inputs.Flags|=D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE; desc.SourceAccelerationStructureData=m->blasVa; }
                list->BuildRaytracingAccelerationStructure(&desc, 0, nullptr);
                { auto& w=structureWork[kStructureCharacters]; if(rebuild) ++w.builds; else ++w.refits; w.triangles+=m->triangles; w.bytes+=m->blasBytes; }
                m->sinceBuild=rebuild ? (m->sinceBuild==kInvalid ? uint32_t(m->handle)%kCharacterRebuild : 0u) : m->sinceBuild+1;
            }
            barriers.uav(nullptr); barriers.flush(list);
            for(auto& [m, i]:todo) {
                const auto& s=skinned[i];
                D3D12_RAYTRACING_INSTANCE_DESC d{};
                bool finite=true;
                for(int r=0;r<3;++r) for(int c=0;c<4;++c) {
                    float v=s.transform[r*4+c];
                    if(c==3) v=float(double(v)-origin[r]);
                    if(!std::isfinite(v)) finite=false;
                    d.Transform[r][c]=v;
                }
                if(!finite) continue;
                d.InstanceID=uint32_t(records.size())&0xFFFFFF;
                // A character's hair drawn as a layer (its head of hair, its beard, its face with the brows): the bit pt_hair asks for.
                bool strands=false;
                const uint32_t parts=meshRecords_[size_t(s.mesh)].geometryCount;
                for(uint32_t g=0;g<parts && !strands;++g) strands=(materials_[combinedIndices_[characterBase[i]+g]].flags&TGPT_MATERIAL_HAIR_LAYER)!=0;
                d.InstanceMask=kMaskStillPlain|(strands ? kMaskStrands : 0u);
                if(strands) ++hairLayer;
                // A character with an alpha-tested material (hair, torn cloth, the plugin's invisible one) is tested
                // texel by texel through its texture coordinates, like the world's; one without is forced opaque.
                bool alpha=false;
                const uint32_t geometries=meshRecords_[size_t(s.mesh)].geometryCount;
                for(uint32_t g=0;g<geometries && !alpha;++g) alpha=(materials_[combinedIndices_[characterBase[i]+g]].flags&TGPT_MATERIAL_ALPHA_TEST)!=0;
                // M3: where its structure marks its plain submeshes solid (above), the structure's own marks decide, and
                // only its cut-outs are candidates; trial 16 sends its plain submeshes through the test too.
                // (FOR MEASURING, charactersSolid: its cut-outs solid for the step.)
                bool marked=false;
                for(const uint8_t mark:m->skinOpaque) marked|=mark!=0;
                d.Flags=D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE|
                    (!alpha || charactersSolid ? D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_OPAQUE :
                     marked ? D3D12_RAYTRACING_INSTANCE_FLAG_NONE : D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_NON_OPAQUE);
                if(alpha && !marked) count_tested(uint32_t(s.mesh), combinedIndices_.data()+characterBase[i]);
                if(alpha) count_cutouts(cut[kCutCharacters], *m, uint32_t(s.mesh), combinedIndices_.data()+characterBase[i], false, marked);
                d.AccelerationStructure=m->blasVa;
                descs.push_back(d);
                records.push_back(InstanceRecordGpu{uint32_t(s.mesh), characterBase[i], (alpha ? TGPT_INSTANCE_ALPHA_TEST : 0u)|TGPT_INSTANCE_CHARACTER, kInvalid, s.renderingLayerMask});
                ++characters; characterTriangles+=m->triangles;
            }
        }
    }
    structure_stamp(kStructureCharacters);
    record_cpu.section("the instance records' upload");
    upload_structured(device, list, heap, ring, release, retireValue, instanceBuffer_, records.data(), uint32_t(records.size()), sizeof(InstanceRecordGpu), barriers);
    record_cpu.section("the lights' records and the lamps' tree");
    std::vector<TGPT_Light> lights=lights_;
    for(auto& l:lights) for(int i=0;i<3;++i) l.position[i]=float(double(l.position[i])-origin[i]);
    lightCount=uint32_t(lights.size());
    // The lamps' search tree (lamp_tree.hpp; pt_common.hlsli reflected_lamps walks it), in every build: made anew each
    // frame from the very records uploaded, and laid after them in the lights' buffer. The root is record number
    // lightCount, which stays the lights' own count: no other reader goes past the lights.
    {
        const std::vector<TGPT_Light> tree=lamp_tree::build(lights.data(), lights.size());
        lampTreeNodes=tree[0].type;
        lights.insert(lights.end(), tree.begin(), tree.end());
    }
    upload_structured(device, list, heap, ring, release, retireValue, lightBuffer_, lights.data(), uint32_t(lights.size()), sizeof(TGPT_Light), barriers);
    std::vector<TGPT_Light> previous=previousLights_;
    for(auto& l:previous) for(int i=0;i<3;++i) l.position[i]=float(double(l.position[i])-origin[i]);
    upload_structured(device, list, heap, ring, release, retireValue, previousLightBuffer_, previous.data(), uint32_t(previous.size()), sizeof(TGPT_Light), barriers);
    previousLightCount=uint32_t(previous.size());
    previousLights_=lights_;
    // The light grid, where this frame makes it anew (decided and begun after the emitters, above): its build is waited
    // for here, and its lists go up.
    if(gridJob.valid()) {
        record_cpu.section("the light grid: the wait for its build (made beside the record since the emitters)");
        const auto waitBegan=std::chrono::steady_clock::now();
        gridJob.get(); // (what the build threw, if anything, is thrown here)
        gridWhy_.waitedMs+=std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now()-waitBegan).count();
        gridWhy_.cellsMs+=gridJobMs_[0]; gridWhy_.farMs+=gridJobMs_[1];
        std::memcpy(gridCorner_, gridJobCorner_, sizeof(gridCorner_));
        record_cpu.section("the light grid's uploads");
        if(gridStaged_ && gridStage_>=0) {
            // (The build left both arrays in its upload buffer: two copies on the GPU, nothing through the ring. That
            // buffer is not written again until the GPU is past this frame.)
            GridStage& stage=gridStages_[size_t(gridStage_)];
            upload_structured(device, list, heap, ring, release, retireValue, gridCellBuffer_, gridCells_.data(), uint32_t(gridCells_.size()/2), 8, barriers, stage.buffer.resource.Get(), 0);
            upload_light_lists(device, list, heap, ring, release, retireValue, barriers, stage.buffer.resource.Get(), gridStagedEntriesAt_);
            stage.fence=retireValue;
        } else {
            upload_structured(device, list, heap, ring, release, retireValue, gridCellBuffer_, gridCells_.data(), uint32_t(gridCells_.size()/2), 8, barriers);
            upload_light_lists(device, list, heap, ring, release, retireValue, barriers); // (gridIndexBuffer_: the cells' entries, then the far lists')
        }
        gridKey_=gridKeyNow_; gridKeyLights_=lights_.size(); gridBuilt_=true;
    }
    if(const uint64_t tick=GetTickCount64(); !gridWhy_.since) gridWhy_.since=tick;
    else if(tick-gridWhy_.since>=10000) {
        const GridWhy& w=gridWhy_;
        char b[900];
        const double builds=double(std::max(w.builds, 1u));
        std::snprintf(b, sizeof b, "PT light grid 10s: made anew in %u of %llu frames (%zu lights, %zu emissive clusters). Of those builds, what had changed since the grid "
            "was last made: a light's range in %u (in one build at most %u lights'), a light's place in %u (at most %u lights'), a light's kind in %u, its "
            "falls-off-with-distance mark in %u, the lights' count in %u, the emissive clusters in %u, the grid's corner in %u; %u with no grid before. "
            "A build, on its own thread beside the record: the cells %.3f ms, the far lists %.3f ms; the record waited %.3f ms for it",
            w.builds, (unsigned long long)w.frames, lights_.size(), clusterLights_.size(), w.range, w.rangeMost, w.place, w.placeMost, w.kind, w.flag, w.count, w.clusters, w.corner, w.first,
            w.cellsMs/builds, w.farMs/builds, w.waitedMs/builds);
        log(b);
        gridWhy_=GridWhy{}; gridWhy_.since=tick;
    }
    for(int i=0;i<3;++i) gridMin[i]=float(gridCorner_[i]-origin[i]);
    gridCellsSrv=gridCellBuffer_.srv; gridIndicesSrv=gridIndexBuffer_.srv;
    barriers.flush(list);
    structure_stamp(kStructureOther);
    record_cpu.section("the top level");

#if TGPT_MEGA_GEOMETRY
    // PT-109 on trial: the top level partitioned (mega_geometry.hpp): kept from frame to frame, and the driver told only
    // which of the frame's instances changed. 0 when the plain build below must serve this frame (nothing was recorded).
    D3D12_GPU_VIRTUAL_ADDRESS partitioned=0;
    const auto topCpuStart=std::chrono::steady_clock::now();
    if(megaTopLevel) {
        std::vector<D3D12_GPU_VIRTUAL_ADDRESS> restless; // swaying cloth's copies: refitted in place among the still instances
        for(const auto& pair:swayCopies_) if(pair.second.blas) restless.push_back(pair.second.blas.va());
        std::sort(restless.begin(), restless.end());
        std::sort(megaFresh_.begin(), megaFresh_.end());
        MegaTopLevel::Frame did; std::vector<std::string> said;
        const uint32_t grid=megaTrial==1 ? 32u : megaTrial==3 ? 8u : 16u; // the trial's cells: 64, 128 or 256 m (7 and 8: 128 m)
        partitioned=megaTop_.build(device, list, ring, release, retireValue, descs.data(), descs.size(), megaStill, restless, megaFresh_, grid, barriers, did, said);
        for(const auto& line:said) log(line);
        // For the log's "PT structures" line: no build counted; what was told to the driver in `refits`.
        if(partitioned) { auto& w=structureWork[kStructureTopLevel]; w.refits+=did.written+did.updated; w.triangles+=descs.size(); w.bytes+=did.bytes; }
        // Told (or its structure is gone, and the next one is written whole): the list starts over. Kept through a frame
        // the plain build served, for the next the partitioned one does.
        if(partitioned || !megaTop_.bytes()) megaFresh_.clear();
    } else if(megaTop_.bytes()) {
        megaTop_.release(release, retireValue); megaFresh_.clear(); // the trial was switched off: its structure goes
        log("Mega Geometry: the partitioned top level is off again; the plain top level serves");
    }
    if(!partitioned)
#endif
    {
    // The top level: rebuilt each frame (it is small next to the tracing).
    D3D12_GPU_VIRTUAL_ADDRESS descAddress=0;
    if(!descs.empty()) {
        const uint64_t bytes=descs.size()*sizeof(D3D12_RAYTRACING_INSTANCE_DESC);
        void* cpu=nullptr;
        if(!ring.allocate(bytes, 16, &cpu, &descAddress)) {
            Buffer upload=make_buffer(device, bytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, "Create instance upload buffer");
            cpu=upload.mapped; descAddress=upload.va();
            release.add(retireValue, upload.resource.Get());
        }
        std::memcpy(cpu, descs.data(), bytes);
    }
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS in{};
    in.Type=D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    in.Flags=D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    in.NumDescs=UINT(descs.size()); in.DescsLayout=D3D12_ELEMENTS_LAYOUT_ARRAY; in.InstanceDescs=descAddress;
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info{};
    device->GetRaytracingAccelerationStructurePrebuildInfo(&in, &info);
    if(!tlasBuffer_ || tlasBuffer_.size<info.ResultDataMaxSizeInBytes) {
        if(tlasBuffer_) release.add(retireValue, tlasBuffer_.resource.Get());
        tlasBuffer_=make_buffer(device, std::max<uint64_t>(info.ResultDataMaxSizeInBytes*3/2, 1ull<<20), D3D12_HEAP_TYPE_DEFAULT,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, "Create TLAS");
    }
    if(!tlasScratch_ || tlasScratch_.size<info.ScratchDataSizeInBytes) {
        if(tlasScratch_) release.add(retireValue, tlasScratch_.resource.Get());
        tlasScratch_=make_buffer(device, std::max<uint64_t>(info.ScratchDataSizeInBytes*3/2, 1ull<<20), D3D12_HEAP_TYPE_DEFAULT,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, "Create TLAS scratch");
    }
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC desc{};
    desc.DestAccelerationStructureData=tlasBuffer_.va(); desc.ScratchAccelerationStructureData=tlasScratch_.va(); desc.Inputs=in;
    list->BuildRaytracingAccelerationStructure(&desc, 0, nullptr);
    barriers.uav(tlasBuffer_.resource.Get()); barriers.flush(list);
    { auto& w=structureWork[kStructureTopLevel]; ++w.builds; w.triangles+=descs.size(); w.bytes+=info.ResultDataMaxSizeInBytes; }
    } // (the plain top level's block: PT-109's partitioned one, above, stands in for it in a frame it serves)
    structure_stamp(kStructureTopLevel);
#if TGPT_MEGA_GEOMETRY
    megaCpu_[3]=std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now()-topCpuStart).count();
#endif
#if TGPT_MEGA_GEOMETRY
    tlas=partitioned ? partitioned : tlasBuffer_.va(); hasTlas=true;
#else
    tlas=tlasBuffer_.va(); hasTlas=true;
#endif
    instancesSrv=instanceBuffer_.srv; meshesSrv=meshBuffer_.srv; geometriesSrv=geometryBuffer_.srv; materialsSrv=materialBuffer_.srv;
    materialIndicesSrv=indexListBuffer_.srv; lightsSrv=lightBuffer_.srv; previousLightsSrv=previousLightBuffer_.srv;
    std::lock_guard<std::mutex> s(statsMutex_);
    stats_.instancesTraced=uint32_t(descs.size()); stats_.instancesWaiting=instancesWaiting;
    stats_.ommInstances=ommInstances; stats_.ommWaiting=uint32_t(ommQueue_.size()); stats_.swayingInstances=swaying; stats_.clothInstances=cloth; stats_.seeThroughInstances=seeThrough; stats_.waterInstances=water;
    stats_.charactersTraced=characters; stats_.characterTriangles=characterTriangles; stats_.hairLayerInstances=hairLayer;
    stats_.subsurfaceMaterials=subsurfaceMaterials_;
    stats_.shaderTestedInstances=testedInstances; stats_.shaderTestedTriangles=testedTriangles; stats_.shaderTestedOpaque=testedOpaque;
    for(uint32_t k=0;k<kCutKinds;++k) stats_.cutouts[k]=cut[k]; // (PT-110, for the log)
    stats_.decalMeshes=uint32_t(decalMeshes_.size()); stats_.ommTrial=ommTrial;
    stats_.clothSwayed=swayed; stats_.clothSwayedVertices=swayedVertices; stats_.clothSwayCopies=uint32_t(swayCopies_.size());
    stats_.plantInstances=plantInstances; stats_.plantGroups=uint32_t(plantUses.size()); stats_.plantVertices=plantVertices; stats_.plantTriangles=plantTriangles;
    stats_.plantBuilt=plantBuilt;
    stats_.movingTraced=movingTraced; stats_.movingHidden=movingHidden; stats_.movingReplaced=movingReplaced;
    if(waterGroupUses_.empty()) {
        stats_.waterUnits=stats_.waterUnitVertices=stats_.waterUnitsMade=0; stats_.waterUnitTriangles=0; stats_.waterUnitsKept=uint32_t(waterUnits_.size());
        stats_.waterGroups=stats_.waterGroupsBuilt=stats_.waterGroupsMerged=stats_.waterCollapsed=stats_.waterDead=0; stats_.waterBuiltTriangles=0;
    }
}

void GpuScene::set_map_record(uint32_t row, const uint32_t (*rows)[4], uint32_t count, const uint64_t* maps, uint32_t mapCount) {
    if(size_t(row)+count>kMapRecordRows) return;
    if(mapRecords_.empty()) mapRecords_.assign(size_t(kMapRecordRows)*4, 0xFFFFFFFFu);
    std::memcpy(&mapRecords_[size_t(row)*4], rows, size_t(count)*16);
    // Which texture each handle was read from, for swap_map_texture: the record's old ones forgotten first.
    const size_t first=size_t(row)*4;
    for(size_t k=0;k<size_t(count)*4;++k) mapTextures_.erase(first+k);
    for(uint32_t k=0;k<mapCount && k<count*4;++k) if(maps[k]) mapTextures_[first+k]=maps[k];
    mapRecordsDirty_=true;
}
uint32_t GpuScene::swap_map_texture(uint64_t from, uint64_t to, int32_t handle) {
    uint32_t changed=0;
    for(auto& [word, texture]:mapTextures_) {
        if((texture!=from || !from) && texture!=to) continue;
        texture=to; mapRecords_[word]=uint32_t(handle);
        ++changed;
    }
    if(changed) mapRecordsDirty_=true;
    return changed;
}
void GpuScene::tile_settled(int32_t tile) {
    if(tile<0 || uint32_t(tile)>=kMaxTiles) return;
    if(tilesSettled_.size()<=size_t(tile)) tilesSettled_.resize(size_t(tile)+1, 0);
    tilesSettled_[size_t(tile)]=1;
}
void GpuScene::alpha_tile_settled(int32_t tile) {
    if(tile<0 || uint32_t(tile)>=kMaxAlphaTiles) return;
    if(alphaTilesSettled_.size()<=size_t(tile)) alphaTilesSettled_.resize(size_t(tile)+1, 0);
    alphaTilesSettled_[size_t(tile)]=1;
}

// Whether a material's alpha test reads the game's own texture this frame, as pt_common.hlsli shared_cutout decides it from
// the same record (gpuMaterials_: this frame's upload) and the same frame's options.
bool GpuScene::shared_cutout(uint32_t material) const {
    if(!sharedTextures || material>=gpuMaterials_.size()) return false;
    const TGPT_Material& m=gpuMaterials_[material];
    return !(m.flags&(TGPT_MATERIAL_STRANDS|TGPT_MATERIAL_HAIR_LAYER)) && (m.flags&TGPT_MATERIAL_SHARED) && m.colorTexture>=0;
}
// Whether a mesh with these materials can have micromaps: it has alpha-tested triangles, and none of its submeshes must
// reach the per-ray test whole. A surface seen by camera rays only, and glass, must: a micromap would make its texels
// opaque to every ray. So must hair: rays pass its strands by chance (alpha_opaque), where a micromap would decide by
// the cutoff; and hair drawn as a layer, which alpha_opaque lets every camera ray but the tracer's own first pass whole.
bool GpuScene::omm_possible(const Mesh& mesh, const uint32_t* materials) const {
    uint64_t alphaTriangles=0;
    for(size_t g=0;g<mesh.data.geometries.size();++g) {
        const TGPT_Material& m=materials_[materials[g]];
        if(m.flags&(TGPT_MATERIAL_CAMERA_ONLY|TGPT_MATERIAL_GLASS|TGPT_MATERIAL_STRANDS|TGPT_MATERIAL_HAIR_LAYER)) return false;
        if(m.flags&TGPT_MATERIAL_ALPHA_TEST) alphaTriangles+=mesh.data.geometries[g].indexCount/3;
    }
    return alphaTriangles && alphaTriangles<=0xFFFFFFFFull/omm_bytes(kOmmLevelMost);
}
// The level a mesh's micromaps are baked at: kOmmLevel, and kOmmLevelLeaves (6, round 20) where a cut-out submesh is a leaf's
// or a blade of grass's (is_foliage), whose alpha's edges cross most of its pieces at level 4: the plants' pieces unknown
// (the alpha test asked there) 59 % at level 4, 46 at 5, 35 at 6 in a town; the forest's tracer 82.8 -> 75.5 ms with the
// world running, the town's 64.5 -> 62.2, the micromaps 32 -> 163 MB and 44 -> 200 (run 20261011-055446). FOR MEASURING
// (TGPT_OMM_TRIAL_LEAVES_4, _5): level 4 as before, or 5. A mesh whose cut-outs at that level are more pieces than one
// frame's bake may take (kOmmTrianglesPerFrame at level 4: 67 million pieces) is baked a level coarser, as often as it takes:
// the bake of one mesh is one dispatch, and the first mesh of a frame always went whatever its size. 2026-10-11 the GPU hung
// in the scene stage while the owner walked into new ground (TDR, 0x887A0006). A coarser level changes no pixel (an
// unknown piece is the alpha test's, as without micromaps); it costs that mesh's rays a little.
uint32_t GpuScene::omm_level(const uint32_t* materials, size_t count, uint64_t alphaTriangles) const {
    const uint32_t leaves=(ommTrial&TGPT_OMM_TRIAL_LEAVES_4) ? kOmmLevel : (ommTrial&TGPT_OMM_TRIAL_LEAVES_5) ? 5u : kOmmLevelLeaves;
    if(leaves==kOmmLevel) return kOmmLevel;
    for(size_t g=0;g<count;++g) if(materials[g]<materials_.size() && is_foliage(materials_[materials[g]].flags)) {
        uint32_t level=leaves;
        while(level>kOmmLevel && (alphaTriangles<<(2u*(level-kOmmLevel)))>kOmmTrianglesPerFrame) --level;
        return level;
    }
    return kOmmLevel;
}
// A mesh's micromaps are made for these materials (omm_possible said they can be), as they are now.
void GpuScene::want_omm(Mesh& mesh, const uint32_t* materials) {
    const size_t n=mesh.data.geometries.size();
    mesh.ommMaterials.assign(n, kInvalid); mesh.ommSignatures.assign(n, 0);
    uint64_t alphaTriangles=0;
    mesh.ommLeaf=false;
    for(size_t g=0;g<n;++g) {
        const uint32_t index=materials[g];
        mesh.ommMaterials[g]=index; mesh.ommSignatures[g]=materialSignatures_[index];
        if(materials_[index].flags&TGPT_MATERIAL_ALPHA_TEST) alphaTriangles+=mesh.data.geometries[g].indexCount/3;
        // (PT-110, for the log's count: a cut-out whose covered pieces the bake leaves unknown on purpose.)
        if((materials_[index].flags&TGPT_MATERIAL_ALPHA_TEST) && (materials_[index].flags&(TGPT_MATERIAL_TRANSLUCENT|TGPT_MATERIAL_THIN_CLOTH))) mesh.ommLeaf=true;
    }
    mesh.alphaTriangles=uint32_t(alphaTriangles);
    mesh.ommLevel=omm_level(materials, n, alphaTriangles);
    mesh.omm=Mesh::Omm::Waiting; mesh.ommWantedFrame=swayFrame_;
    ommQueue_.push_back(&mesh);
}
bool GpuScene::omm_matches(const Mesh& mesh, const uint32_t* materials) const {
    for(size_t g=0;g<mesh.ommMaterials.size();++g) {
        const uint32_t index=materials[g];
        if(index!=mesh.ommMaterials[g] || materialSignatures_[index]!=mesh.ommSignatures[g]) return false;
    }
    return mesh.ommLevel==omm_level(materials, mesh.ommMaterials.size(), mesh.alphaTriangles);
}
bool GpuScene::omm_stale(const Mesh& mesh) const {
    for(size_t g=0;g<mesh.ommMaterials.size();++g) {
        const uint32_t index=mesh.ommMaterials[g];
        if(index>=materialSignatures_.size() || materialSignatures_[index]!=mesh.ommSignatures[g]) return true;
    }
    return mesh.ommLevel!=omm_level(mesh.ommMaterials.data(), mesh.ommMaterials.size(), mesh.alphaTriangles);
}
// A mesh's micromaps and the BLAS they are linked into go once the GPU is past this frame (frames in flight, and instances
// already listed this frame, still trace with them); the next instance that can have them asks for new ones.
void GpuScene::release_omm(Mesh& mesh, uint64_t retireValue) {
    accelerationArena.retire(mesh.ommArrayVa, retireValue); accelerationArena.retire(mesh.ommBlasVa, retireValue);
    auto& kept=mesh.accelerationAllocations;
    kept.erase(std::remove_if(kept.begin(), kept.end(), [&](uint64_t va) { return va==mesh.ommArrayVa || va==mesh.ommBlasVa; }), kept.end());
    {
        std::lock_guard<std::mutex> s(statsMutex_);
        --stats_.ommMeshes; stats_.ommTriangles-=mesh.alphaTriangles; stats_.ommBytes-=mesh.ommBytes; stats_.accelerationBytes-=mesh.ommBytes;
    }
    ommInputBytes_-=std::min(ommInputBytes_, uint64_t(mesh.alphaTriangles)*omm_bytes(mesh.ommLevel));
    mesh.ommBytes=0; mesh.ommArrayVa=mesh.ommBlasVa=0; mesh.omm=Mesh::Omm::Unchecked;
    mesh.ommCounted=false; forget_omm_readback(&mesh); // (PT-110: its count goes with them)
    // (M1: and its kept index buffer, once the GPU is past this frame: a plant group that links these micromaps is
    // refitted and traced with them for the last time in this frame, and is another group from the next: plan_plants.)
    if(mesh.ommIndices) ommIndicesRetired_.emplace_back(retireValue, std::move(mesh.ommIndices));
    mesh.ommIndices=Buffer{}; mesh.ommFirst.clear();
#if TGPT_MEGA_GEOMETRY
    // (PT-109 on trial, megageo-5: its clusters' micromap array, and the templates and cluster index buffers made for that build, once the
    // GPU is past this frame: a group of clusters that links them is traced with them for the last time in this frame, and
    // is another group from the next: plan_plants.)
    if(mesh.megaMaps.serial) {
        std::vector<Buffer> gone;
        auto cut=megaMeshes_.find(mesh.handle);
        if(cut!=megaMeshes_.end()) mega_release_maps(cut->second, mesh.megaMaps.serial, gone);
        if(mesh.megaMaps.array) gone.push_back(std::move(mesh.megaMaps.array));
        for(auto& b:gone) if(b) ommIndicesRetired_.emplace_back(retireValue, std::move(b));
    }
    mesh.megaMaps=MegaMicromaps{};
#endif
}
// PT-110, for the log alone ("PT cut-outs"): the bakes whose output the GPU has copied back are counted, mesh by mesh. A
// triangle's 256 pieces are 2 bits each (0 bare, 1 covered, 2 and 3 unknown: the bake writes 3); a triangle whose index is
// one of DirectX Raytracing's special ones (-1: all bare, -2: all covered) has no pieces of its own that count.
void GpuScene::count_omm(DeferredRelease& release, uint64_t retireValue) {
    // (M1: the index buffers of micromaps let go of are freed once the GPU is past the frame that did.)
    while(!ommIndicesRetired_.empty() && ommIndicesRetired_.front().first<=completed_) ommIndicesRetired_.pop_front();
    while(!ommReadbacks_.empty() && ommReadbacks_.front().fence<=completed_) {
        OmmReadback& back=ommReadbacks_.front();
        const auto* bytes=static_cast<const uint8_t*>(back.buffer.mapped);
        for(const auto& part:back.parts) {
            Mesh* m=part.mesh;
            if(!m || !bytes || m->omm!=Mesh::Omm::Linked) continue;
            const uint8_t* pieces=bytes+part.at;
            const uint8_t* indices=pieces+uint64_t(part.triangles)*part.bytes;
#if TGPT_MEGA_GEOMETRY
            // (PT-109 on trial, megageo-5: the bake's indices of the build its clusters' micromap array was made from, for its clusters'
            // index buffers: mega_templates puts them in the clusters' order.)
            if(m->megaMaps.array && m->megaMaps.serial==m->ommSerial && m->megaMaps.count==part.triangles && m->megaMaps.indices.empty()) {
                m->megaMaps.indices.resize(part.triangles);
                std::memcpy(m->megaMaps.indices.data(), indices, size_t(part.triangles)*4);
            }
#endif
            const uint64_t triPieces=uint64_t(part.bytes)*4; // (2 bits a piece)
            uint64_t states[3]={}, settled=0;
            for(uint32_t t=0;t<part.triangles;++t) {
                int32_t index; std::memcpy(&index, indices+uint64_t(t)*4, 4);
                if(index==-1) { states[0]+=triPieces; ++settled; continue; }
                if(index==-2) { states[1]+=triPieces; ++settled; continue; }
                uint64_t covered=0, unknown=0;
                for(uint32_t w=0;w<part.bytes/8;++w) {
                    uint64_t word; std::memcpy(&word, pieces+uint64_t(t)*part.bytes+w*8, 8);
                    const uint64_t low=word&0x5555555555555555ull, high=(word>>1)&0x5555555555555555ull;
                    unknown+=__popcnt64(high); covered+=__popcnt64(low&~high);
                }
                states[0]+=triPieces-covered-unknown; states[1]+=covered; states[2]+=unknown;
                if(!unknown) ++settled;
            }
            // (In level 4's pieces, kOmmPieces a triangle, whatever the level: shares of the triangles' area, so that a leaf's finer
            // maps on trial count against the rest's alike.)
            for(int i=0;i<3;++i) m->ommPieces[i]=states[i]*kOmmPieces/triPieces;
            m->ommSettledTriangles=settled; m->ommCounted=true;
        }
        release.add(retireValue, back.buffer.resource.Get());
        ommReadbacks_.pop_front();
    }
}
// PT-110: the frame's cut-outs as the log tells them every 10 s ("PT cut-outs"), from the last recorded frame's count.
std::string GpuScene::cutout_text() const {
    const SceneStats s=stats();
    static const char* const names[kCutKinds]={"still meshes and things the game moves", "plants in the wind's pose", "characters", "cloth in the wind's pose"};
    auto number=[](uint64_t v) { return std::to_string(v); };
    auto percent=[](uint64_t part, uint64_t all) { char b[32]; std::snprintf(b, sizeof(b), "%.1f", all ? 100.0*double(part)/double(all) : 0.0); return std::string(b); };
    uint64_t triangles=0, mapped=0, counted=0, settledPieces=0, plain=0, spared=0; uint32_t instances=0;
    std::string kinds;
    for(uint32_t k=0;k<kCutKinds;++k) {
        const CutoutCount& c=s.cutouts[k];
        triangles+=c.triangles; mapped+=c.mapped; counted+=c.counted; settledPieces+=c.pieces[0]+c.pieces[1]; plain+=c.plain; spared+=c.spared; instances+=c.instances;
        const uint64_t pieces=c.pieces[0]+c.pieces[1]+c.pieces[2];
        kinds+=std::string(k ? "; " : " ")+names[k]+": "+number(c.triangles)+" in "+number(c.instances)+" instances"+
            (c.hair ? " ("+number(c.hair)+" of hair)" : std::string())+", "+number(c.mapped)+" with a micromap"+
            (c.counted ? " (pieces: "+percent(c.pieces[0], pieces)+" % bare, "+percent(c.pieces[1], pieces)+" % covered, "+percent(c.pieces[2], pieces)+" % unknown"+
                (c.leafUnknown ? ", "+percent(c.leafUnknown, pieces)+" % of all being leaves', blades' and thin cloth's" : std::string())+"; "+
                number(c.settled)+" triangles need no test at all"+(c.counted<c.mapped ? "; "+number(c.mapped-c.counted)+" not counted yet" : std::string())+")" :
             c.mapped ? std::string(" (not counted yet)") : std::string())+
            (c.plain ? ", and "+number(c.plain)+" triangles that are no cut-out go to the shader's test with them" : std::string())+
            (c.spared ? ", "+number(c.spared)+" triangles that are no cut-out are solid in their own structures" : std::string());
    }
    // What the hardware settles of every cut-out's pieces: a triangle without a micromap counts as unknown whole; one whose
    // bake is not counted yet is left out of both sides.
    const uint64_t known=triangles-(mapped-counted);
    return "PT cut-outs: "+number(triangles)+" cut-out triangles in "+number(instances)+" instances this frame; micromaps settle "+
        percent(settledPieces, known*kOmmPieces)+" % of their pieces (bare or covered), the shader's alpha test is asked about the rest ("+
        number(triangles-mapped)+" triangles have no micromap). By kind:"+kinds+". Besides: "+number(plain)+" triangles that are no cut-out reach the shader's test ("+
        number(spared)+" more are spared it: solid in a character's or a plant group's own structure); decals: "+
        number(s.decalMeshes)+" mesh decals in structures of their own, searched whole (no alpha test there). Micromaps trial: "+number(s.ommTrial)+
        (s.ommTrial&(TGPT_OMM_TRIAL_NO_PLANT_MAPS|TGPT_OMM_TRIAL_PLANT_REBUILD) ? std::string(s.ommTrial&TGPT_OMM_TRIAL_NO_PLANT_MAPS ? " (FOR MEASURING: the plants in the wind's pose without their meshes' micromaps" : " (")+
            (s.ommTrial&TGPT_OMM_TRIAL_PLANT_REBUILD ? ", their groups built anew at every pose" : "")+")" : std::string())+
        std::string(" (leaves' and grass's micromaps at level ")+((s.ommTrial&TGPT_OMM_TRIAL_LEAVES_4) ? "4, FOR MEASURING" : (s.ommTrial&TGPT_OMM_TRIAL_LEAVES_5) ? "5, FOR MEASURING" : "6")+
            ", the rest's at 4; pieces counted as shares of a triangle)"+
        (s.ommTrial&~(TGPT_OMM_TRIAL_NO_PLANT_MAPS|TGPT_OMM_TRIAL_PLANT_REBUILD|TGPT_OMM_TRIAL_LEAVES_4|TGPT_OMM_TRIAL_LEAVES_5) ? " (A MEASURING STEP: "+std::string(s.ommTrial&TGPT_OMM_TRIAL_PLANTS_SOLID ? "the plants' cut-outs solid; " : "")+
            (s.ommTrial&TGPT_OMM_TRIAL_CHARACTERS_SOLID ? "the characters' cut-outs solid; " : "")+(s.ommTrial&TGPT_OMM_TRIAL_STILL_SOLID ? "every other cut-out solid; " : "")+
            (s.ommTrial&TGPT_OMM_TRIAL_NO_MAPS ? "no instance traces with its micromaps; " : "")+
            (s.ommTrial&TGPT_OMM_TRIAL_PLAIN_TESTED ? "characters' and plants' plain submeshes go to the shader's test, as before; " : "")+
            "the picture or its speed is changed on purpose)" : std::string(s.ommTrial ? "" : " (as it ships)"));
}
// An alpha-tested instance's turn at its mesh's micromaps: true when they were made for its materials as they are now, and
// it traces with them. The first instance that can have micromaps decides which materials a mesh's are made for. They are
// made anew for this instance's materials when the ones they were made for have changed (the texture a cut-out reads was
// replaced, another cutoff or tiling), or when no instance has traced with them for kOmmUnusedFrames (the scene moved on
// to other materials): never for materials that cannot have them, so a mesh's micromaps are not lost to an instance that
// has no use for them.
bool GpuScene::omm_instance(Mesh& mesh, const uint32_t* materials, uint64_t retireValue) {
    if(mesh.omm==Mesh::Omm::Linked) {
        if(omm_matches(mesh, materials)) { mesh.ommUsedFrame=swayFrame_; return true; }
        if((omm_stale(mesh) || swayFrame_-mesh.ommUsedFrame>kOmmUnusedFrames) && omm_possible(mesh, materials)) release_omm(mesh, retireValue);
    }
    if(mesh.omm==Mesh::Omm::Unchecked && omm_possible(mesh, materials)) want_omm(mesh, materials);
    return false;
}

void GpuScene::record_omm(ID3D12Device5* device, ID3D12GraphicsCommandList4* list, DescriptorHeap& heap, DeferredRelease& release,
                          uint64_t retireValue, ID3D12PipelineState* bake) {
    if(!ommEnabled || !bake) return;
    count_omm(release, retireValue); // (PT-110, for the log: the bakes whose output has come back)
    // The bakes' scratch goes when no mesh waits for one (as the new meshes' does when a frame has none): it is as large as
    // the largest frame's bakes needed, and a scene needs it for a few frames after it loads.
    if(ommQueue_.empty()) {
        if(ommScratch_) { release.add(retireValue, ommScratch_.resource.Get()); ommScratch_=Buffer{}; }
        return;
    }
    // The game's own textures this frame's cut-outs read (shared_cutout), by the number each has this frame: a texture
    // whose sides are powers of two gets its alpha's bounds for the bake (pt_omm_bake.hlsl: the least and the most alpha of
    // every 4 x 4 texels of its top mip, then of every 2 x 2 of those, up to one cell for the whole texture, one level
    // after another in one buffer). Made for this frame's bake and dropped after it.
    struct Bounds { uint32_t width=0, height=0, uav=kInvalid; Buffer buffer; };
    std::unordered_map<int32_t, Bounds> bounds;
    uint64_t boundsTexels=0;
    // Meshes whose alpha-tested textures are there to read (the game's own: from the frame its number is; an atlas tile:
    // once it is in), within this frame's budget of triangles and of texels read for the bounds.
    std::vector<Mesh*> batch; uint64_t triangles=0;
    // PT-98: by the clock, as the builds are (their scratch and the bounds are memory in flight: kMemoryClockMost).
    // (Not more than one frame's worth however long a frame takes: one bake dispatch must never near Windows' 2 s limit.)
    const double ommClock=std::min(clock_, 1.0);
    const uint64_t ommTriangles=uint64_t(double(kOmmTrianglesPerFrame)*ommClock), ommBoundsTexels=uint64_t(double(kOmmBoundsTexelsPerFrame)*ommClock);
    for(auto it=ommQueue_.begin(); it!=ommQueue_.end() && triangles<ommTriangles;) {
        Mesh* m=*it;
        if(m->state!=Mesh::State::Ready || m->omm!=Mesh::Omm::Waiting) { it=ommQueue_.erase(it); continue; }
        bool stale=false, settled=true;
        std::vector<std::pair<int32_t, Bounds>> wanted; uint64_t wantedTexels=0;
        for(size_t g=0;g<m->ommMaterials.size();++g) {
            const uint32_t index=m->ommMaterials[g];
            if(index>=materials_.size() || materialSignatures_[index]!=m->ommSignatures[g]) { stale=true; break; }
            const TGPT_Material& mat=materials_[index];
            if(!(mat.flags&TGPT_MATERIAL_ALPHA_TEST)) continue;
            // Micromaps are baked from the texels the alpha test reads: the game's own texture where it reads that, else
            // the alpha tile when the material has one, else the albedo tile.
            if(shared_cutout(index)) {
                const int32_t number=gpuMaterials_[index].colorTexture;
                Bounds b;
                if(bounds.count(number) || !share::dimensions(number, b.width, b.height)) continue;
                if((b.width&(b.width-1)) || (b.height&(b.height-1))) continue; // the bake reads such a texture's texels alone
                bool listed=false; for(const auto& w:wanted) listed|=w.first==number;
                if(!listed) { wantedTexels+=uint64_t(b.width)*b.height; wanted.emplace_back(number, std::move(b)); }
                continue;
            }
            const bool alphaTile=(mat.flags&TGPT_MATERIAL_ALPHA_TILE) && mat.alphaTile>=0;
            const int32_t tile=alphaTile ? mat.alphaTile : mat.albedoTile;
            const std::vector<uint8_t>& ready=alphaTile ? alphaTilesSettled_ : tilesSettled_;
            if(tile>=0 && (size_t(tile)>=ready.size() || !ready[size_t(tile)])) settled=false;
        }
        // A material that changed since: the next alpha-tested instance decides again.
        if(stale) { m->omm=Mesh::Omm::Unchecked; it=ommQueue_.erase(it); continue; }
        if(!settled) { ++it; continue; }
        const uint64_t bytes=uint64_t(m->alphaTriangles)*omm_bytes(m->ommLevel);
        if(ommInputBytes_+bytes>kOmmBudget) {
            if(!ommBudgetLogged_) { log("Opacity micromaps reached their "+std::to_string(kOmmBudget>>20)+" MB budget; further alpha-tested meshes keep the alpha test alone"); ommBudgetLogged_=true; }
            m->omm=Mesh::Omm::None; it=ommQueue_.erase(it); continue;
        }
        // (A finer level's triangle counts as the pieces it bakes: level 5 four of level 4's, 6 (leaves) sixteen.)
        const uint64_t weighed=uint64_t(m->alphaTriangles)<<(2u*(std::max(m->ommLevel, kOmmLevel)-kOmmLevel));
        if(!batch.empty() && (triangles+weighed>ommTriangles || boundsTexels+wantedTexels>ommBoundsTexels)) { ++it; continue; }
        ommInputBytes_+=bytes; triangles+=weighed; boundsTexels+=wantedTexels;
        for(auto& w:wanted) bounds.emplace(w.first, std::move(w.second));
        batch.push_back(m); it=ommQueue_.erase(it);
    }
    if(batch.empty()) return;
    Barriers barriers;
    list->SetPipelineState(bake);
    // The bounds: every texture's finest level from its texels, then level by level from the one before (a barrier between
    // two levels: a level reads the one before it in the same buffer).
    {
        const auto cells=[](const Bounds& b, uint32_t level) { return std::pair<uint32_t, uint32_t>(std::max(b.width>>level, 1u), std::max(b.height>>level, 1u)); };
        const auto last=[](const Bounds& b) { uint32_t level=kOmmBoundsLevel; while((1u<<level)<std::max(b.width, b.height)) ++level; return level; };
        const auto dispatch=[&](uint32_t count) { const uint32_t groups=(count+63)/64; list->Dispatch(std::min(groups, 32768u), (groups+32767)/32768, 1); };
        uint32_t deepest=kOmmBoundsLevel;
        for(auto& [number, b]:bounds) {
            uint64_t total=0;
            for(uint32_t level=kOmmBoundsLevel;level<=last(b);++level) { const auto c=cells(b, level); total+=uint64_t(c.first)*c.second; }
            b.buffer=make_buffer(device, total*4, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, "Create a texture's alpha bounds");
            b.uav=raw_uav(device, heap, b.buffer.resource.Get(), b.buffer.size);
            deepest=std::max(deepest, last(b));
            const auto c=cells(b, kOmmBoundsLevel);
            const uint32_t constants[8]={uint32_t(number), b.uav, kOmmBoundsFirst, 0, c.first, c.second, 0, 0};
            list->SetComputeRoot32BitConstants(1, 8, constants, 0);
            dispatch(c.first*c.second);
        }
        for(uint32_t level=kOmmBoundsLevel+1;level<=deepest;++level) {
            barriers.uav(nullptr); barriers.flush(list);
            for(auto& [number, b]:bounds) {
                if(level>last(b)) continue;
                uint32_t finerFirst=0, first=0;
                for(uint32_t l=kOmmBoundsLevel;l<level;++l) { const auto c=cells(b, l); finerFirst=first; first+=c.first*c.second; }
                const auto finer=cells(b, level-1), c=cells(b, level);
                const uint32_t constants[8]={finerFirst, b.uav, kOmmBoundsNext, first, c.first, c.second, finer.first, finer.second};
                list->SetComputeRoot32BitConstants(1, 8, constants, 0);
                dispatch(c.first*c.second);
            }
        }
        if(!bounds.empty()) { barriers.uav(nullptr); barriers.flush(list); }
    }

    // Per mesh: every alpha-tested triangle's micromap (input), index and descriptor, baked on the GPU.
    struct Work {
        Mesh* mesh=nullptr; std::vector<uint32_t> first; uint32_t count=0; Buffer input, indices, descs;
        bool keep=false; // (M1: its index buffer stays with the mesh's micromaps)
        D3D12_RAYTRACING_OPACITY_MICROMAP_HISTOGRAM_ENTRY histogram{};
        D3D12_RAYTRACING_OPACITY_MICROMAP_ARRAY_DESC array{};
        std::vector<D3D12_RAYTRACING_GEOMETRY_TRIANGLES_DESC> triangles;
        std::vector<D3D12_RAYTRACING_GEOMETRY_OMM_LINKAGE_DESC> links;
        std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geometries;
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS arrayInputs{}, blasInputs{};
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO arrayInfo{}, blasInfo{};
        Arena::Range arrayRange, blasRange;
    };
    std::vector<Work> work(batch.size());
    for(size_t w=0;w<batch.size();++w) {
        Work& k=work[w]; k.mesh=batch[w];
        const auto& geos=k.mesh->data.geometries;
        k.first.assign(geos.size(), kInvalid);
        for(size_t g=0;g<geos.size();++g)
            if(materials_[k.mesh->ommMaterials[g]].flags&TGPT_MATERIAL_ALPHA_TEST) { k.first[g]=k.count; k.count+=geos[g].indexCount/3; }
        const uint32_t level=k.mesh->ommLevel, ommBytes=omm_bytes(level);
        k.input=make_buffer(device, uint64_t(k.count)*ommBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, "Create micromap input");
        k.indices=make_buffer(device, uint64_t(k.count)*4, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, "Create micromap indices");
        k.descs=make_buffer(device, uint64_t(k.count)*sizeof(D3D12_RAYTRACING_OPACITY_MICROMAP_DESC), D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, "Create micromap descriptors");
        auto* descs=static_cast<D3D12_RAYTRACING_OPACITY_MICROMAP_DESC*>(k.descs.mapped);
        for(uint32_t i=0;i<k.count;++i) {
            D3D12_RAYTRACING_OPACITY_MICROMAP_DESC d{}; d.ByteOffset=uint64_t(i)*ommBytes; d.SubdivisionLevel=level; d.Format=D3D12_RAYTRACING_OPACITY_MICROMAP_FORMAT_OC1_4_STATE;
            descs[i]=d;
        }
        const uint32_t inputUav=raw_uav(device, heap, k.input.resource.Get(), k.input.size);
        const uint32_t indexUav=raw_uav(device, heap, k.indices.resource.Get(), k.indices.size);
        for(size_t g=0;g<geos.size();++g) {
            if(k.first[g]==kInvalid) continue;
            const uint32_t count=geos[g].indexCount/3;
            // The mesh by its record (its indices and UVs: this frame's upload holds every ready mesh's), and the bounds of
            // the texture the submesh's material reads, where it has them.
            const uint32_t material=k.mesh->ommMaterials[g];
            uint32_t boundsUav=kInvalid;
            if(shared_cutout(material)) { const auto found=bounds.find(gpuMaterials_[material].colorTexture); if(found!=bounds.end()) boundsUav=found->second.uav; }
            // (The triangle count's top 4 bits carry the level: pt_omm_bake's pass.b.z.)
            const uint32_t constants[8]={uint32_t(k.mesh->handle), boundsUav, material, k.first[g], inputUav, indexUav, count|(level<<28), geos[g].indexStart};
            list->SetComputeRoot32BitConstants(1, 8, constants, 0);
            list->Dispatch(std::min(count, 32768u), (count+32767)/32768, 1);
        }
        // Their slots return once this frame is done (the heap defers every release).
        heap.release(inputUav); heap.release(indexUav);
    }
    for(auto& [number, b]:bounds) { heap.release(b.uav); release.add(retireValue, b.buffer.resource.Get()); }
    barriers.uav(nullptr);
    for(auto& k:work) { barriers.transition(k.input, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE); barriers.transition(k.indices, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE); }
    barriers.flush(list);

    // The micromap arrays, then each mesh's BLAS again with its alpha-tested submeshes linked to them.
    uint64_t scratchNeeded=0;
    for(auto& k:work) {
        k.histogram.Count=k.count; k.histogram.SubdivisionLevel=k.mesh->ommLevel; k.histogram.Format=D3D12_RAYTRACING_OPACITY_MICROMAP_FORMAT_OC1_4_STATE;
        k.array.NumOmmHistogramEntries=1; k.array.pOmmHistogram=&k.histogram; k.array.InputBuffer=k.input.va();
        k.array.PerOmmDescs.StartAddress=k.descs.va(); k.array.PerOmmDescs.StrideInBytes=sizeof(D3D12_RAYTRACING_OPACITY_MICROMAP_DESC);
        k.arrayInputs.Type=D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_OPACITY_MICROMAP_ARRAY;
        k.arrayInputs.Flags=D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
        k.arrayInputs.NumDescs=1; k.arrayInputs.DescsLayout=D3D12_ELEMENTS_LAYOUT_ARRAY; k.arrayInputs.pOpacityMicromapArrayDesc=&k.array;
        device->GetRaytracingAccelerationStructurePrebuildInfo(&k.arrayInputs, &k.arrayInfo);
        const auto& d=k.mesh->data;
        const size_t n=d.geometries.size();
        k.triangles.assign(n, D3D12_RAYTRACING_GEOMETRY_TRIANGLES_DESC{}); k.links.assign(n, D3D12_RAYTRACING_GEOMETRY_OMM_LINKAGE_DESC{});
        k.geometries.assign(n, D3D12_RAYTRACING_GEOMETRY_DESC{});
        for(size_t g=0;g<n;++g) {
            auto& t=k.triangles[g];
            t.VertexBuffer.StartAddress=k.mesh->positionsVa; t.VertexBuffer.StrideInBytes=12;
            t.VertexFormat=DXGI_FORMAT_R32G32B32_FLOAT; t.VertexCount=d.vertexCount;
            t.IndexFormat=DXGI_FORMAT_R32_UINT; t.IndexCount=d.geometries[g].indexCount;
            t.IndexBuffer=k.mesh->indicesVa ? k.mesh->indicesVa+uint64_t(d.geometries[g].indexStart)*4 : 0;
            auto& geo=k.geometries[g];
            if(k.first[g]==kInvalid) { geo.Type=D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES; geo.Flags=D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE; geo.Triangles=t; continue; }
            auto& l=k.links[g];
            l.OpacityMicromapIndexBuffer.StartAddress=k.indices.va()+uint64_t(k.first[g])*4; l.OpacityMicromapIndexBuffer.StrideInBytes=4;
            l.OpacityMicromapIndexFormat=DXGI_FORMAT_R32_UINT; l.OpacityMicromapBaseLocation=0;
            l.OpacityMicromapArray=0; // its place is known once the array is allocated
            geo.Type=D3D12_RAYTRACING_GEOMETRY_TYPE_OMM_TRIANGLES; geo.Flags=D3D12_RAYTRACING_GEOMETRY_FLAG_NONE;
            geo.OmmTriangles.pTriangles=&t; geo.OmmTriangles.pOmmLinkage=&l;
        }
        k.blasInputs.Type=D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
        // M5 (PT-110): built to be compacted, as the plain ones are, and without the flag that let an instance switch
        // its micromaps off (record: such an instance takes the mesh's plain bottom level).
        k.blasInputs.Flags=D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE|D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_COMPACTION;
        k.blasInputs.NumDescs=UINT(n); k.blasInputs.DescsLayout=D3D12_ELEMENTS_LAYOUT_ARRAY; k.blasInputs.pGeometryDescs=k.geometries.data();
        device->GetRaytracingAccelerationStructurePrebuildInfo(&k.blasInputs, &k.blasInfo);
        scratchNeeded+=(k.arrayInfo.ScratchDataSizeInBytes+255)/256*256+256+(k.blasInfo.ScratchDataSizeInBytes+255)/256*256+256;
    }
    if(ommScratch_.size<scratchNeeded) {
        if(ommScratch_) release.add(retireValue, ommScratch_.resource.Get());
        ommScratch_=make_buffer(device, std::max<uint64_t>(scratchNeeded, 16ull<<20), D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, "Create micromap scratch");
    }
    uint64_t scratch=0;
    auto build=[&](const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS& in, const D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO& info, Arena::Range& range,
                   const D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_DESC* post=nullptr) {
        if(!info.ResultDataMaxSizeInBytes) throw DxError(E_FAIL, "prebuild reported no size");
        range=accelerationArena.allocate(device, info.ResultDataMaxSizeInBytes, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT, "Create acceleration structure arena");
        scratch=(scratch+255)/256*256;
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC desc{};
        desc.DestAccelerationStructureData=range.va(); desc.ScratchAccelerationStructureData=ommScratch_.va()+scratch; desc.Inputs=in;
        list->BuildRaytracingAccelerationStructure(&desc, post ? 1u : 0u, post);
        { auto& sw=structureWork[kStructureMicromaps]; ++sw.builds; sw.bytes+=range.size; }
        scratch+=info.ScratchDataSizeInBytes;
    };
    std::vector<bool> ok(work.size(), true);
    for(size_t w=0;w<work.size();++w) {
        try { build(work[w].arrayInputs, work[w].arrayInfo, work[w].arrayRange); }
        catch(const std::exception& e) { ok[w]=false; log_throttled(std::string("An opacity micromap array could not be built: ")+e.what()); }
    }
    barriers.uav(nullptr); barriers.flush(list);
    // M5 (PT-110, docs/agents/omm-1.md): each BLAS with micromaps writes the size it compacts to (8 bytes a mesh), read
    // back when the GPU is past this frame; compact_meshes then copies it compacted, as it does a mesh's plain one (those
    // compact to some 38 %). Until 2026-10-10 a micromap structure stayed at its build's largest size for good. If the
    // two small buffers cannot be made the structures stay as built.
    Buffer compactSizes; CompactBatch compact; compact.fence=retireValue;
    try {
        compactSizes=make_buffer(device, work.size()*8, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                 "Create micromap BLAS compact sizes");
        compact.readback=make_buffer(device, work.size()*8, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST, "Read micromap BLAS compact sizes");
    } catch(const std::exception&) { compactSizes=Buffer{}; compact.readback=Buffer{}; }
    const bool sized=compactSizes && compact.readback;
    uint64_t triangleCount=0, bytes=0; uint32_t meshes=0;
    for(size_t w=0;w<work.size();++w) {
        Work& k=work[w];
        if(ok[w]) {
            for(size_t g=0;g<k.links.size();++g) if(k.first[g]!=kInvalid) k.links[g].OpacityMicromapArray=k.arrayRange.va();
            D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_DESC post{};
            post.DestBuffer=sized ? compactSizes.va()+compact.entries.size()*8 : 0; post.InfoType=D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_COMPACTED_SIZE;
            try { build(k.blasInputs, k.blasInfo, k.blasRange, sized ? &post : nullptr); }
            catch(const std::exception& e) { ok[w]=false; log_throttled(std::string("A BLAS with opacity micromaps could not be built: ")+e.what()); }
        }
        if(ok[w]) {
            // (The entry names this build of the mesh's micromaps by its number: maps made anew meanwhile are another.)
            const uint32_t serial=++k.mesh->ommSerial;
            if(sized) compact.entries.push_back(CompactEntry{k.mesh->handle, k.blasRange.va(), true, k.blasRange.size, serial});
            k.mesh->accelerationAllocations.push_back(k.arrayRange.va());
            k.mesh->accelerationAllocations.push_back(k.blasRange.va());
            k.mesh->ommBytes=k.arrayRange.size+k.blasRange.size;
            k.mesh->ommArrayVa=k.arrayRange.va(); k.mesh->ommBlasVa=k.blasRange.va(); k.mesh->omm=Mesh::Omm::Linked;
#if TGPT_MEGA_GEOMETRY
            mega_fresh(k.blasRange.va()); // PT-109, as build_blas
#endif
            k.mesh->ommUsedFrame=swayFrame_;
            ++meshes; triangleCount+=k.count; bytes+=k.arrayRange.size+k.blasRange.size;
        } else { accelerationArena.retire(k.arrayRange.chunk ? k.arrayRange.va() : 0, retireValue);
            accelerationArena.retire(k.blasRange.chunk ? k.blasRange.va() : 0, retireValue); k.mesh->omm=Mesh::Omm::None;
            ommInputBytes_-=std::min(ommInputBytes_, uint64_t(k.mesh->alphaTriangles)*omm_bytes(k.mesh->ommLevel)); }
        // Micromap inputs are read by the builds only. (M1, on trial: but the index buffer of a mesh the wind moves is kept
        // with its micromaps, for the plant groups' structures that link them: below, once it is read again.)
        release.add(retireValue, k.input.resource.Get()); release.add(retireValue, k.descs.resource.Get());
        bool kept=ok[w] && (ommTrial&TGPT_OMM_TRIAL_NO_PLANT_MAPS)==0;
        for(size_t g=0;g<k.mesh->ommMaterials.size() && ok[w] && !kept;++g) kept=plantWindOf_.count(k.mesh->ommMaterials[g])!=0;
        k.keep=kept;
        if(!kept) release.add(retireValue, k.indices.resource.Get());
    }
    barriers.uav(nullptr); barriers.flush(list);
#if TGPT_MEGA_GEOMETRY
    // PT-109 on trial (megageo-5; the hidden "Mega Geometry trial" 5 and 7): a plant mesh's micromaps a second time, as
    // the driver's own micromap array for clusters (the only kind its cluster calls document), from the same bake output while it is still on the GPU
    // (the inputs are let go when the GPU is past this frame), for this build of them (its serial). Its index buffer follows
    // from the bake's indices once they are back on the CPU (count_omm, mega_templates). One that cannot be made keeps the
    // build's number with no array: that mesh's clusters take no micromaps.
    std::vector<Mesh*> megaMapped;
    if(mega_maps_on()) for(size_t w=0;w<work.size();++w) {
        Work& k=work[w];
        if(!ok[w] || !k.count || k.mesh->skinned) continue;
        bool plant=false;
        for(size_t g=0;g<k.mesh->ommMaterials.size() && !plant;++g) plant=plantWindOf_.count(k.mesh->ommMaterials[g])!=0;
        if(!plant) continue;
        std::string error;
        if(mega_build_micromaps(device, list, release, retireValue, k.input.va(), k.count, k.mesh->ommLevel, omm_bytes(k.mesh->ommLevel), k.mesh->ommSerial, k.mesh->megaMaps, barriers, error)) {
            ++megaMapsBuilt_; megaMapped.push_back(k.mesh);
        } else {
            ++megaMapsFailed_; k.mesh->megaMaps.serial=k.mesh->ommSerial;
            log_throttled("Mega Geometry trial: a plant mesh's micromaps could not be made as the clusters' own micromap array ("+error+"); its clusters take none");
        }
    }
#endif
    if(sized) { // (M5: the compacted sizes on their way back, as record sends the plain structures')
        barriers.transition(compactSizes, D3D12_RESOURCE_STATE_COPY_SOURCE); barriers.flush(list);
        list->CopyResource(compact.readback.resource.Get(), compactSizes.resource.Get());
        release.add(retireValue, compactSizes.resource.Get());
        if(!compact.entries.empty()) compactBatches_.push_back(std::move(compact));
        else release.add(retireValue, compact.readback.resource.Get());
    }
    // PT-110, for the log alone ("PT cut-outs"): the bakes' output on its way back to the CPU, where each mesh's pieces are
    // counted by state once the GPU is past this frame (count_omm). One read-back buffer for the frame's bakes, 68 bytes a
    // cut-out triangle (its pieces, then its index), gone when counted. If it cannot be made the meshes go uncounted, and
    // trace the same.
    {
        OmmReadback back; back.fence=retireValue;
        uint64_t total=0;
        for(size_t w=0;w<work.size();++w) if(ok[w] && work[w].count) total+=uint64_t(work[w].count)*(omm_bytes(work[w].mesh->ommLevel)+4);
        try { if(total) back.buffer=make_buffer(device, total, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST, "Read the frame's micromaps back"); }
        catch(const std::exception&) {}
        if(back.buffer) {
            for(size_t w=0;w<work.size();++w) if(ok[w] && work[w].count) {
                barriers.transition(work[w].input, D3D12_RESOURCE_STATE_COPY_SOURCE); barriers.transition(work[w].indices, D3D12_RESOURCE_STATE_COPY_SOURCE);
            }
            barriers.flush(list);
            uint64_t at=0;
            for(size_t w=0;w<work.size();++w) {
                Work& k=work[w];
                if(!ok[w] || !k.count) continue;
                const uint32_t ommBytes=omm_bytes(k.mesh->ommLevel);
                list->CopyBufferRegion(back.buffer.resource.Get(), at, k.input.resource.Get(), 0, uint64_t(k.count)*ommBytes);
                list->CopyBufferRegion(back.buffer.resource.Get(), at+uint64_t(k.count)*ommBytes, k.indices.resource.Get(), 0, uint64_t(k.count)*4);
                back.parts.push_back(OmmReadback::Part{k.mesh, k.count, at, ommBytes});
                k.mesh->ommCounted=false;
                at+=uint64_t(k.count)*(ommBytes+4);
            }
            ommReadbacks_.push_back(std::move(back));
        }
#if TGPT_MEGA_GEOMETRY
        // (megageo-5: with no read-back the bake's indices never come: such a mesh's clusters' micromap array goes, and its clusters take none.)
        else for(Mesh* m:megaMapped) { std::vector<Buffer> gone; gone.push_back(std::move(m->megaMaps.array)); m->megaMaps.array=Buffer{}; m->megaMaps.count=0;
                                       for(auto& b:gone) if(b) release.add(retireValue, b.resource.Get()); }
#endif
    }
    // M1 (on trial): a kept index buffer goes to its mesh, readable by the builds and refits to come (a plant group's
    // structure names it at each), with where each submesh's triangles start in it.
    for(size_t w=0;w<work.size();++w) {
        Work& k=work[w];
        if(!k.keep) continue;
        barriers.transition(k.indices, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        k.mesh->ommIndices=k.indices; k.mesh->ommFirst=k.first;
    }
    barriers.flush(list);
    std::lock_guard<std::mutex> s(statsMutex_);
    stats_.ommMeshes+=meshes; stats_.ommTriangles+=triangleCount; stats_.ommBytes+=bytes; stats_.accelerationBytes+=bytes;
}

// Emissive triangles as lights. The emitters are the emissive submeshes of the scene's instances traced this frame (not those
// a thing the game moves stands in for, not the moving things themselves, not stand-ins, not characters), ranked by the light they can give (their material's radiance
// over their area) and kept, brightest and largest first, up to kEmitterBudget triangles. Each keeps its slots from build to
// build (by its transform, mesh, submesh and material): a slot holding the same triangle in two frames is the same light for
// ReSTIR's temporal reuse, as a light's slot is. Their clusters bright enough to matter are sampled as lights through the
// light grid (with the Emissive Lights row); the pass that writes their records (pt_emissive.hlsl) finds each triangle's
// light from the atlas and marks the ones right around a light the light's own surface (a lantern's glass round its candle).
void GpuScene::build_emitters(ID3D12Device5* device, ID3D12GraphicsCommandList4* list, DescriptorHeap& heap, UploadRing& ring, DeferredRelease& release,
                              uint64_t retireValue, const double origin[3], const std::vector<uint32_t>& sources, Barriers& barriers) {
    // The previous frame's records are the ones this frame's temporal reuse compares with, until a build writes the others.
    previousEmittersSrv=emittersSrv; previousEmitterCount=emitterCount;
    // What the records depend on besides the scene: where the lights are (a light's own surface), and the origin.
    uint64_t lightsHash=1469598103934665603ull;
    for(const auto& l:lights_) {
        if(l.type>1) continue;
        for(float p:l.position) lightsHash=mix64(lightsHash, uint64_t(int64_t(std::llround(double(p)*4.0))));
        lightsHash=mix64(lightsHash, uint64_t(std::llround(double(l.radius)*100.0)));
    }
    bool rebuild=emittersDirty_ || emissiveLights!=emissiveLightsBuilt_;
    // The instance table is by RECORD NUMBER, and the frame's records are laid out anew every frame: a plant in the wind has
    // no record of its own once its group is made (and has one again while its group waits to be made anew), which moves
    // every record after it and marks nothing here. Until 2026-10-12 the table kept the numbers of the frame the emitters
    // were last built in: a path that met a glowing triangle then read another instance's entry, and its glow counted
    // twice (no entry: whole, beside the light sampling) or for nothing (another emitter's record) until the next build
    // (PT-125's kind of fault; docs/agents/stale.md F2). So the table is laid out again for this frame's records whenever
    // they are not the ones it was laid out for: the same emitters and slots, each at its instance's record now. An
    // emitter whose instance has no record any more (it went into a group) is no emitter: built anew.
    if(!rebuild && !emitterPlaces_.empty() && sources!=emitterTableSources_) {
        std::vector<uint32_t> recordOf(instances_.size(), kInvalid);
        for(uint32_t r=0;r<uint32_t(sources.size());++r) if(sources[r]<recordOf.size()) recordOf[sources[r]]=r;
        for(const EmitterPlace& p:emitterPlaces_) if(p.source>=recordOf.size() || recordOf[p.source]==kInvalid) { rebuild=true; break; }
        if(!rebuild) {
            emitterTable_.assign(sources.size(), kInvalid);
            for(const EmitterPlace& p:emitterPlaces_) {
                const uint32_t record=recordOf[p.source];
                uint32_t entry=emitterTable_[record];
                if(entry==kInvalid) {
                    entry=uint32_t(emitterTable_.size()); emitterTable_[record]=entry;
                    emitterTable_.resize(emitterTable_.size()+p.geometries, kInvalid);
                }
                emitterTable_[entry+p.geometry]=p.first;
            }
            emitterTableSources_=sources; emitterTableInstances=uint32_t(sources.size());
            upload_structured(device, list, heap, ring, release, retireValue, emitterTableBuffer_, emitterTable_.data(), uint32_t(emitterTable_.size()), 4, barriers);
            emitterTableSrv=emitterTableBuffer_.srv;
        }
    }
    const bool rewrite=rebuild || lightsHash!=emitterLightsHash_ || std::memcmp(origin, emitterOrigin_, sizeof(emitterOrigin_))!=0;
    if(!rewrite) return;
    emitterLightsHash_=lightsHash; std::memcpy(emitterOrigin_, origin, sizeof(emitterOrigin_));
    if(rebuild) {
        emittersDirty_=false; emissiveLightsBuilt_=emissiveLights;
        struct Candidate { uint32_t record, geometry, material, triangles; const Mesh* mesh; float radiance, planeScale2[3]; double power; uint64_t key; };
        std::vector<Candidate> candidates;
        for(uint32_t r=0;r<uint32_t(sources.size());++r) {
            const TGPT_Instance& inst=instances_[sources[r]];
            if((inst.flags&TGPT_INSTANCE_STAND_IN) || (sources[r]<replaced_.size() && replaced_[sources[r]])) continue;
            const Mesh* m=meshes_[size_t(inst.mesh)-1].get();
            if(!m || m->skinned || m->data.geometryClusters.size()!=m->data.geometries.size()+1) continue;
            const float* t=inst.transform;
            // The squared areas of the mesh's axis planes in the world (world_area): only ranks, places and reaches clusters,
            // the records have their triangles' own.
            float planeScale2[3];
            for(int a=0;a<3;++a) {
                const int b=(a+1)%3, c=(a+2)%3;
                const double u[3]={t[b], t[4+b], t[8+b]}, v[3]={t[c], t[4+c], t[8+c]};
                const double x=u[1]*v[2]-u[2]*v[1], y=u[2]*v[0]-u[0]*v[2], z=u[0]*v[1]-u[1]*v[0];
                planeScale2[a]=float(x*x+y*y+z*z);
            }
            if(!(planeScale2[0]+planeScale2[1]+planeScale2[2]>0.f) || !std::isfinite(planeScale2[0]+planeScale2[1]+planeScale2[2])) continue;
            for(uint32_t g=0;g<uint32_t(m->data.geometries.size());++g) {
                const uint32_t mi=materialIndices_[inst.materialBase+g];
                const TGPT_Material& mat=materials_[mi];
                float radiance=luminance(mat.emissive);
                if(!(radiance>0.f) || !std::isfinite(radiance) || (mat.flags&TGPT_MATERIAL_CAMERA_ONLY)) continue;
                // Never met by a ray (the plugin's invisible material): it lights nothing.
                if((mat.flags&TGPT_MATERIAL_ALPHA_TEST) && !(mat.flags&TGPT_MATERIAL_GLASS) && mat.albedoTile<0 && !(mat.baseColor[3]>=mat.alphaCutoff)) continue;
                if(mat.flags&TGPT_MATERIAL_EMISSIVE_ALBEDO) radiance*=std::max(0.f, luminance(mat.baseColor));
                double area=0;
                for(uint32_t c=m->data.geometryClusters[g];c<m->data.geometryClusters[g+1];++c) area+=world_area(m->data.clusters[c], planeScale2);
                const double power=double(radiance)*area;
                if(!(power>=kEmitterMinIntensity)) continue;
                uint64_t key=mix64(mix64(mix64(1469598103934665603ull, uint64_t(inst.mesh)), g), mi);
                for(int i=0;i<12;++i) { uint32_t bits; std::memcpy(&bits, &t[i], 4); key=mix64(key, bits); }
                candidates.push_back(Candidate{r, g, mi, m->data.geometries[g].indexCount/3, m, radiance, {planeScale2[0], planeScale2[1], planeScale2[2]}, power, key});
            }
        }
        std::stable_sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) { return a.power>b.power; });
        std::vector<const Candidate*> kept; uint32_t triangles=0, overBudget=0;
        for(const auto& c:candidates) {
            if(triangles+c.triangles>kEmitterBudget) { ++overBudget; continue; }
            triangles+=c.triangles; kept.push_back(&c);
        }
        // Slots: an emitter kept from the last build keeps its own; the others take free ranges, or new ones after the last;
        // when those run out, every emitter is given slots again from the start (temporal reuse starts over for them once).
        ++emitterBuild_;
        std::vector<uint32_t> first(kept.size(), kInvalid);
        for(size_t i=0;i<kept.size();++i) {
            auto found=emitterSlots_.find(kept[i]->key);
            if(found!=emitterSlots_.end() && found->second.count==kept[i]->triangles && found->second.build!=emitterBuild_) {
                first[i]=found->second.first; found->second.build=emitterBuild_;
            }
        }
        for(auto it=emitterSlots_.begin();it!=emitterSlots_.end();) {
            if(it->second.build==emitterBuild_) { ++it; continue; }
            freeSlots_.emplace_back(it->second.first, it->second.count); it=emitterSlots_.erase(it);
        }
        std::sort(freeSlots_.begin(), freeSlots_.end());
        std::vector<std::pair<uint32_t, uint32_t>> merged;
        for(const auto& f:freeSlots_) {
            if(!merged.empty() && merged.back().first+merged.back().second==f.first) merged.back().second+=f.second;
            else if(f.second) merged.push_back(f);
        }
        freeSlots_.swap(merged);
        auto allocate=[&](uint32_t count) -> uint32_t {
            for(auto& f:freeSlots_) if(f.second>=count) { const uint32_t at=f.first; f.first+=count; f.second-=count; return at; }
            if(slotCapacity_+count>kEmitterSlots) return kInvalid;
            const uint32_t at=slotCapacity_; slotCapacity_+=count; return at;
        };
        bool compact=false;
        for(size_t i=0;i<kept.size() && !compact;++i) if(first[i]==kInvalid && (first[i]=allocate(kept[i]->triangles))==kInvalid) compact=true;
        if(compact) {
            emitterSlots_.clear(); freeSlots_.clear(); slotCapacity_=0;
            for(size_t i=0;i<kept.size();++i) first[i]=allocate(kept[i]->triangles);
        }
        for(size_t i=0;i<kept.size();++i) emitterSlots_[kept[i]->key]=EmitterSlots{first[i], kept[i]->triangles, emitterBuild_};
        // A trailing free range gives its slots back.
        freeSlots_.erase(std::remove_if(freeSlots_.begin(), freeSlots_.end(), [](const std::pair<uint32_t, uint32_t>& f) { return f.second==0; }), freeSlots_.end());
        while(!freeSlots_.empty() && freeSlots_.back().first+freeSlots_.back().second==slotCapacity_) {
            slotCapacity_=freeSlots_.back().first; freeSlots_.pop_back();
        }
        // The records' inputs: each emitter, its clusters (sampled as lights, or left to the paths that meet them) and the
        // clusters the light grid lists; the slots' owners; the instance table (an instance's first entry, then its
        // submeshes' first slots).
        emitterList_.clear(); emitterWorld_.clear(); clusterInfo_.clear(); clusterLights_.clear();
        slotOwners_.assign(slotCapacity_, kInvalid);
        emitterTable_.assign(sources.size(), kInvalid);
        // (What the table is made from, for the frames whose records differ: above. Read only between two builds: clear
        // marks the emitters, so a build makes both anew before the lines above look at them again.)
        emitterPlaces_.clear(); emitterTableSources_=sources;
        brightestEmitter_=0.f; brightestEmitterRaw_=0.f;
        uint32_t trianglesSampled=0;
        for(size_t i=0;i<kept.size();++i) {
            const Candidate& c=*kept[i];
            const TGPT_Instance& inst=instances_[sources[c.record]];
            const float* t=inst.transform;
            EmitterGpu e{}; std::array<float, 12> world{};
            for(int k=0;k<12;++k) world[size_t(k)]=t[k];
            e.mesh=uint32_t(inst.mesh); e.geometry=c.geometry; e.material=c.material; e.firstSlot=first[i]; e.triangles=c.triangles;
            e.firstCluster=uint32_t(clusterInfo_.size()); e.key=uint32_t(c.key^(c.key>>32)); e.flags=0;
            const uint32_t c0=c.mesh->data.geometryClusters[c.geometry], c1=c.mesh->data.geometryClusters[c.geometry+1];
            double sampledPower=0;
            for(uint32_t k=c0;k<c1;++k) {
                const TriangleCluster& tc=c.mesh->data.clusters[k];
                const uint32_t start=(k-c0)*kClusterTriangles, count=std::min(kClusterTriangles, c.triangles-start);
                // The cluster's bounds in the world: its box's corners moved there, the sphere around them.
                double lo[3]={1e300, 1e300, 1e300}, hi[3]={-1e300, -1e300, -1e300};
                for(int corner=0;corner<8;++corner) {
                    const double p[3]={corner&1 ? tc.hi[0] : tc.lo[0], corner&2 ? tc.hi[1] : tc.lo[1], corner&4 ? tc.hi[2] : tc.lo[2]};
                    for(int a=0;a<3;++a) {
                        const double w=double(t[a*4])*p[0]+double(t[a*4+1])*p[1]+double(t[a*4+2])*p[2]+double(t[a*4+3]);
                        lo[a]=std::min(lo[a], w); hi[a]=std::max(hi[a], w);
                    }
                }
                const double radius=0.5*std::sqrt((hi[0]-lo[0])*(hi[0]-lo[0])+(hi[1]-lo[1])*(hi[1]-lo[1])+(hi[2]-lo[2])*(hi[2]-lo[2]));
                const float intensity=c.radiance*world_area(tc, c.planeScale2);
                // A kept light's own surface (TGPT_MATERIAL_OWN_SURFACE) lights nothing: never drawn as a light.
                const bool sampled=emissiveLights && !(materials_[c.material].flags&TGPT_MATERIAL_OWN_SURFACE) && intensity>=kEmitterMinIntensity && std::isfinite(radius);
                const float reach=std::clamp(std::sqrt(std::max(intensity, 0.f)/kEmitterReachLux), kEmitterMinReach, kEmitterMaxReach);
                clusterInfo_.push_back(ClusterInfoGpu{first[i]+start, count, sampled ? 1u : 0u, reach});
                if(!sampled) continue;
                // Its record (pt_common.hlsli cluster_triangle): its first slot in flags, its triangles in cookieScale's bits,
                // no identity (never in a reservoir).
                TGPT_Light l{};
                for(int a=0;a<3;++a) l.position[a]=float(0.5*(lo[a]+hi[a]));
                l.type=3u; l.color[0]=l.color[1]=l.color[2]=intensity; l.range=float(radius)+reach;
                l.id=0u; l.flags=first[i]+start; std::memcpy(&l.cookieScale, &count, 4);
                clusterLights_.push_back(l);
                sampledPower+=intensity; trianglesSampled+=count;
            }
            // PT-136: what a glowing mesh adds to the reference radiance (tracer.cpp: "a white surface lit by the brightest
            // light 1 m away"). Its clusters' intensity together (radiance times area) is a point light's reckoning: for a
            // SMALL glowing thing 1 m off, right; for a large one (a glowing sheet tens of metres wide) it says a light no
            // surface can ever receive: a white surface right against an endless glowing wall shows the wall's own
            // radiance and no more. So: the lesser of the two (the tracer divides by pi: radiance times pi here). Until
            // 2026-10-12 the sum alone counted, and one large glowing mesh in a daylight vista put the reference at
            // 5.7e8, 20,000 times the sun's white: every other light was then stored under a 16-bit float's normal range
            // (a sunlit white wall at 4.7e-5, the fog's light at 0 to 8 of its steps), and the clamps, which measure
            // against the reference, never held anything.
            brightestEmitterRaw_=std::max(brightestEmitterRaw_, float(sampledPower));
            brightestEmitter_=std::max(brightestEmitter_, float(std::min(sampledPower, 3.14159265358979*double(c.radiance))));
            for(uint32_t s=0;s<c.triangles;++s) slotOwners_[first[i]+s]=uint32_t(emitterList_.size());
            uint32_t entry=emitterTable_[c.record];
            if(entry==kInvalid) {
                entry=uint32_t(emitterTable_.size()); emitterTable_[c.record]=entry;
                emitterTable_.resize(emitterTable_.size()+c.mesh->data.geometries.size(), kInvalid);
            }
            emitterTable_[entry+c.geometry]=first[i];
            emitterPlaces_.push_back(EmitterPlace{sources[c.record], c.geometry, first[i], uint32_t(c.mesh->data.geometries.size())});
            emitterList_.push_back(e); emitterWorld_.push_back(world);
        }
        clusterBase=slotCapacity_; sampledClusters=uint32_t(clusterLights_.size());
        emitterTableInstances=uint32_t(sources.size());
        upload_structured(device, list, heap, ring, release, retireValue, slotOwnerBuffer_, slotOwners_.data(), uint32_t(slotOwners_.size()), 4, barriers);
        upload_structured(device, list, heap, ring, release, retireValue, clusterInfoBuffer_, clusterInfo_.data(), uint32_t(clusterInfo_.size()), sizeof(ClusterInfoGpu), barriers);
        upload_structured(device, list, heap, ring, release, retireValue, emitterTableBuffer_, emitterTable_.data(), uint32_t(emitterTable_.size()), 4, barriers);
        emitterTableSrv=emitterTableBuffer_.srv;
        std::lock_guard<std::mutex> s(statsMutex_);
        stats_.emitters=uint32_t(kept.size()); stats_.emitterTriangles=triangles; stats_.emitterClustersSampled=sampledClusters;
        stats_.emitterTrianglesSampled=trianglesSampled; stats_.emittersOverBudget=overBudget; ++stats_.emitterBuilds;
    }
    const uint32_t records=slotCapacity_+sampledClusters;
    if(emitterList_.empty() || !records) { emittersSrv=kInvalid; emitterCount=0; emitterPassPending_=false; return; }
    // Written into the buffer the previous frame did not read: its records stay what that frame's reservoirs refer to.
    const int next=emitterCurrent_<0 ? 0 : 1-emitterCurrent_;
    Buffer& target=emitterRecords_[size_t(next)];
    if(!target || emitterRecordsCapacity_[size_t(next)]<records) {
        if(target) release.add(retireValue, target.resource.Get());
        heap.release(emitterRecordsSrv_[size_t(next)]); heap.release(emitterRecordsUav_[size_t(next)]);
        uint32_t capacity=256; while(capacity<records) capacity*=2;
        target=make_buffer(device, uint64_t(capacity)*sizeof(TGPT_Light), D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                           D3D12_RESOURCE_STATE_COMMON, "Create emitter records");
        emitterRecordsCapacity_[size_t(next)]=capacity;
        emitterRecordsSrv_[size_t(next)]=structured_srv(device, heap, target.resource.Get(), capacity, sizeof(TGPT_Light));
        emitterRecordsUav_[size_t(next)]=structured_uav(device, heap, target.resource.Get(), capacity, sizeof(TGPT_Light));
    }
    // The emitters' transforms and the clusters' records relative to this frame's origin.
    for(size_t i=0;i<emitterList_.size();++i)
        for(int k=0;k<12;++k) emitterList_[i].transform[k]=float(k%4==3 ? double(emitterWorld_[i][size_t(k)])-origin[k/4] : double(emitterWorld_[i][size_t(k)]));
    upload_structured(device, list, heap, ring, release, retireValue, emitterListBuffer_, emitterList_.data(), uint32_t(emitterList_.size()), sizeof(EmitterGpu), barriers);
    if(sampledClusters) {
        std::vector<TGPT_Light> clusters=clusterLights_;
        for(auto& l:clusters) for(int a=0;a<3;++a) l.position[a]=float(double(l.position[a])-origin[a]);
        const uint64_t bytes=uint64_t(clusters.size())*sizeof(TGPT_Light);
        barriers.transition(target, D3D12_RESOURCE_STATE_COPY_DEST); barriers.flush(list);
        void* cpu=nullptr; D3D12_GPU_VIRTUAL_ADDRESS gpu=0; uint64_t offset=0;
        if(ring.allocate(bytes, 16, &cpu, &gpu, &offset)) {
            std::memcpy(cpu, clusters.data(), bytes);
            list->CopyBufferRegion(target.resource.Get(), uint64_t(slotCapacity_)*sizeof(TGPT_Light), ring.resource(), offset, bytes);
        } else {
            Buffer upload=make_buffer(device, bytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, "Create cluster upload buffer");
            std::memcpy(upload.mapped, clusters.data(), bytes);
            list->CopyBufferRegion(target.resource.Get(), uint64_t(slotCapacity_)*sizeof(TGPT_Light), upload.resource.Get(), 0, bytes);
            release.add(retireValue, upload.resource.Get());
        }
    }
    barriers.flush(list);
    emitterCurrent_=next; emitterRecordsCount_[size_t(next)]=records;
    emittersSrv=emitterRecordsSrv_[size_t(next)]; emitterCount=records;
    emitterPassPending_=true;
}

void GpuScene::record_emitters(ID3D12GraphicsCommandList4* list, ID3D12PipelineState* pass) {
    if(!emitterPassPending_ || emitterCurrent_<0) return;
    emitterPassPending_=false;
    Buffer& target=emitterRecords_[size_t(emitterCurrent_)];
    const uint32_t uav=emitterRecordsUav_[size_t(emitterCurrent_)];
    Barriers b;
    b.transition(target, D3D12_RESOURCE_STATE_UNORDERED_ACCESS); b.flush(list);
    if(pass) {
        list->SetPipelineState(pass);
        // The triangles first (every slot, a free one as an empty record), then each sampled cluster's cumulative weights.
        const uint32_t triangles[8]={emitterListBuffer_.srv, slotOwnerBuffer_.srv, clusterInfoBuffer_.srv, 0u, uav, slotCapacity_, sampledClusters, 0u};
        list->SetComputeRoot32BitConstants(1, 8, triangles, 0);
        if(slotCapacity_) list->Dispatch((slotCapacity_+63)/64, 1, 1);
        b.uav(target.resource.Get()); b.flush(list);
        const uint32_t weights[8]={emitterListBuffer_.srv, slotOwnerBuffer_.srv, clusterInfoBuffer_.srv, 1u, uav, slotCapacity_, sampledClusters, 0u};
        list->SetComputeRoot32BitConstants(1, 8, weights, 0);
        if(sampledClusters) list->Dispatch((sampledClusters+63)/64, 1, 1);
        b.uav(target.resource.Get());
    }
    b.transition(target, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE); b.flush(list);
}

void GpuScene::build_light_grid(const double corner[3]) {
    const uint32_t dx=kGridDims[0], dy=kGridDims[1], dz=kGridDims[2];
    const size_t cellCount=size_t(dx)*dy*dz;
    // (PT-99: this runs on a thread of its own, beside the frame's record, which waits for it before the lists go up:
    // record. So it touches nothing of record's: no D3D12, no record_cpu; its times go into gridJobMs_, and
    // gridCorner_, which record reads every frame, is set by record once this is done.)
    const auto began=std::chrono::steady_clock::now();
    // Counted, then filled: every cell's lights are contiguous, and no cell has a cap that would drop one. Each entry
    // is a light and its cumulative weight within the cell (normalized to 1): the light's luminance over its squared
    // distance from the cell's box, at least 1 m, so the shaders draw the lights that can matter there about as often
    // as they do (cell_candidate). With lights reaching as far as physical lights do, a cell lists dozens.
    // After the lights, the emissive clusters sampled as lights (their records from kEmitterBase + clusterBase on): a
    // cluster's range is its reach from its bounds, its luminance the intensity its triangles have together at most.
    // The cells' lists are light_grid.hpp's (PT-99: one walk a light where this function walked each light's cells twice,
    // the same bytes; an entry's weight there: float(lightPower/std::max(d2, 1.0))).
    const light_grid::Shape shape{{kGridDims[0], kGridDims[1], kGridDims[2]}, kGridCell, kGridPad, kEmitterBase};
    const light_grid::Counts counts=light_grid::build_cells(shape, corner, lights_.data(), lights_.size(), clusterLights_.data(), clusterLights_.size(), clusterBase,
                                                            gridCells_, gridIndices_, gridWeights_, gridScratch_);
    // FOR A CHECK (the environment's TGPT_TEST_RECORD_CHECK; record reads it once): the loop this took the place of, run
    // beside it, and every byte compared. Said in the log when they differ, and once when the first build agreed.
    if(recordCheck_) {
        std::vector<uint32_t> cells, indices; std::vector<float> weights;
        const light_grid::Counts was=light_grid::build_cells_reference(shape, corner, lights_.data(), lights_.size(), clusterLights_.data(), clusterLights_.size(), clusterBase,
                                                                       cells, indices, weights);
        const bool agree=cells==gridCells_ && indices==gridIndices_ && weights.size()==gridWeights_.size() &&
            (weights.empty() || std::memcmp(weights.data(), gridWeights_.data(), weights.size()*sizeof(float))==0) &&
            was.entries==counts.entries && was.most==counts.most && was.lit==counts.lit;
        if(!agree) log_throttled("PT record check: THE LIGHT GRID'S CELLS DIFFER from the loop they took the place of ("+std::to_string(counts.entries)+" entries against "+std::to_string(was.entries)+")");
        else if(!recordCheckSaid_[0]) { recordCheckSaid_[0]=true; log("PT record check: the light grid's cells are the bytes of the loop they took the place of ("+std::to_string(counts.entries)+" entries); checked at every build"); }
    }
    {
        std::lock_guard<std::mutex> s(statsMutex_);
        stats_.gridCellsLit=counts.lit; stats_.gridEntries=counts.entries; stats_.gridMostInCell=counts.most; ++stats_.gridBuilds;
    }
    const auto cumulate=light_grid::cumulate; // (a list's weights into cumulative ones: the far lists' too)
    const auto cellsDone=std::chrono::steady_clock::now();
    gridJobMs_[0]=std::chrono::duration<double, std::milli>(cellsDone-began).count();
    // The far lists after the cells (pt_common.hlsli light_lists, PT-50): no light stops at a reach. Per coarse cell of
    // kGridCoarse^3 cells, every light that falls off with distance alone (TGPT_LIGHT_NO_RANGE), weighted by its power over
    // its squared distance from the coarse cell, at least the coarse cell's size; after them, for a point outside the
    // grid, every point and spot light by its power. A far candidate the point's own cell lists counts nothing there, so
    // each light is counted once at every point, by its cell or by the far list. Entries in the order of their lights.
    // The lists hold every such light whatever its range, so they are kept from build to build (the grid is made anew on a
    // quarter of the game's frames, when a range or a position changed a little; these, 720 lists of every light, took 9 ms
    // with the game's 985 lights): made anew only when a light came, went, changed its kind or moved kFarStale metres, or
    // the grid moved. Their weights only say how often a light is drawn, never how much light it gives, so a light that
    // moved less is still counted right.
    const uint32_t cx=(dx+kGridCoarse-1)/kGridCoarse, cy=(dy+kGridCoarse-1)/kGridCoarse, cz=(dz+kGridCoarse-1)/kGridCoarse;
    const size_t coarseCount=size_t(cx)*cy*cz, listCount=cellCount+coarseCount+1;
    gridCells_.resize(listCount*2, 0);
    auto usable=[](const TGPT_Light& l) {
        return l.type<=1u && std::isfinite(l.position[0]) && std::isfinite(l.position[1]) && std::isfinite(l.position[2]);
    };
    auto falls_off=[&](const TGPT_Light& l) { return usable(l) && (l.flags&TGPT_LIGHT_NO_RANGE)!=0; };
    bool stale=farCells_.size()!=(coarseCount+1)*2 || farLights_.size()!=lights_.size() || std::memcmp(farCorner_, corner, sizeof(farCorner_))!=0;
    for(size_t i=0;i<lights_.size() && !stale;++i) {
        const TGPT_Light& l=lights_[i]; const FarLight& f=farLights_[i];
        if(usable(l)!=f.usable || falls_off(l)!=f.fallsOff) stale=true;
        else if(f.usable) {
            double moved=0;
            for(int a=0;a<3;++a) { const double d=double(l.position[a])-double(f.position[a]); moved+=d*d; }
            stale=moved>kFarStale*kFarStale;
        }
    }
    if(stale) {
        const double side=kGridCell*kGridCoarse, extent[3]={dx*kGridCell, dy*kGridCell, dz*kGridCell};
        auto power=[](const TGPT_Light& l) { return std::max(0.0, 0.2126*double(l.color[0])+0.7152*double(l.color[1])+0.0722*double(l.color[2])); };
        farCells_.assign((coarseCount+1)*2, 0); farEntries_.clear(); farLights_.resize(lights_.size());
        std::memcpy(farCorner_, corner, sizeof(farCorner_));
        std::vector<uint32_t> members; std::vector<float> weights;
        for(size_t i=0;i<lights_.size();++i) {
            FarLight& f=farLights_[i]; f.usable=usable(lights_[i]); f.fallsOff=falls_off(lights_[i]);
            for(int a=0;a<3;++a) f.position[a]=lights_[i].position[a];
            if(f.fallsOff) members.push_back(uint32_t(i));
        }
        weights.resize(std::max(members.size(), lights_.size()));
        for(uint32_t kz=0;kz<cz;++kz) for(uint32_t ky=0;ky<cy;++ky) for(uint32_t kx=0;kx<cx;++kx) {
            const size_t list=(size_t(kz)*cy+ky)*cx+kx;
            const double lo[3]={kx*side, ky*side, kz*side};
            const double hi[3]={std::min(lo[0]+side, extent[0]), std::min(lo[1]+side, extent[1]), std::min(lo[2]+side, extent[2])};
            const size_t first=farEntries_.size()/2;
            farEntries_.resize((first+members.size())*2, 0);
            for(size_t k=0;k<members.size();++k) {
                const TGPT_Light& l=lights_[members[k]];
                double nearest=0;
                for(int a=0;a<3;++a) {
                    const double p=double(l.position[a])-corner[a];
                    const double d=p<lo[a] ? lo[a]-p : (p>hi[a] ? p-hi[a] : 0.0);
                    nearest+=d*d;
                }
                farEntries_[(first+k)*2]=members[k]; weights[k]=float(power(l)/std::max(nearest, side*side));
            }
            farCells_[list*2]=uint32_t(first); farCells_[list*2+1]=uint32_t(members.size());
            if(!members.empty()) cumulate(weights.data(), uint32_t(members.size()), &farEntries_[first*2]);
        }
        const size_t first=farEntries_.size()/2; uint32_t count=0;
        for(size_t i=0;i<lights_.size();++i)
            if(farLights_[i].usable) { farEntries_.push_back(uint32_t(i)); farEntries_.push_back(0); weights[count++]=float(power(lights_[i])); }
        farCells_[coarseCount*2]=uint32_t(first); farCells_[coarseCount*2+1]=count;
        if(count) cumulate(weights.data(), count, &farEntries_[first*2]);
        farUploadDirty_=true; // (upload_light_lists: the far lists' own upload is made anew)
        std::lock_guard<std::mutex> s(statsMutex_);
        ++stats_.gridFarBuilds;
    }
    const uint32_t total=uint32_t(gridWeights_.size());
    for(size_t k=0;k<=coarseCount;++k) { gridCells_[(cellCount+k)*2]=total+farCells_[k*2]; gridCells_[(cellCount+k)*2+1]=farCells_[k*2+1]; }
    // The far lists' entries follow the cells' in the buffer the passes read, as they always did; they are no longer
    // appended to gridIndices_ here and uploaded with it at every build (PT-99: 720 lists of every light, 5.7 MB with a
    // town's 985 lights, copied twice a build though they are kept from build to build): upload_light_lists puts them there.
    // The two arrays the GPU takes (the cells, the cells' entries), left for it in the upload buffer record chose for
    // this build (gridStage_), where there is one and they fit: so the record, which waits for this build, has only the
    // two copies on the GPU to record, and does not copy 2 MB and more into the frame's ring itself. The same bytes.
    gridStaged_=false;
    const uint64_t cellsBytes=uint64_t(gridCells_.size())*4, entriesAt=(cellsBytes+15)/16*16, entriesBytes=uint64_t(gridIndices_.size())*4;
    gridStageWanted_=entriesAt+entriesBytes;
    if(gridStage_>=0) {
        const Buffer& stage=gridStages_[size_t(gridStage_)].buffer;
        if(stage && stage.mapped && gridStageWanted_<=stage.size) {
            std::memcpy(stage.mapped, gridCells_.data(), cellsBytes);
            if(entriesBytes) std::memcpy(static_cast<uint8_t*>(stage.mapped)+entriesAt, gridIndices_.data(), entriesBytes);
            gridStagedEntriesAt_=entriesAt; gridStaged_=true;
        }
    }
    gridJobMs_[1]=std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now()-cellsDone).count();
}

// The light lists' entries as the passes read them (gridIndexBuffer_): the cells' (gridIndices_), then the far lists'
// (farEntries_), one after the other, the same bytes at the same places as when both were one array uploaded whole.
// The cells' go through the frame's ring as before. The far lists' are copied on the GPU from an upload of their own,
// which is written once when they are made (build_light_grid: stale) and never again: a buffer the GPU may still read
// for a frame in flight is not written, so each new set of far lists gets a new one and the old goes when the GPU is
// past this frame.
void GpuScene::upload_light_lists(ID3D12Device5* device, ID3D12GraphicsCommandList4* list, DescriptorHeap& heap, UploadRing& ring,
                                  DeferredRelease& release, uint64_t retireValue, Barriers& barriers, ID3D12Resource* staged, uint64_t stagedAt) {
    // (`far` is a word of the Windows headers.)
    const uint32_t own=uint32_t(gridIndices_.size()/2), farCount=uint32_t(farEntries_.size()/2), count=own+farCount;
    if(farCount && (!farUpload_ || farUploadDirty_)) {
        if(farUpload_) release.add(retireValue, farUpload_.resource.Get());
        farUpload_=make_buffer(device, uint64_t(farCount)*8, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, "Create the far light lists' upload");
        std::memcpy(farUpload_.mapped, farEntries_.data(), size_t(farCount)*8);
        farUploadDirty_=false;
    }
    // (The buffer and its view as upload_structured makes them: 8 bytes a record, room doubled from 64.)
    Structured& target=gridIndexBuffer_;
    const uint32_t needed=std::max<uint32_t>(count, 1), stride=8;
    if(!target.buffer || target.capacity<needed || target.stride!=stride) {
        if(target.buffer) release.add(retireValue, target.buffer.resource.Get());
        heap.release(target.srv);
        uint32_t capacity=64; while(capacity<needed) capacity*=2;
        target.buffer=make_buffer(device, uint64_t(capacity)*stride, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COMMON, "Create scene record buffer");
        target.capacity=capacity; target.stride=stride;
        target.srv=structured_srv(device, heap, target.buffer.resource.Get(), capacity, stride);
    }
    if(!count) { barriers.transition(target.buffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE); return; }
    barriers.transition(target.buffer, D3D12_RESOURCE_STATE_COPY_DEST); barriers.flush(list);
    if(own) {
        const uint64_t bytes=uint64_t(own)*stride;
        void* cpu=nullptr; D3D12_GPU_VIRTUAL_ADDRESS gpu=0; uint64_t offset=0;
        // (`staged`: the build left gridIndices_' bytes in its own upload, at stagedAt: record.)
        if(staged) list->CopyBufferRegion(target.buffer.resource.Get(), 0, staged, stagedAt, bytes);
        else if(ring.allocate(bytes, 16, &cpu, &gpu, &offset)) {
            std::memcpy(cpu, gridIndices_.data(), bytes);
            list->CopyBufferRegion(target.buffer.resource.Get(), 0, ring.resource(), offset, bytes);
        } else {
            Buffer upload=make_buffer(device, bytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, "Create record upload buffer");
            std::memcpy(upload.mapped, gridIndices_.data(), bytes);
            list->CopyBufferRegion(target.buffer.resource.Get(), 0, upload.resource.Get(), 0, bytes);
            release.add(retireValue, upload.resource.Get());
        }
    }
    if(farCount) list->CopyBufferRegion(target.buffer.resource.Get(), uint64_t(own)*stride, farUpload_.resource.Get(), 0, uint64_t(farCount)*stride);
    barriers.transition(target.buffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
}

void GpuScene::upload_decals(ID3D12Device5* device, ID3D12GraphicsCommandList4* list, DescriptorHeap& heap, UploadRing& ring,
                             DeferredRelease& release, uint64_t retireValue, const double origin[3], Barriers& barriers) {
    const auto began=std::chrono::steady_clock::now();
    const uint64_t generation=share::generation();
    // A mesh decal's own top level names its mesh's bottom level BY ITS ADDRESS (below), and that address moves: every still
    // mesh's structure is copied compacted to another place a few frames after its build (compact_meshes, which gives the
    // old place back), and a mesh let go gives its own back (retire_meshes). Neither marks the decals. Until 2026-10-12
    // nothing looked again unless a mesh was converted in that very frame, a texture came or went or the origin moved:
    // a decal whose mesh was compacted in a quiet frame kept a top level that pointed at free room, at whatever was built
    // there next, or, once its chunk had gone back to the driver, at nothing: a ray query through it is bounded by
    // nothing of ours (PT-125's kind of fault; docs/agents/stale.md F1). So the address each named top level was built
    // with is checked against its mesh's here, every frame, before the early way out.
    bool moved=false;
    for(const DecalMesh& cache:decalMeshes_) {
        if(!cache.mesh) continue;
        const Mesh* mesh=cache.mesh<=meshes_.size() ? meshes_[cache.mesh-1].get() : nullptr;
        if(!mesh || mesh->state!=Mesh::State::Ready || mesh->blasVa!=cache.instance.AccelerationStructure) { moved=true; break; }
    }
    if(!decalsDirty_ && !moved && generation==decalShareGeneration_ && std::memcmp(origin, decalOrigin_, sizeof(decalOrigin_))==0) return;
    decalsDirty_=false; decalShareGeneration_=generation; std::memcpy(decalOrigin_, origin, sizeof(decalOrigin_));
    while(decalMeshes_.size()>decals_.size()) {
        auto& old=decalMeshes_.back(); if(old.tlas) release.add(retireValue,old.tlas.resource.Get()); heap.release(old.srv); decalMeshes_.pop_back();
    }
    decalMeshes_.resize(decals_.size());
    if(decals_.empty()) { decalNodeCount=0; return; }
    auto gpu=decals_;
    std::vector<int32_t> handles; handles.reserve(gpu.size()*12);
    for(const auto& d:gpu) { for(int32_t h:d.textures) handles.push_back(h); for(int32_t h:d.graphTextures) handles.push_back(h); }
    share::resolve(handles.data(), handles.size());
    for(size_t i=0;i<gpu.size();++i) {
        auto& d=gpu[i];
        decalMeshes_[i].mesh=0; // (no record names its top level, until this decal's does below)
        for(int a=0;a<3;++a) {
            d.boundsMin[a]=float(double(d.boundsMin[a])-origin[a]);
            d.boundsMax[a]=float(double(d.boundsMax[a])-origin[a]);
            double offset=d.worldToDecal[4*a+3];
            for(int k=0;k<3;++k) offset+=double(d.worldToDecal[4*a+k])*origin[k];
            d.worldToDecal[4*a+3]=float(offset);
        }
        bool missing=false;
        for(int k=0;k<4;++k) { missing|=d.textures[k]>0 && handles[12*i+k]<0; if(d.textures[k]>=0) d.textures[k]=handles[12*i+k]; }
        for(int k=0;k<8;++k) { missing|=d.graphTextures[k]>0 && handles[12*i+4+k]<0; if(d.graphTextures[k]>=0) d.graphTextures[k]=handles[12*i+4+k]; }
        if(missing) d.flags=0; // missing live resource is a reported capture gap, never a copied texture
        if((d.flags&1024u)!=0) {
            const uint32_t handle=d.reserved[0];
            Mesh* mesh=handle>0 && handle<=meshes_.size() ? meshes_[handle-1].get() : nullptr;
            if(!mesh || mesh->state!=Mesh::State::Ready || handle>=meshRecords_.size() || d.reserved[1]>=meshRecords_[handle].geometryCount) { d.flags=0; continue; }
            D3D12_RAYTRACING_INSTANCE_DESC instance{};
            for(int r=0;r<3;++r) for(int c=0;c<4;++c) instance.Transform[r][c]=c==3 ? float(double(d.graphWorld[r*4+c])-origin[r]) : d.graphWorld[r*4+c];
            instance.InstanceMask=255;
            instance.Flags=D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE|D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_NON_OPAQUE;
            instance.AccelerationStructure=mesh->blasVa;
            auto& cache=decalMeshes_[i];
            if(!cache.tlas || std::memcmp(&instance,&cache.instance,sizeof(instance))!=0) {
                D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build{};
                build.Inputs.Type=D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
                build.Inputs.Flags=D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
                build.Inputs.NumDescs=1; build.Inputs.DescsLayout=D3D12_ELEMENTS_LAYOUT_ARRAY;
                D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info{};
                device->GetRaytracingAccelerationStructurePrebuildInfo(&build.Inputs,&info);
                if(cache.tlas.size<info.ResultDataMaxSizeInBytes) {
                    if(cache.tlas) release.add(retireValue,cache.tlas.resource.Get()); heap.release(cache.srv);
                    cache.tlas=make_buffer(device,info.ResultDataMaxSizeInBytes,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                        D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,"Create mesh decal TLAS");
                    cache.srv=heap.allocate(); D3D12_SHADER_RESOURCE_VIEW_DESC view{};
                    view.ViewDimension=D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE; view.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                    view.RaytracingAccelerationStructure.Location=cache.tlas.va(); device->CreateShaderResourceView(nullptr,&view,heap.cpu(cache.srv));
                }
                if(decalScratch_.size<info.ScratchDataSizeInBytes) {
                    if(decalScratch_) release.add(retireValue,decalScratch_.resource.Get());
                    decalScratch_=make_buffer(device,info.ScratchDataSizeInBytes,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,"Create mesh decal TLAS scratch");
                }
                void* cpu=nullptr; D3D12_GPU_VIRTUAL_ADDRESS uploadVa=0; Buffer own;
                if(!ring.allocate(sizeof(instance),D3D12_RAYTRACING_INSTANCE_DESCS_BYTE_ALIGNMENT,&cpu,&uploadVa)) {
                    own=make_buffer(device,sizeof(instance),D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_FLAG_NONE,D3D12_RESOURCE_STATE_GENERIC_READ,"Upload mesh decal instance");
                    cpu=own.mapped; uploadVa=own.va(); release.add(retireValue,own.resource.Get());
                }
                std::memcpy(cpu,&instance,sizeof(instance)); build.Inputs.InstanceDescs=uploadVa;
                build.DestAccelerationStructureData=cache.tlas.va(); build.ScratchAccelerationStructureData=decalScratch_.va();
                list->BuildRaytracingAccelerationStructure(&build,0,nullptr);
                ++structureWork[kStructureDecals].builds;
                barriers.uav(nullptr); barriers.flush(list); cache.instance=instance;
            }
            d.reserved[2]=cache.srv; cache.mesh=handle;
        }
    }
    auto& nodes=decalNodes_;
    bool treeChanged=nodes.size()!=gpu.size()*2-1;
    if(!treeChanged) for(const auto& node:nodes) if(node.decal!=kInvalid) {
        const auto& d=gpu[node.decal];
        if(std::memcmp(node.lo,d.boundsMin,12)!=0 || std::memcmp(node.hi,d.boundsMax,12)!=0 || node.mesh!=uint32_t((d.flags&1024u)!=0)) { treeChanged=true; break; }
    }
    if(treeChanged) { nodes.clear(); nodes.reserve(gpu.size()*2-1); }
    // The tree is split by PLACE (round 20): each split at the median of the boxes' centres along the axis they spread
    // most, a leaf naming its decal's draw-order index, and every node the least and the most index under it (pad[0],
    // pad[1]), so that apply_decals (pt_common) can take the decals a point lies in in draw order, one at a time. Until
    // round 20 the tree was split by draw order alone: its boxes spanned the whole scene, and every hit walked most of its
    // nodes (round 19: the traced decals cost a room about 4 ms a frame).
    std::vector<uint32_t> order(gpu.size()); std::iota(order.begin(), order.end(), 0u);
    const auto centre=[&](uint32_t i, int a) { const float c=0.5f*(gpu[i].boundsMin[a]+gpu[i].boundsMax[a]); return std::isfinite(c) ? c : 0.f; };
    const auto build=[&](auto&& self, uint32_t first, uint32_t end)->uint32_t {
        const uint32_t here=uint32_t(nodes.size()); nodes.push_back({});
        if(end-first==1) {
            const uint32_t i=order[first];
            std::copy(gpu[i].boundsMin,gpu[i].boundsMin+3,nodes[here].lo);
            std::copy(gpu[i].boundsMax,gpu[i].boundsMax+3,nodes[here].hi);
            nodes[here].decal=i; nodes[here].mesh=(gpu[i].flags&1024u)!=0; nodes[here].pad[0]=nodes[here].pad[1]=i;
        } else {
            float lo[3]={INFINITY, INFINITY, INFINITY}, hi[3]={-INFINITY, -INFINITY, -INFINITY};
            for(uint32_t k=first;k<end;++k) for(int a=0;a<3;++a) { lo[a]=std::min(lo[a], centre(order[k], a)); hi[a]=std::max(hi[a], centre(order[k], a)); }
            int axis=0; for(int a=1;a<3;++a) if(hi[a]-lo[a]>hi[axis]-lo[axis]) axis=a;
            const uint32_t mid=first+(end-first)/2;
            std::nth_element(order.begin()+first, order.begin()+mid, order.begin()+end, [&](uint32_t x, uint32_t y) {
                const float cx=centre(x, axis), cy=centre(y, axis); return cx<cy || (cx==cy && x<y); });
            const uint32_t left=self(self,first,mid), right=self(self,mid,end);
            for(int a=0;a<3;++a) { nodes[here].lo[a]=std::min(nodes[left].lo[a],nodes[right].lo[a]); nodes[here].hi[a]=std::max(nodes[left].hi[a],nodes[right].hi[a]); }
            nodes[here].decal=kInvalid; nodes[here].mesh=nodes[left].mesh|nodes[right].mesh;
            nodes[here].pad[0]=std::min(nodes[left].pad[0], nodes[right].pad[0]); nodes[here].pad[1]=std::max(nodes[left].pad[1], nodes[right].pad[1]);
        }
        nodes[here].next=uint32_t(nodes.size()); return here;
    };
    if(treeChanged) build(build,0,uint32_t(gpu.size()));
    upload_structured(device,list,heap,ring,release,retireValue,decalBuffer_,gpu.data(),uint32_t(gpu.size()),sizeof(TGPT_Decal),barriers);
    if(treeChanged || !decalNodeBuffer_.buffer) upload_structured(device,list,heap,ring,release,retireValue,decalNodeBuffer_,nodes.data(),uint32_t(nodes.size()),sizeof(DecalNode),barriers);
    decalsSrv=decalBuffer_.srv; decalNodesSrv=decalNodeBuffer_.srv; decalNodeCount=uint32_t(nodes.size());
    const uint64_t now=GetTickCount64();
    if(now-decalLastLog_>=10000) {
        decalLastLog_=now; size_t ready=0,meshes=0; uint64_t acceleration=0;
        for(const auto& d:gpu) { ready+=d.flags!=0; meshes+=(d.flags&1024u)!=0; }
        for(const auto& d:decalMeshes_) acceleration+=d.tlas.size;
        const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-began).count();
        log("Decal GPU inputs: "+std::to_string(ready)+" ready ("+std::to_string(meshes)+" meshes), "+std::to_string(gpu.size()-ready)+
            " unavailable; "+std::to_string(gpu.size()*sizeof(TGPT_Decal)+nodes.size()*sizeof(DecalNode))+" record/tree bytes, "+
            std::to_string(acceleration)+" acceleration bytes; upload CPU "+std::to_string(ms)+" ms, tree "+(treeChanged ? "rebuilt" : "reused"));
    }
}

void GpuScene::clear(DescriptorHeap* heap, DeferredRelease& release, uint64_t retireValue) {
    {
        // The worker may still hold a mesh: let it finish, and drop what it has not started.
        std::unique_lock<std::mutex> lock(workMutex_);
        work_.clear();
        idleSignal_.wait_for(lock, std::chrono::seconds(10), [this]{ return converting_==nullptr; });
        if(converting_) {
            // A mesh that never finishes converting keeps its object for the rest of the process.
            for(auto& m:meshes_) if(m.get()==converting_) (void)m.release();
        }
        done_.clear();
    }
    for(auto& m:meshes_) {
        if(!m || !heap) continue;
        heap->release(m->positionsSrv); heap->release(m->uvsSrv); heap->release(m->colorsSrv); heap->release(m->indicesSrv); heap->release(m->normalsSrv);
        heap->release(m->uvs2Srv);
        heap->release(m->tangentsSrv); heap->release(m->decalColorsSrv);
        heap->release(m->skinUav); heap->release(m->skinPreviousUav); heap->release(m->skinPreviousSrv);
        heap->release(m->skinNormalsUav); heap->release(m->skinNormalsSrv);
        if(m->skinNormals) release.add(retireValue, m->skinNormals.resource.Get());
        if(m->skinPrevious) release.add(retireValue, m->skinPrevious.resource.Get());
        if(m->skinPositions) release.add(retireValue, m->skinPositions.resource.Get());
    }
    for(auto& m:meshes_) if(m && m->ommIndices) release.add(retireValue, m->ommIndices.resource.Get()); // (PT-110's M1: the micromaps' kept index buffers)
#if TGPT_MEGA_GEOMETRY
    for(auto& m:meshes_) if(m && m->megaMaps.array) release.add(retireValue, m->megaMaps.array.resource.Get()); // (megageo-5: the clusters' micromap arrays)
#endif
    for(auto& [key, c]:swayCopies_) release_sway_copy(c, heap, release, retireValue);
    for(auto& [cell, g]:plantGroups_) release_plant_group(g, heap, release, retireValue);
    plantGroups_.clear(); uploadedPlantGroups_.clear(); plantRecords_.clear(); uploadedPlantRecords_.clear(); plantGeometries_.clear(); uploadedPlantGeometries_.clear();
    for(auto& batch:plantCompactBatches_) release.add(retireValue, batch.readback.resource.Get());
    plantCompactBatches_.clear(); plantCompactBefore_=plantCompactAfter_=0; plantCompacted_=0;
    plantWinds_.clear(); plantWindOf_.clear(); plantWindTable_.clear(); plantWindPastTable_=false; uploadedPlantWinds_.clear(); windNoise_.reset(); uploadedNoise_.reset();
    swayCopies_.clear(); swayRecords_.clear(); uploadedSwayRecords_.clear(); winds_.clear(); clothWindy_.clear(); clothWindPastTable_=false;
    if(swayRecordBuffer_.buffer) release.add(retireValue, swayRecordBuffer_.buffer.resource.Get());
    if(heap) heap->release(swayRecordBuffer_.srv);
    swayRecordBuffer_=Structured{};
    for(auto& batch:compactBatches_) release.add(retireValue,batch.readback.resource.Get());
    compactBatches_.clear(); compactBefore_=compactAfter_=0; ommCompactBefore_=ommCompactAfter_=0; ommCompacted_=0;
    meshes_.clear(); pending_.clear(); retiredMeshes_.clear();
    ++meshEpoch_; plantPlan_=PlantPlan{}; // (PT-99: the plants' kept list goes with the scene it was made from)
    ommQueue_.clear(); tilesSettled_.clear(); alphaTilesSettled_.clear(); materialSignatures_.clear(); combinedLast_=true; ommInputBytes_=0; ommBudgetLogged_=false;
    for(auto& back:ommReadbacks_) release.add(retireValue, back.buffer.resource.Get()); // (PT-110: bakes not counted yet)
    ommReadbacks_.clear();
    for(auto& retired:ommIndicesRetired_) release.add(retireValue, retired.second.resource.Get()); // (M1: index buffers on their way out)
    ommIndicesRetired_.clear();

    for(auto& d:decalMeshes_) { if(d.tlas) release.add(retireValue,d.tlas.resource.Get()); if(heap) heap->release(d.srv); }
    decalMeshes_.clear(); decalNodes_.clear(); if(decalScratch_) release.add(retireValue,decalScratch_.resource.Get()); decalScratch_=Buffer{};
    geometryArena.release_all(release, retireValue);
    accelerationArena.release_all(release, retireValue);
    buildArena.release_all(release, retireValue);
    decalsSrv=decalNodesSrv=kInvalid; decalNodeCount=0; decalsDirty_=true;
    previousLights_.clear(); previousLightCount=0;
    mapRecords_.clear(); gpuMapRecords_.clear(); mapTextures_.clear(); mapRecordsDirty_=false;
    programRows_.clear(); gpuProgramRows_.clear(); programsDirty_=true;
    for(Structured* s:{&meshBuffer_, &geometryBuffer_, &materialBuffer_, &indexListBuffer_, &instanceBuffer_, &lightBuffer_, &previousLightBuffer_,
                       &emitterListBuffer_, &slotOwnerBuffer_, &clusterInfoBuffer_, &emitterTableBuffer_, &mapRecordsBuffer_, &programBuffer_, &movingPreviousBuffer_, &gridCellBuffer_, &gridIndexBuffer_,
                       &plantGroupBuffer_, &plantMemberBuffer_, &plantRangeBuffer_, &plantWindBuffer_, &noiseBuffer_, &decalBuffer_, &decalNodeBuffer_, &waterRecordBuffer_}) {
        if(s->buffer) release.add(retireValue, s->buffer.resource.Get());
        if(heap) heap->release(s->srv);
        *s=Structured{};
    }
    for(size_t i=0;i<2;++i) {
        if(emitterRecords_[i]) release.add(retireValue, emitterRecords_[i].resource.Get());
        if(heap) { heap->release(emitterRecordsSrv_[i]); heap->release(emitterRecordsUav_[i]); }
        emitterRecords_[i]=Buffer{}; emitterRecordsSrv_[i]=emitterRecordsUav_[i]=kInvalid; emitterRecordsCapacity_[i]=emitterRecordsCount_[i]=0;
    }
    emitterSlots_.clear(); freeSlots_.clear(); slotCapacity_=0; emitterCurrent_=-1; emitterPassPending_=false; emittersDirty_=true; emitterLightsHash_=0;
    emitterList_.clear(); emitterWorld_.clear(); slotOwners_.clear(); emitterTable_.clear(); clusterInfo_.clear(); clusterLights_.clear(); brightestEmitter_=0.f;
    brightestEmitterRaw_=0.f;
    emittersSrv=previousEmittersSrv=emitterTableSrv=kInvalid; emitterCount=previousEmitterCount=emitterTableInstances=sampledClusters=clusterBase=0;
    for(Buffer* b:{&scratch_, &tlasScratch_, &tlasBuffer_, &ommScratch_, &skinScratch_, &plantScratch_, &swayScratch_, &farUpload_}) { if(*b) release.add(retireValue, b->resource.Get()); *b=Buffer{}; }
    farUploadDirty_=true; // (the far lists themselves stay, as before: their upload is made again at the next build)
    for(auto& stage:gridStages_) { if(stage.buffer) release.add(retireValue, stage.buffer.resource.Get()); stage=GridStage{}; }
    gridStage_=-1; gridStaged_=false;
#if TGPT_MEGA_GEOMETRY
    megaFresh_.clear();
    megaTop_.release(release, retireValue); // PT-109: the partitioned top level goes with the scene (written whole at its next use)
    // (And the small plants' clusters, on trial: the meshes' cuts and templates, their read-backs, the frame's buffers.)
    for(auto& [handle, cut]:megaMeshes_) mega_release(cut, heap, release, retireValue);
    megaMeshes_.clear(); megaWanted_.clear(); megaBoxes_.clear();
    for(auto& [handle, r]:megaReadbacks_) release.add(retireValue, r.buffer.resource.Get());
    megaReadbacks_.clear();
    megaBuilder_.release(release, retireValue);
#endif
    meshRecords_.clear(); geometryRecords_.clear();
    materialsDirty_=indicesDirty_=meshRecordsDirty_=true;
    instances_.clear(); materialIndices_.clear(); sceneList_.reset(); gridBuilt_=false; gridKey_.clear(); gridKeyLights_=0;
    ++listEpoch_; // (PT-99: the list is gone; the next one is another)
    hasTlas=false; tlas=0; movingPreviousSrv=kInvalid; moving_.reset(); replaced_.clear(); replacedGlowHash_=0;
    waterRecordsSrv=kInvalid; waterRecordCount=0;
    release_water(heap, release, retireValue); sceneHasWater_=false;
    std::lock_guard<std::mutex> s(statsMutex_);
    stats_=SceneStats{};
}

void GpuScene::compact_meshes(ID3D12Device5* device, ID3D12GraphicsCommandList4* list, uint64_t fence) {
    uint64_t before=0,after=0; uint32_t count=0;
    uint64_t ommBefore=0, ommAfter=0; uint32_t ommCount=0; // (PT-110's M5: the structures with micromaps)
    while(!compactBatches_.empty() && compactBatches_.front().fence<=completed_) {
        auto& batch=compactBatches_.front(); const auto* sizes=static_cast<const uint64_t*>(batch.readback.mapped);
        for(size_t i=0;i<batch.entries.size();++i) {
            const auto& entry=batch.entries[i];
            Mesh* m=entry.handle>0 && size_t(entry.handle)<=meshes_.size() ? meshes_[entry.handle-1].get() : nullptr;
            const uint64_t bytes=(sizes[i]+255)&~255ull;
            // M5 (PT-110): a mesh's structure with micromaps, compacted as its plain one is. The same triangles and the
            // same links to the same micromap array, which stays where it is: no ray can tell. Only the very build the
            // size was read from (its address, and which build of the mesh's micromaps it is).
            if(entry.omm) {
                if(!m || m->omm!=Mesh::Omm::Linked || m->ommBlasVa!=entry.source || m->ommSerial!=entry.serial || !bytes || bytes>=entry.bytes) continue;
                auto compact=accelerationArena.allocate(device,bytes,256,"Create compact BLAS storage");
                list->CopyRaytracingAccelerationStructure(compact.va(),m->ommBlasVa,D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE_COMPACT);
                accelerationArena.retire(m->ommBlasVa,fence);
                auto& allocations=m->accelerationAllocations;
                allocations.erase(std::remove(allocations.begin(),allocations.end(),m->ommBlasVa),allocations.end()); allocations.push_back(compact.va());
                const uint64_t saved=entry.bytes>compact.size ? entry.bytes-compact.size : 0;
                ommBefore+=entry.bytes; ommAfter+=compact.size; ++ommCount;
                { auto& w=structureWork[kStructureCompaction]; ++w.builds; w.triangles+=m->triangles; w.bytes+=compact.size; }
                { std::lock_guard<std::mutex> lock(statsMutex_); stats_.accelerationBytes-=saved; stats_.ommBytes-=saved; }
                m->ommBytes-=std::min(m->ommBytes, saved); m->ommBlasVa=compact.va();
#if TGPT_MEGA_GEOMETRY
                mega_fresh(compact.va()); // PT-109, as build_blas
#endif
                continue;
            }
            // (One built in the full-size builds' own chunks leaves them even where compaction saves nothing: buildArena.)
            if(!m || m->blasVa!=entry.source || !bytes || (bytes>=m->blasBytes && !buildArena.owns(m->blasVa))) continue;
            auto compact=accelerationArena.allocate(device,bytes,256,"Create compact BLAS storage");
            list->CopyRaytracingAccelerationStructure(compact.va(),m->blasVa,D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE_COMPACT);
            retire_structure(m->blasVa,fence);
            auto& allocations=m->accelerationAllocations;
            allocations.erase(std::remove(allocations.begin(),allocations.end(),m->blasVa),allocations.end()); allocations.push_back(compact.va());
            before+=m->blasBytes; after+=compact.size; ++count;
            { auto& w=structureWork[kStructureCompaction]; ++w.builds; w.triangles+=m->triangles; w.bytes+=compact.size; }
            { std::lock_guard<std::mutex> lock(statsMutex_); stats_.accelerationBytes-=m->blasBytes-compact.size; }
            m->blasVa=compact.va(); m->blasBytes=compact.size;
#if TGPT_MEGA_GEOMETRY
            mega_fresh(compact.va()); // PT-109, as build_blas
#endif
        }
        compactBatches_.pop_front();
    }
    if(count) {
        D3D12_RESOURCE_BARRIER barrier{}; barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV; list->ResourceBarrier(1,&barrier);
        compactBefore_+=before; compactAfter_+=after;
        log("BLAS compaction: "+std::to_string(count)+" static meshes, "+std::to_string(before)+" -> "+std::to_string(after)+
            " bytes; scene saved "+std::to_string(compactBefore_-compactAfter_)+" bytes (same triangles)");
    }
    if(ommCount) { // (PT-110's M5)
        if(!count) { D3D12_RESOURCE_BARRIER barrier{}; barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV; list->ResourceBarrier(1,&barrier); }
        ommCompactBefore_+=ommBefore; ommCompactAfter_+=ommAfter; ommCompacted_+=ommCount;
        log("BLAS compaction: "+std::to_string(ommCount)+" structures with micromaps, "+std::to_string(ommBefore)+" -> "+std::to_string(ommAfter)+
            " bytes; since the traced scene started: "+std::to_string(ommCompacted_)+" of them, "+std::to_string(ommCompactBefore_)+" -> "+std::to_string(ommCompactAfter_)+
            " bytes (the same triangles, the same micromaps)");
    }
}

void GpuScene::memory(TGPT_Diagnostics& d) const {
    for(const auto& batch:compactBatches_) d.transfer+=allocation_bytes(batch.readback.resource.Get());
    d.transfer+=allocation_bytes(farUpload_.resource.Get()); // (the far light lists' own upload: upload_light_lists)
    for(const auto& stage:gridStages_) d.transfer+=allocation_bytes(stage.buffer.resource.Get()); // (the light grid's: record)
    d.meshGeometry=geometryArena.bytes(); d.acceleration=accelerationArena.bytes()+buildArena.bytes()+allocation_bytes(tlasBuffer_.resource.Get());
    for(const Buffer* b:{&scratch_, &tlasScratch_, &ommScratch_, &skinScratch_, &swayScratch_, &plantScratch_}) d.scratch+=allocation_bytes(b->resource.Get());
#if TGPT_MEGA_GEOMETRY
    d.acceleration+=megaTop_.bytes(); d.scratch+=megaTop_.scratch_bytes(); // PT-109: the partitioned top level, while it serves
    // (PT-109's small plants as clusters, on trial: the groups' clusters with the plants', the meshes' cut orders with the
    // geometry, their templates with the structures, the frame's scratch and address arrays with the scratch, read-backs.)
    for(const auto& pair:plantGroups_) d.plants+=allocation_bytes(pair.second.clas.resource.Get());
    for(const auto& [handle, cut]:megaMeshes_) {
        d.meshGeometry+=allocation_bytes(cut.order.resource.Get());
        for(const auto& [serial, b]:cut.ommOrders) d.meshGeometry+=allocation_bytes(b.resource.Get());
        for(const auto& [key, t]:cut.templates) d.acceleration+=allocation_bytes(t.buffer.resource.Get());
    }
    for(const auto& m:meshes_) if(m) d.acceleration+=allocation_bytes(m->megaMaps.array.resource.Get()); // (megageo-5: the clusters' micromap arrays)
    d.scratch+=megaBuilder_.bytes();
    d.transfer+=megaBuilder_.readback_bytes();
    for(const auto& pair:megaReadbacks_) d.transfer+=allocation_bytes(pair.second.buffer.resource.Get());
#endif
    for(const auto& m:meshes_) if(m) d.skinned+=allocation_bytes(m->skinPositions.resource.Get())+allocation_bytes(m->skinPrevious.resource.Get())+
        allocation_bytes(m->skinNormals.resource.Get());
    for(const auto& pair:swayCopies_) d.cloth+=allocation_bytes(pair.second.positions.resource.Get())+allocation_bytes(pair.second.blas.resource.Get());
    for(const auto& pair:plantGroups_) d.plants+=allocation_bytes(pair.second.data.resource.Get())+allocation_bytes(pair.second.blas.resource.Get());
    for(const Structured* b:{&meshBuffer_, &geometryBuffer_, &materialBuffer_, &indexListBuffer_, &instanceBuffer_, &lightBuffer_, &previousLightBuffer_,
        &mapRecordsBuffer_, &gridCellBuffer_, &gridIndexBuffer_, &emitterListBuffer_, &slotOwnerBuffer_, &clusterInfoBuffer_, &emitterTableBuffer_,
        &swayRecordBuffer_, &plantGroupBuffer_, &plantMemberBuffer_, &plantRangeBuffer_, &plantWindBuffer_, &noiseBuffer_, &programBuffer_, &movingPreviousBuffer_}) d.records+=allocation_bytes(b->buffer.resource.Get());
    for(const auto& b:emitterRecords_) d.records+=allocation_bytes(b.resource.Get());
    // The water's surface (scene_water.cpp), each part in the figure of what it is: its pieces' vertices and the one pattern
    // of indices with the meshes' geometry, their acceleration structures and scratch with the others', its records, and a
    // water mesh's read-back while it is on its way. water_memory() is their sum, for the log.
    for(const auto& page:waterVertexPool_.pages) d.meshGeometry+=allocation_bytes(page.buffer.resource.Get());
    d.meshGeometry+=allocation_bytes(waterIndices_.resource.Get());
    for(const auto& page:waterBlasPool_.pages) d.acceleration+=allocation_bytes(page.buffer.resource.Get());
    d.scratch+=allocation_bytes(waterScratch_.resource.Get());
    for(const Structured* b:{&waterRecordBuffer_, &waterTriBuffer_, &waterTableBuffer_}) d.records+=allocation_bytes(b->buffer.resource.Get());
    for(const auto& pair:waterReadbacks_) d.transfer+=allocation_bytes(pair.second.buffer.resource.Get());
}

uint64_t GpuScene::water_memory() const {
    uint64_t bytes=allocation_bytes(waterIndices_.resource.Get())+allocation_bytes(waterScratch_.resource.Get());
    for(const WaterPool* pool:{&waterVertexPool_, &waterBlasPool_}) for(const auto& page:pool->pages) bytes+=allocation_bytes(page.buffer.resource.Get());
    for(const Structured* b:{&waterRecordBuffer_, &waterTriBuffer_, &waterTableBuffer_}) bytes+=allocation_bytes(b->buffer.resource.Get());
    for(const auto& pair:waterReadbacks_) bytes+=allocation_bytes(pair.second.buffer.resource.Get());
    return bytes;
}

// PT-87 (scene.hpp DistanceBands): counted, nothing cut. The meshes' own bytes (what they take of the arenas' chunks: the
// log names the chunks' whole beside them), not the chunks'.
void GpuScene::distance_bands(const double camera[3], const std::vector<uint64_t>& textureBytes, DistanceBands& out) const {
    out=DistanceBands{};
    const int none=DistanceBands::kBands;
    std::vector<float> meshNearest(meshes_.size()+1, 1e30f), materialNearest(gpuMaterials_.size(), 1e30f);
    for(const auto& inst:instances_) {
        if((inst.flags&TGPT_INSTANCE_HIDDEN) || inst.mesh<=0 || size_t(inst.mesh)>meshes_.size()) continue;
        const double dx=double(inst.transform[3])-camera[0], dy=double(inst.transform[7])-camera[1], dz=double(inst.transform[11])-camera[2];
        const double distance=std::sqrt(dx*dx+dy*dy+dz*dz);
        if(!std::isfinite(distance)) continue;
        const int b=DistanceBands::band(distance);
        ++out.instances[b];
        if(const Mesh* m=meshes_[size_t(inst.mesh)-1].get()) out.instanceTriangles[b]+=m->triangles;
        meshNearest[size_t(inst.mesh)]=std::min(meshNearest[size_t(inst.mesh)], float(distance));
        if(meshRecords_.size()<=size_t(inst.mesh)) continue;
        const MeshRecordGpu& own=meshRecords_[size_t(inst.mesh)];
        for(uint32_t gi=0;gi<own.geometryCount && uint64_t(inst.materialBase)+gi<materialIndices_.size();++gi) {
            const uint32_t material=materialIndices_[inst.materialBase+gi];
            if(material<materialNearest.size()) materialNearest[material]=std::min(materialNearest[material], float(distance));
        }
    }
    for(size_t i=0;i<meshes_.size();++i) {
        const Mesh* m=meshes_[i].get();
        if(!m || m->state!=Mesh::State::Ready) continue;
        const int b=meshNearest[i+1]<1e29f ? DistanceBands::band(double(meshNearest[i+1])) : none;
        ++out.meshes[b]; out.geometry[b]+=m->bytesOnGpu; out.acceleration[b]+=m->blasBytes+m->ommBytes;
    }
    for(const auto& [cell, g]:plantGroups_) {
        const double dx=g.origin[0]+0.5*kPlantCell-camera[0], dy=g.origin[1]-camera[1], dz=g.origin[2]+0.5*kPlantCell-camera[2]; // (its cell's middle)
        const int b=DistanceBands::band(std::sqrt(dx*dx+dy*dy+dz*dz));
        ++out.plantGroups[b]; out.plantTriangles[b]+=g.triangles;
        out.plants[b]+=allocation_bytes(g.data.resource.Get())+allocation_bytes(g.blas.resource.Get());
#if TGPT_MEGA_GEOMETRY
        out.plants[b]+=allocation_bytes(g.clas.resource.Get()); // (PT-109 on trial: its clusters)
#endif
    }
    water_bands(out);
    // The game's textures the tracer holds open, each at the nearest instance with a material that reads it (the materials
    // as uploaded name them by their number).
    std::vector<float> textureNearest(textureBytes.size(), 1e30f);
    for(size_t material=0;material<gpuMaterials_.size();++material) {
        if(!(materialNearest[material]<1e29f)) continue;
        const TGPT_Material& m=gpuMaterials_[material];
        for(const int32_t number:{m.colorTexture, m.normalTexture, m.maskTexture, m.emissiveTexture})
            if(number>=0 && size_t(number)<textureNearest.size()) textureNearest[size_t(number)]=std::min(textureNearest[size_t(number)], materialNearest[material]);
    }
    for(size_t number=0;number<textureBytes.size();++number) {
        if(!textureBytes[number]) continue;
        const int b=textureNearest[number]<1e29f ? DistanceBands::band(double(textureNearest[number])) : none;
        ++out.textureCount[b]; out.textures[b]+=textureBytes[number];
    }
}

void GpuScene::collect(uint64_t completed) {
    completed_=completed; geometryArena.collect(completed); accelerationArena.collect(completed); buildArena.collect(completed);
#if TGPT_MEGA_GEOMETRY
    std::string checked, error;
    if(!megaBuilder_.check(completed, checked, error)) mega_fail("size readback: "+error);
    if(!checked.empty()) log("Mega Geometry size check: "+checked);
#endif
    // PT-87, in the log when it has changed, at most every 10 s: what the two arenas of acceleration structures hold and
    // what lives in them (GpuScene::arena_use; before memory-2's first cut both were one arena).
    static uint64_t told=0, toldAt=0;
    const ArenaUse use=arena_use();
    const uint64_t stamp=use.keptChunks^(use.buildChunks<<1)^(use.geometryChunks<<2)^(use.keptUsed*3)^(use.buildUsed*5)^(use.geometryUsed*7), now=GetTickCount64();
    // (The check reads this line for the figures no export has: with its switch set, to anything, every change is said.)
    if(stamp!=told && (cutsTest || now-toldAt>=10000)) {
        told=stamp; toldAt=now;
        char line[400];
        std::snprintf(line, sizeof line, "PT memory, the arenas (MB): the structures that stay %.1f in chunks, %.1f of it in use (%.1f free inside them); full-size builds waiting "
                      "to be compacted %.1f in chunks, %.1f in use (the chunks go back %u frames after the last leaves)%s; mesh geometry %.1f in chunks, %.1f in use",
                      use.keptChunks/1048576.0, use.keptUsed/1048576.0, (use.keptChunks-std::min(use.keptChunks, use.keptUsed))/1048576.0, use.buildChunks/1048576.0,
                      use.buildUsed/1048576.0, unsigned(kBuildChunkKeep), (cutsOff&kMemory2BuildChunks) ? " [the test's switch: one arena for both, as before]" : "",
                      use.geometryChunks/1048576.0, use.geometryUsed/1048576.0);
        log(line);
    }
}
GpuScene::ArenaUse GpuScene::arena_use() const {
    return ArenaUse{geometryArena.bytes(), geometryArena.used, accelerationArena.bytes(), accelerationArena.used, buildArena.bytes(), buildArena.used};
}

void GpuScene::retire_meshes(DescriptorHeap& heap, DeferredRelease& release, uint64_t fence, const std::vector<TGPT_SkinnedInstance>& skinned) {
    if(retiredMeshes_.empty()) return;
    std::unordered_set<int32_t> live;
    for(const auto& i:instances_) live.insert(i.mesh);
    for(const auto& i:skinned) live.insert(i.mesh);
    if(moving_) for(const auto& i:moving_->things) live.insert(i.mesh);
    std::lock_guard<std::mutex> lock(workMutex_);
    size_t kept=0;
    for(int32_t handle:retiredMeshes_) {
        if(handle<=0 || size_t(handle)>meshes_.size() || !meshes_[handle-1]) continue;
        Mesh* m=meshes_[handle-1].get();
        if(live.count(handle) || converting_==m) { retiredMeshes_[kept++]=handle; continue; }
        auto remove=[&](auto& values) { values.erase(std::remove(values.begin(), values.end(), m), values.end()); };
        remove(pending_); remove(work_); remove(done_); remove(ommQueue_);
        forget_omm_readback(m); // (PT-110: its bake's count, if it is still on its way back)
        for(auto va:m->geometryAllocations) geometryArena.retire(va, fence);
        for(auto va:m->accelerationAllocations) retire_structure(va, fence);
        // (Its tangents' and its decal colors' views too: until 2026-10-12 only clear gave those two back, so every mesh
        // let go on a walk kept one or two of the heap's slots for the rest of the session. docs/agents/stale.md F3.)
        for(uint32_t srv:{m->positionsSrv,m->uvsSrv,m->colorsSrv,m->indicesSrv,m->normalsSrv,m->uvs2Srv,m->tangentsSrv,m->decalColorsSrv,
                          m->skinUav,m->skinPreviousUav,m->skinPreviousSrv,m->skinNormalsUav,m->skinNormalsSrv}) heap.release(srv);
        release.add(fence,m->skinPositions.resource.Get()); release.add(fence,m->skinPrevious.resource.Get()); release.add(fence,m->skinNormals.resource.Get());
        release.add(fence,m->ommIndices.resource.Get()); // (PT-110's M1: its micromaps' kept index buffer, if it has one)
        if(auto r=waterReadbacks_.find(handle); r!=waterReadbacks_.end()) { release.add(fence, r->second.buffer.resource.Get()); waterReadbacks_.erase(r); }
#if TGPT_MEGA_GEOMETRY
        // (PT-109 on trial: its cut and templates, and its read-back for the cut if one is on its way.)
        if(auto c=megaMeshes_.find(handle); c!=megaMeshes_.end()) { mega_release(c->second, &heap, release, fence); megaMeshes_.erase(c); }
        if(auto r=megaReadbacks_.find(handle); r!=megaReadbacks_.end()) { release.add(fence, r->second.buffer.resource.Get()); megaReadbacks_.erase(r); }
        megaWanted_.erase(handle); megaBoxes_.erase(handle);
        release.add(fence, m->megaMaps.array.resource.Get()); // (megageo-5: its micromaps as the clusters' own micromap array, if it has one)
#endif
        if(size_t(handle)<meshRecords_.size()) meshRecords_[handle]=MeshRecordGpu{kInvalid,kInvalid,kInvalid,kInvalid,0,0,0,kInvalid,kInvalid};
        { std::lock_guard<std::mutex> stats(statsMutex_);
          if(m->state==Mesh::State::Ready) { --stats_.meshesReady; stats_.triangles-=m->triangles; stats_.geometryBytes-=m->bytesOnGpu; }
          stats_.accelerationBytes-=m->blasBytes+m->ommBytes;
          if(m->omm==Mesh::Omm::Linked) { --stats_.ommMeshes; stats_.ommTriangles-=m->alphaTriangles; stats_.ommBytes-=m->ommBytes; }
        }
        // Its micromaps' share of the scene's budget comes back with it (it only grew until 2026-10-08: a long session's
        // retired meshes would have used the budget up for the meshes after them).
        if(m->omm==Mesh::Omm::Linked) ommInputBytes_-=std::min(ommInputBytes_, uint64_t(m->alphaTriangles)*omm_bytes(m->ommLevel));
        meshes_[handle-1].reset(); meshRecordsDirty_=true; emittersDirty_=true;
        ++meshEpoch_; // (PT-99: the plants' kept list, plan_plants)
    }
    retiredMeshes_.resize(kept);
}

SceneStats GpuScene::stats() const { std::lock_guard<std::mutex> s(statsMutex_); return stats_; }

// What the reference radiance is made of, for the log (PT-136): the brightest lamp, the brightest glowing mesh as it
// counts, and as it counted before its own radiance bounded it (its clusters' intensity together).
void GpuScene::brightest_parts(float& lamp, float& emitter, float& emitterRaw) const {
    lamp=0.f;
    for(const auto& l:lights_) {
        const float lum=0.2126f*l.color[0]+0.7152f*l.color[1]+0.0722f*l.color[2];
        if(std::isfinite(lum)) lamp=std::max(lamp, lum);
    }
    emitter=std::isfinite(brightestEmitter_) ? brightestEmitter_ : 0.f; emitterRaw=std::isfinite(brightestEmitterRaw_) ? brightestEmitterRaw_ : 0.f;
}
// PT-136: the lamps' share of the reference radiance: the brightest lamp, but a lamp counts for no more than
// kLampOverMedian times the median lamp. The reference is where light is stored from (every 16-bit target holds light
// relative to it) and what the clamps measure against; ONE lamp far over the rest (a daylight vista's effect light of
// 1.8e9 cd among lamps of 6e4 at the median; the sun's white there is 2.7e4) put it 20,000 times over the sun's white,
// and every other light of the view was stored at the bottom of a 16-bit float: dim light lost or shown in steps. 64:
// in every scene looked at on 2026-10-12 without such a lamp the brightest is 16 to 52 times the median (the forest
// and the town 3.87e6 over 7.5e4, the tavern 1.24e7 over 1.5e6), so their reference is what it was to the last bit.
constexpr float kLampOverMedian=64.f;
float GpuScene::lamps_reference(float* brightest, float* median) const {
    std::vector<float> lums; lums.reserve(lights_.size());
    float most=0.f;
    for(const auto& l:lights_) {
        const float lum=0.2126f*l.color[0]+0.7152f*l.color[1]+0.0722f*l.color[2];
        if(std::isfinite(lum) && lum>0.f) { lums.push_back(lum); most=std::max(most, lum); }
    }
    float middle=most;
    if(!lums.empty()) { const auto mid=lums.begin()+lums.size()/2; std::nth_element(lums.begin(), mid, lums.end()); middle=*mid; }
    if(brightest) *brightest=most;
    if(median) *median=middle;
    return std::min(most, kLampOverMedian*middle);
}

// PT-136, for the log: the lamps by strength. The brightest three (kind, luminance in candela, range, radius, flags, where)
// and how the lamps' 1 m values spread: the reference radiance is the brightest one's, and one lamp far over the rest
// puts every other light at the bottom of a 16-bit float.
std::string GpuScene::brightest_lamps_text() const {
    struct Row { float lum; size_t at; };
    std::vector<Row> rows; rows.reserve(lights_.size());
    for(size_t i=0;i<lights_.size();++i) {
        const auto& l=lights_[i];
        const float lum=0.2126f*l.color[0]+0.7152f*l.color[1]+0.0722f*l.color[2];
        if(std::isfinite(lum) && lum>0.f) rows.push_back(Row{lum, i});
    }
    if(rows.empty()) return "no lamps";
    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.lum>b.lum; });
    auto at=[&](double share) { return rows[std::min(rows.size()-1, size_t(share*double(rows.size())))].lum; };
    char b[256];
    std::snprintf(b, sizeof b, "%zu lamps; their luminance, candela: the brightest %.4g, the 2nd %.4g, the 10th %.4g, the median %.4g, the dimmest tenth under %.4g; ",
                  rows.size(), rows[0].lum, rows[std::min<size_t>(1, rows.size()-1)].lum, rows[std::min<size_t>(9, rows.size()-1)].lum, at(0.5), at(0.9));
    std::string text=b;
    for(size_t k=0;k<std::min<size_t>(3, rows.size());++k) {
        const auto& l=lights_[rows[k].at];
        std::snprintf(b, sizeof b, "%s#%zu: %s, %.4g cd, range %.3g m%s, radius %.3g m, flags 0x%X, at %.1f %.1f %.1f",
                      k ? "; " : "the brightest: ", k+1, l.type==0u ? "a point light" : l.type==1u ? "a spot" : l.type==2u ? "a directional light" : "another kind",
                      rows[k].lum, l.range, (l.flags&TGPT_LIGHT_NO_RANGE) ? " (falls off with distance alone past it)" : " (ends there)", l.radius, l.flags,
                      l.position[0], l.position[1], l.position[2]);
        text+=b;
    }
    return text;
}

float GpuScene::brightest_light() const {
    float brightest=lamps_reference(nullptr, nullptr); // (PT-136: the brightest lamp, bounded by the median's)
    // An emissive surface sampled as a light is one too: in a room lit by a glowing crystal alone, the clamps measured
    // against the lamps left (none) cut its light down to the fallback reference's.
    if(std::isfinite(brightestEmitter_)) brightest=std::max(brightest, brightestEmitter_);
    return brightest;
}
} // namespace tgpt
