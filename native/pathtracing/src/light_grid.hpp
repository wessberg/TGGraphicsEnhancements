// SPDX-License-Identifier: MIT
#pragma once
// The light grid's cells and their lists (scene.cpp build_light_grid; pt_common.hlsli light_lists, cell_candidate), made
// on the CPU whenever a light moved or changed its range: with flames that flicker that is every frame (the plugin
// derives a light's range from its intensity, SceneLights.cs Reach), and while it is made the GPU waits (PT-99,
// docs/agents/reccpu.md).
//
// build_cells makes the very bytes build_cells_reference makes (the loop that was in scene.cpp until 2026-10-12, kept
// here as what the other is held against; tests/light_grid_test.cpp compares them byte by byte over made-up lights, no
// GPU), with less work:
//   - one walk over each light's cells in place of two (the first only counted, the second measured every cell's
//     distance again): each entry's cell and weight are kept from the one walk and dealt out to the lists afterwards, in
//     the order of the lights, so every list holds its lights in the order it did;
//   - a cell's squared distance from a light is the sum of three numbers that depend on one axis each: those are worked
//     out once a light and axis (at most 48 + 20 + 48 of them), not once a cell. The sum is taken in the same order
//     (x, then y, then z), so it has the same bits.
// Nothing here knows the scene: lights in, lists out.
#include "tgpt_api.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace light_grid {
// scene.hpp's kGridDims, kGridCell, kGridPad and kEmitterBase (a cluster's record: kEmitterBase + clusterBase + its number).
struct Shape { uint32_t dims[3]; double cell, pad; uint32_t emitterBase; };
struct Counts { uint32_t entries=0, most=0, lit=0; };

// A list's weights into cumulative ones, normalized; a list whose lights all weigh nothing counts them evenly, and
// every light keeps a thousandth of the mean weight, so none whose light was 0 at the build (one fading in) is never drawn.
inline void cumulate(const float* weights, uint32_t count, uint32_t* entries) {
    double total=0; for(uint32_t k=0;k<count;++k) total+=weights[k];
    const double floor=total>0 ? 1e-3*total/double(count) : 1.0, sum=total+floor*double(count);
    double running=0;
    for(uint32_t k=0;k<count;++k) {
        running+=double(weights[k])+floor;
        float cumulative=float(running/sum);
        if(k+1==count) cumulative=1.f;
        std::memcpy(&entries[size_t(k)*2+1], &cumulative, sizeof(cumulative));
    }
}
// Whether the grid lists this light at all (a cluster: an emissive cluster sampled as a light, type 3).
inline bool listed(const TGPT_Light& l, bool cluster) {
    return !((cluster ? l.type!=3u : l.type>1) || !(l.range>0.f) || !std::isfinite(l.range) ||
             !std::isfinite(l.position[0]) || !std::isfinite(l.position[1]) || !std::isfinite(l.position[2]));
}
inline double power(const TGPT_Light& l) { return std::max(0.0, 0.2126*double(l.color[0])+0.7152*double(l.color[1])+0.0722*double(l.color[2])); }

// What the builder keeps between builds: room only (no result of one build is read by the next).
struct Scratch {
    std::vector<uint32_t> cell, first, cursor; // every entry's cell, light by light; each light's first entry; a cell's next free entry
    std::vector<float> weight;                 // every entry's weight, beside `cell`
    std::vector<double> axis[3];               // the light's squared distance from each cell along one axis
};

// The cells' lists: cells (first entry, count; two words a cell), indices (light, cumulative weight; two words an entry)
// and weights (an entry's weight before the lists were made cumulative). Counted, then filled: every cell's lights are
// contiguous, and no cell has a cap that would drop one. An entry's weight is the light's luminance over its squared
// distance from the cell's box, at least 1 m.
inline Counts build_cells(const Shape& shape, const double corner[3], const TGPT_Light* lights, size_t lightCount, const TGPT_Light* clusters, size_t clusterCount,
                          uint32_t clusterBase, std::vector<uint32_t>& cells, std::vector<uint32_t>& indices, std::vector<float>& weights, Scratch& w) {
    const uint32_t dx=shape.dims[0], dy=shape.dims[1], dz=shape.dims[2];
    const size_t cellCount=size_t(dx)*dy*dz, sources=lightCount+clusterCount;
    cells.assign(cellCount*2, 0);
    w.cell.clear(); w.weight.clear(); w.first.assign(sources+1, 0);
    for(int a=0;a<3;++a) w.axis[a].resize(shape.dims[a]);
    for(size_t i=0;i<sources;++i) {
        w.first[i]=uint32_t(w.cell.size());
        const bool cluster=i>=lightCount;
        const TGPT_Light& l=cluster ? clusters[i-lightCount] : lights[i];
        if(!listed(l, cluster)) continue;
        const double r=double(l.range)+shape.pad;
        const double lightPower=power(l);
        int lo[3], hi[3]; bool inside=true;
        for(int a=0;a<3;++a) {
            const double p=double(l.position[a])-corner[a];
            lo[a]=std::max(0, int(std::floor((p-r)/shape.cell)));
            hi[a]=std::min(int(shape.dims[a])-1, int(std::floor((p+r)/shape.cell)));
            if(lo[a]>hi[a]) inside=false;
        }
        if(!inside) continue;
        // The light's sphere of influence against a cell's box: along each axis, how far the light is outside the cell's
        // stretch, squared.
        for(int a=0;a<3;++a) {
            const double p=double(l.position[a])-corner[a];
            double* out=w.axis[a].data();
            for(int c=lo[a];c<=hi[a];++c) {
                const double min=c*shape.cell, max=min+shape.cell;
                const double d=p<min ? min-p : (p>max ? p-max : 0.0);
                out[c-lo[a]]=d*d;
            }
        }
        const double r2=r*r;
        const double* ax=w.axis[0].data();
        for(int z=lo[2];z<=hi[2];++z) {
            const double tz=w.axis[2][size_t(z-lo[2])];
            for(int y=lo[1];y<=hi[1];++y) {
                const double ty=w.axis[1][size_t(y-lo[1])];
                const size_t row=(size_t(z)*dy+size_t(y))*dx;
                for(int x=lo[0];x<=hi[0];++x) {
                    double d2=ax[x-lo[0]]; // (0 + it, as the loop it stands for began)
                    d2+=ty; d2+=tz;
                    if(d2>r2) continue;
                    const size_t at=row+size_t(x);
                    ++cells[at*2+1];
                    w.cell.push_back(uint32_t(at));
                    w.weight.push_back(float(lightPower/std::max(d2, 1.0)));
                }
            }
        }
    }
    w.first[sources]=uint32_t(w.cell.size());
    Counts counts;
    for(size_t c=0;c<cellCount;++c) {
        cells[c*2]=counts.entries; counts.entries+=cells[c*2+1];
        counts.most=std::max(counts.most, cells[c*2+1]); counts.lit+=cells[c*2+1] ? 1u : 0u;
    }
    indices.assign(size_t(counts.entries)*2, 0);
    weights.assign(counts.entries, 0.f);
    w.cursor.resize(cellCount);
    for(size_t c=0;c<cellCount;++c) w.cursor[c]=cells[c*2];
    for(size_t i=0;i<sources;++i) {
        const uint32_t index=i>=lightCount ? shape.emitterBase+clusterBase+uint32_t(i-lightCount) : uint32_t(i);
        for(uint32_t e=w.first[i];e<w.first[i+1];++e) {
            const uint32_t entry=w.cursor[w.cell[e]]++;
            indices[size_t(entry)*2]=index;
            weights[entry]=w.weight[e];
        }
    }
    for(size_t c=0;c<cellCount;++c)
        if(const uint32_t first=cells[c*2], count=cells[c*2+1]; count) cumulate(&weights[first], count, &indices[size_t(first)*2]);
    return counts;
}

// The loop build_cells took the place of, as it stood in scene.cpp (GpuScene::build_light_grid) until 2026-10-12: what
// build_cells is held against (tests/light_grid_test.cpp, and in the game or the GPU test with the environment's
// TGPT_TEST_RECORD_CHECK set: scene.cpp). Not run otherwise.
inline Counts build_cells_reference(const Shape& shape, const double corner[3], const TGPT_Light* lights, size_t lightCount, const TGPT_Light* clusters, size_t clusterCount,
                                    uint32_t clusterBase, std::vector<uint32_t>& cells, std::vector<uint32_t>& indices, std::vector<float>& weights) {
    const uint32_t dx=shape.dims[0], dy=shape.dims[1], dz=shape.dims[2];
    const size_t cellCount=size_t(dx)*dy*dz;
    std::vector<uint32_t> fill;
    cells.assign(cellCount*2, 0);
    fill.assign(cellCount, 0);
    Counts counts;
    const size_t sources=lightCount+clusterCount;
    for(int pass=0;pass<2;++pass) {
        for(size_t i=0;i<sources;++i) {
            const bool cluster=i>=lightCount;
            const TGPT_Light& l=cluster ? clusters[i-lightCount] : lights[i];
            const uint32_t index=cluster ? shape.emitterBase+clusterBase+uint32_t(i-lightCount) : uint32_t(i);
            if((cluster ? l.type!=3u : l.type>1) || !(l.range>0.f) || !std::isfinite(l.range) ||
               !std::isfinite(l.position[0]) || !std::isfinite(l.position[1]) || !std::isfinite(l.position[2])) continue;
            const double r=double(l.range)+shape.pad;
            const double power=std::max(0.0, 0.2126*double(l.color[0])+0.7152*double(l.color[1])+0.0722*double(l.color[2]));
            int lo[3], hi[3]; bool inside=true;
            for(int a=0;a<3;++a) {
                const double p=double(l.position[a])-corner[a];
                lo[a]=std::max(0, int(std::floor((p-r)/shape.cell)));
                hi[a]=std::min(int(shape.dims[a])-1, int(std::floor((p+r)/shape.cell)));
                if(lo[a]>hi[a]) inside=false;
            }
            if(!inside) continue;
            for(int z=lo[2];z<=hi[2];++z) for(int y=lo[1];y<=hi[1];++y) for(int x=lo[0];x<=hi[0];++x) {
                // The light's sphere of influence against the cell's box.
                const int c[3]={x, y, z};
                double d2=0;
                for(int a=0;a<3;++a) {
                    const double p=double(l.position[a])-corner[a], min=c[a]*shape.cell, max=min+shape.cell;
                    const double d=p<min ? min-p : (p>max ? p-max : 0.0);
                    d2+=d*d;
                }
                if(d2>r*r) continue;
                const size_t cell=(size_t(z)*dy+size_t(y))*dx+size_t(x);
                if(pass==0) ++cells[cell*2+1];
                else {
                    const uint32_t entry=cells[cell*2]+fill[cell]++;
                    indices[size_t(entry)*2]=index;
                    weights[entry]=float(power/std::max(d2, 1.0));
                }
            }
        }
        if(pass==0) {
            uint32_t total=0, most=0, lit=0;
            for(size_t c=0;c<cellCount;++c) {
                cells[c*2]=total; total+=cells[c*2+1];
                most=std::max(most, cells[c*2+1]); lit+=cells[c*2+1] ? 1u : 0u;
            }
            indices.assign(size_t(total)*2, 0);
            weights.assign(total, 0.f);
            counts.entries=total; counts.most=most; counts.lit=lit;
        }
    }
    for(size_t c=0;c<cellCount;++c)
        if(const uint32_t first=cells[c*2], count=cells[c*2+1]; count) cumulate(&weights[first], count, &indices[size_t(first)*2]);
    return counts;
}
}
