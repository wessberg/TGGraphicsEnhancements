// SPDX-License-Identifier: MIT
// The light grid's cells as scene.cpp makes them since 2026-10-12 (native/pathtracing/src/light_grid.hpp build_cells:
// one walk a light, each axis's share of the distance worked out once; PT-99, docs/agents/reccpu.md) against the loop
// that made them until then (build_cells_reference, kept there as what the other is held against). No GPU.
//
// What it proves: for the same lights, clusters and corner the two make the same bytes: every cell's first entry and
// count, every entry's light and cumulative weight, every entry's weight before the lists were made cumulative, and the
// three counts the log tells. The lights are the hard ones: on a cell's border to the last bit, a range that ends on a
// border, outside the grid, covering all of it, with no range, with a range or a place that is no number, of every
// kind; and a town's worth of flames with the ranges the plugin gives them, for the time each way takes.
#include "../native/pathtracing/src/light_grid.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace {
int failures=0;
void expect(bool ok, const std::string& what) {
    if(ok) return;
    ++failures;
    if(failures<=20) std::printf("FAIL: %s\n", what.c_str());
}
struct Random {
    uint64_t state;
    uint32_t next() { state=state*6364136223846793005ull+1442695040888963407ull; return uint32_t(state>>33); }
    double unit() { return double(next())/2147483648.0; }
    double range(double a, double b) { return a+(b-a)*unit(); }
    uint32_t below(uint32_t n) { return next()%n; }
};
// scene.hpp's grid: 48 x 20 x 48 cells of 8 m, a quarter metre of padding; a cluster's record from kEmitterBase on.
const light_grid::Shape kShape{{48, 20, 48}, 8.0, 0.25, 0x80000000u};
const light_grid::Shape kSmall{{5, 3, 4}, 8.0, 0.25, 0x80000000u};

struct Result { std::vector<uint32_t> cells, indices; std::vector<float> weights; light_grid::Counts counts; };
template<typename T> bool same(const std::vector<T>& a, const std::vector<T>& b) {
    return a.size()==b.size() && (a.empty() || std::memcmp(a.data(), b.data(), a.size()*sizeof(T))==0);
}
bool compare(const light_grid::Shape& shape, const double corner[3], const std::vector<TGPT_Light>& lights, const std::vector<TGPT_Light>& clusters, uint32_t clusterBase,
             light_grid::Scratch& scratch, const std::string& what, Result* keep=nullptr) {
    Result a, b;
    a.counts=light_grid::build_cells_reference(shape, corner, lights.data(), lights.size(), clusters.data(), clusters.size(), clusterBase, a.cells, a.indices, a.weights);
    b.counts=light_grid::build_cells(shape, corner, lights.data(), lights.size(), clusters.data(), clusters.size(), clusterBase, b.cells, b.indices, b.weights, scratch);
    const bool ok=same(a.cells, b.cells) && same(a.indices, b.indices) && same(a.weights, b.weights) &&
        a.counts.entries==b.counts.entries && a.counts.most==b.counts.most && a.counts.lit==b.counts.lit;
    expect(ok, what+": the two builds differ ("+std::to_string(a.counts.entries)+" entries against "+std::to_string(b.counts.entries)+")");
    if(keep) *keep=std::move(b);
    return ok;
}
TGPT_Light light(double x, double y, double z, float range, uint32_t type, Random& r) {
    TGPT_Light l{};
    l.position[0]=float(x); l.position[1]=float(y); l.position[2]=float(z);
    l.range=range; l.type=type;
    for(float& c:l.color) c=float(r.range(0.0, 4000.0));
    if(r.below(16)==0) l.color[0]=l.color[1]=l.color[2]=0.f; // a light that is off: weight 0, kept by the floor
    return l;
}
}

int main() {
    Random r{0x5EEDull};
    light_grid::Scratch scratch;
    uint64_t compared=0, entries=0;
    const float nan=std::numeric_limits<float>::quiet_NaN(), inf=std::numeric_limits<float>::infinity();

    // 1. Made-up scenes on the small grid: every kind of light, every kind of trouble, corners far from 0.
    for(int scene=0;scene<4000;++scene) {
        const light_grid::Shape& shape=scene%4==3 ? kShape : kSmall;
        double corner[3];
        for(int a=0;a<3;++a) corner[a]=std::floor(r.range(-4000.0, 4000.0)/32.0)*32.0-double(shape.dims[a])*shape.cell*0.5;
        const double extent[3]={shape.dims[0]*shape.cell, shape.dims[1]*shape.cell, shape.dims[2]*shape.cell};
        std::vector<TGPT_Light> lights, clusters;
        const uint32_t lightCount=r.below(scene%4==3 ? 60 : 24), clusterCount=r.below(12);
        for(uint32_t i=0;i<lightCount+clusterCount;++i) {
            const bool cluster=i>=lightCount;
            double p[3];
            for(int a=0;a<3;++a) {
                switch(r.below(8)) {
                case 0: p[a]=corner[a]+shape.cell*double(r.below(shape.dims[a]+1)); break;           // on a cell's border
                case 1: p[a]=corner[a]+r.range(-60.0, extent[a]+60.0); break;                         // maybe outside
                case 2: p[a]=corner[a]+shape.cell*(double(r.below(shape.dims[a]))+0.5); break;        // a cell's middle
                default: p[a]=corner[a]+r.range(0.0, extent[a]); break;
                }
            }
            float range;
            switch(r.below(12)) {
            case 0: range=0.f; break;
            case 1: range=-1.f; break;
            case 2: range=nan; break;
            case 3: range=inf; break;
            case 4: range=float(shape.cell*double(1+r.below(4))-shape.pad); break; // its reach ends on a border, if it stands on one
            case 5: range=1000.f; break;                                           // all of the grid
            case 6: range=0.01f; break;
            default: range=float(r.range(0.2, 40.0)); break;
            }
            uint32_t type=cluster ? 3u : r.below(2);
            if(r.below(10)==0) type=r.below(5); // a directional light among the lights, a light among the clusters: left out
            TGPT_Light l=light(p[0], p[1], p[2], range, type, r);
            if(r.below(40)==0) l.position[r.below(3)]=r.below(2) ? nan : inf;
            (cluster ? clusters : lights).push_back(l);
        }
        Result made;
        compare(shape, corner, lights, clusters, r.below(100000), scratch, "scene "+std::to_string(scene), &made);
        ++compared; entries+=made.counts.entries;
        // And what any build must hold: a list's last cumulative weight is 1, the weights never fall, the lists follow one
        // another without a gap.
        uint32_t next=0;
        const size_t cellCount=size_t(shape.dims[0])*shape.dims[1]*shape.dims[2];
        for(size_t c=0;c<cellCount && failures<20;++c) {
            const uint32_t first=made.cells[c*2], count=made.cells[c*2+1];
            expect(first==next, "scene "+std::to_string(scene)+": a cell's list does not follow the one before");
            next+=count;
            float before=0.f;
            for(uint32_t k=0;k<count;++k) {
                float cumulative; std::memcpy(&cumulative, &made.indices[size_t(first+k)*2+1], 4);
                expect(cumulative>=before && cumulative<=1.f, "scene "+std::to_string(scene)+": a list's cumulative weights fall or pass 1");
                before=cumulative;
            }
            if(count) expect(before==1.f, "scene "+std::to_string(scene)+": a list does not end at 1");
        }
        expect(next==made.counts.entries, "scene "+std::to_string(scene)+": the lists' counts do not add up");
    }

    // 2. A town: 985 lights, most of them flames whose range the plugin derives from their intensity (5 to 40 m), a few
    // hundred emissive clusters, the grid's own size. Twice, the second with every flame's range a little off (a flicker),
    // through the same scratch: nothing of the first build may show in the second.
    {
        double corner[3]={-1536.0-192.0, 0.0-80.0, 2048.0-192.0};
        std::vector<TGPT_Light> lights, clusters;
        for(int i=0;i<985;++i) lights.push_back(light(corner[0]+r.range(0.0, 384.0), corner[1]+r.range(60.0, 100.0), corner[2]+r.range(0.0, 384.0), float(r.range(5.0, 40.0)), r.below(2), r));
        for(int i=0;i<300;++i) clusters.push_back(light(corner[0]+r.range(0.0, 384.0), corner[1]+r.range(60.0, 100.0), corner[2]+r.range(0.0, 384.0), float(r.range(1.0, 12.0)), 3u, r));
        Result made;
        compare(kShape, corner, lights, clusters, 4096, scratch, "the town", &made);
        std::printf("the town: %u entries in %u lit cells, %u lights in the fullest\n", made.counts.entries, made.counts.lit, made.counts.most);
        for(auto& l:lights) l.range*=float(r.range(0.9, 1.1));
        compare(kShape, corner, lights, clusters, 4096, scratch, "the town, flickering");
        compared+=2;
        // The time each way takes (a figure for the report; nothing is judged by it).
        Result a, b;
        const int rounds=20;
        auto began=std::chrono::steady_clock::now();
        for(int k=0;k<rounds;++k) light_grid::build_cells_reference(kShape, corner, lights.data(), lights.size(), clusters.data(), clusters.size(), 4096, a.cells, a.indices, a.weights);
        const double before=std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now()-began).count()/rounds;
        began=std::chrono::steady_clock::now();
        for(int k=0;k<rounds;++k) light_grid::build_cells(kShape, corner, lights.data(), lights.size(), clusters.data(), clusters.size(), 4096, b.cells, b.indices, b.weights, scratch);
        const double after=std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now()-began).count()/rounds;
        std::printf("the town's cells: %.3f ms a build as it was, %.3f ms now\n", before, after);
    }
    std::printf("%llu scenes compared, %llu entries; %d failures\n", (unsigned long long)compared, (unsigned long long)entries, failures);
    if(failures) { std::printf("LIGHT GRID TEST FAILED\n"); return 1; }
    std::printf("light grid test passed\n");
    return 0;
}
