// SPDX-License-Identifier: MIT
// The water's surface: HDRP's waves as triangles in the acceleration structure (scene.hpp "The water's surface";
// docs/agents/water-4.md). The CPU's part: which pieces of the water meshes a frame wants and how finely cut (plan_water),
// the groups they are traced in (scene.hpp WaterGroup: their buffers and acceleration structures), the dispatches that pose
// them (pt_water_mesh.hlsl) and the groups' instances (pose_water). The surface itself is the shader's: a function of the
// game's wave data and the camera's position.
#include "scene.hpp"
#include <chrono>
#include <cmath>
#include <cstring>

namespace tgpt {
namespace {

constexpr uint64_t kVertexPageBytes=16ull<<20, kBlasPageBytes=32ull<<20;
constexpr uint32_t kSmallestBlock=12; // 4 KB: a block's size class c holds 2^(12 + c) bytes

// The lattice's points in the order the units number their vertices: level by level, the three corners first (level 0),
// then each level's new points, row by row. So a triangle cut 4^m times uses the first kWaterLatticeVertices[m] of them.
// After them, for every point of the finest lattice (33 rows of 33), where it is in that order (0xFFFF outside the triangle).
std::vector<uint32_t> water_lattice() {
    std::vector<uint32_t> table;
    table.reserve(kWaterLatticeVertices[kWaterLevels]+33*33);
    std::vector<uint32_t> where(33*33, 0xFFFFu);
    for(uint32_t r=0;r<=kWaterLevels;++r) {
        const uint32_t s=kWaterLattice>>r;
        for(uint32_t j=0;j<=kWaterLattice;j+=s) for(uint32_t i=0;i+j<=kWaterLattice;i+=s) {
            if(r>0 && !((i/s)&1u) && !((j/s)&1u)) continue; // a point of the level before
            where[j*33+i]=uint32_t(table.size());
            table.push_back(i|(j<<8));
        }
    }
    table.insert(table.end(), where.begin(), where.end());
    return table;
}

// The shared pattern of indices: for each level m, kWaterGroupTriangles >> 2m base triangles one after another, each cut in
// 4^m triangles wound as the base triangle is (so a unit's front is its base triangles' front), over that triangle's own
// vertices (kWaterLatticeVertices[m] of them, numbered as the table has them). Base triangle t's are numbered from
// t x kWaterLatticeVertices[m]: a unit's structure reads the pattern from its start over the unit's own vertices, and a
// hit reads it from the unit's first base triangle in its group over the group's (scene.hpp WaterGroup).
std::vector<uint32_t> water_pattern(const std::vector<uint32_t>& table) {
    const uint32_t* where=table.data()+kWaterLatticeVertices[kWaterLevels];
    std::vector<uint32_t> indices(size_t(kWaterLevels+1)*kWaterLevelIndices, 0u);
    for(uint32_t m=0;m<=kWaterLevels;++m) {
        const uint32_t n=1u<<m, s=kWaterLattice>>m, count=kWaterGroupTriangles>>(2*m), vertices=kWaterLatticeVertices[m];
        uint32_t* out=indices.data()+size_t(m)*kWaterLevelIndices;
        auto at=[&](uint32_t a, uint32_t b) { return where[(b*s)*33+a*s]; };
        for(uint32_t t=0;t<count;++t) {
            const uint32_t base=t*vertices;
            for(uint32_t b=0;b<n;++b) for(uint32_t a=0;a+b<n;++a) {
                *out++=base+at(a, b); *out++=base+at(a+1, b); *out++=base+at(a, b+1);
                if(a+b+1<n) { *out++=base+at(a+1, b); *out++=base+at(a+1, b+1); *out++=base+at(a, b+1); }
            }
        }
    }
    return indices;
}

double box_distance(const double lo[3], const double hi[3], const double p[3]) {
    double d2=0;
    for(int a=0;a<3;++a) { const double d=std::max({lo[a]-p[a], 0.0, p[a]-hi[a]}); d2+=d*d; }
    return std::sqrt(d2);
}
// How often a piece whose longest lattice edge is `longest` must be cut so that no edge of it is wanted any finer by a
// camera `distance` away from its nearest point (pt_water_mesh.hlsl: an edge longer than h is wanted). One per cent on the
// safe side of the shader's own arithmetic: cutting finer than needed changes nothing, cutting coarser would open a crack.
uint32_t water_cuts(double longest, double distance, double cell) {
    if(!(cell>0.0)) return 0; // still water: nothing to resolve
    const double h=0.99*std::max(cell, distance*kWaterCellPerMetre);
    const double need=longest/h;
    if(!(need>1.0)) return 0;
    return uint32_t(std::min(64.0, std::ceil(std::log2(need))));
}
uint64_t spread3(uint64_t v) { // 21 bits, two zero bits between each
    v&=0x1FFFFFull; v=(v|(v<<32))&0x1F00000000FFFFull; v=(v|(v<<16))&0x1F0000FF0000FFull; v=(v|(v<<8))&0x100F00F00F00F00Full;
    v=(v|(v<<4))&0x10C30C30C30C30C3ull; v=(v|(v<<2))&0x1249249249249249ull; return v;
}

} // namespace

bool GpuScene::water_block(ID3D12Device5* device, DescriptorHeap& heap, WaterPool& pool, uint64_t bytes, bool blas, uint32_t& page, uint32_t& at, uint32_t& block) {
    const uint64_t pageBytes=blas ? kBlasPageBytes : kVertexPageBytes;
    // The block's size (scene.hpp WaterPool): the power of two at or above the bytes, 4 KB at least; with PT-87's cut, in
    // steps of an eighth of the power of two below them (512 bytes at least: a structure starts on 256, a raw view on 16).
    uint32_t power=0;
    while((1ull<<(kSmallestBlock+power))<bytes) ++power;
    uint64_t size=1ull<<(kSmallestBlock+power);
    const bool fitted=!(waterCutsOff_&kMemory2Water);
    if(fitted && power>0) { const uint64_t step=size>>4; size=(bytes+step-1)/step*step; }
    if(size>pageBytes) return false;
    block=uint32_t(size);
    auto took=[&](uint32_t p, uint64_t start) { page=p; at=uint32_t(start); ++pool.pages[p].live; pool.liveBytes+=size; return true; };
    // The first page with a free stretch that holds it, and that page's first such stretch (PT-99, scene.hpp WaterPool:
    // the groups' blocks are of every size, so a block is cut from whatever stretch is free, not waited for by its
    // size; and the first that fits, so that the later pages empty and go back). Every size is a multiple of 512, so
    // every block starts on one: a structure wants 256, a raw view 16.
    for(uint32_t p=0;p<pool.pages.size();++p) {
        auto& candidate=pool.pages[p];
        if(!candidate.buffer) continue; // a page given back: its place waits for the next one made
        for(auto stretch=candidate.free.begin();stretch!=candidate.free.end();++stretch) {
            if(stretch->second<size) continue;
            const uint32_t start=stretch->first, rest=stretch->second-uint32_t(size);
            candidate.free.erase(stretch);
            if(rest) candidate.free.emplace(start+uint32_t(size), rest);
            return took(p, start);
        }
    }
    WaterPool::Page fresh;
    fresh.buffer=make_buffer(device, pageBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                             blas ? D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE : D3D12_RESOURCE_STATE_COMMON,
                             blas ? "Create a page of the water's acceleration structures" : "Create a page of the water's vertices");
    if(!blas) fresh.uav=raw_uav(device, heap, fresh.buffer.resource.Get(), pageBytes);
    if(size<pageBytes) fresh.free.emplace(uint32_t(size), uint32_t(pageBytes-size));
    uint32_t place=uint32_t(pool.pages.size());
    for(uint32_t p=0;p<pool.pages.size();++p) if(!pool.pages[p].buffer) { place=p; break; }
    if(place==pool.pages.size()) pool.pages.push_back(std::move(fresh)); else pool.pages[place]=std::move(fresh);
    return took(place, 0);
}
// A group's block back among its page's free stretches, one with those it touches, for whatever is asked for next. (At
// once: a frame the GPU has not finished reads it before anything recorded later writes it: one queue.)
void GpuScene::water_block_free(WaterPool& pool, uint32_t page, uint32_t at, uint32_t block) {
    if(page>=pool.pages.size()) return;
    auto& p=pool.pages[page];
    uint32_t start=at, bytes=block;
    auto after=p.free.lower_bound(at);
    if(after!=p.free.begin()) {
        const auto before=std::prev(after);
        if(before->first+before->second==at) { start=before->first; bytes+=before->second; p.free.erase(before); }
    }
    if(after!=p.free.end() && at+block==after->first) { bytes+=after->second; p.free.erase(after); }
    p.free.emplace(start, bytes);
    if(p.live) --p.live;
    if(!p.live) p.idleSince=waterFrame_;
    pool.liveBytes-=std::min<uint64_t>(pool.liveBytes, block);
}

// PT-87 (scene.hpp DistanceBands): the water's pieces by how near the camera was to each when it was last wanted, with
// what each holds of the two pools: its share, by its base triangles, of its group's two blocks (whole blocks, as they
// are taken; a group's units share one cut level, so one's share of the vertices and of the triangles is the same).
void GpuScene::water_bands(DistanceBands& out) const {
    for(const auto& [key, u]:waterUnits_) {
        if(!u.group) continue;
        const WaterGroup& g=*u.group;
        const int b=DistanceBands::band(double(u.nearest));
        ++out.waterUnits[b]; out.waterTriangles[b]+=uint64_t(u.triangles)<<(2*u.level);
        out.water[b]+=(uint64_t(g.vertexBlock)+uint64_t(g.blasBlock))*u.triangles/std::max(g.baseTriangles, 1u);
    }
}

// A unit about to leave the map: its group's entry no longer names it (the geometry stays in the group's structure,
// collapsed, until the group is made anew: scene.hpp WaterGroup).
void GpuScene::water_unit_gone(WaterUnit& u) {
    if(u.group && u.member<u.group->members.size() && u.group->members[u.member].unit==&u) u.group->members[u.member].unit=nullptr;
    u.group=nullptr;
}

// A group of these units, in this order: all of one slot and one cut level, all wanted this frame (so every one of them
// is posed before the group's structure is built). Everything a later frame reads of it is set here and never written
// again: each unit's place (its first base triangle among the group's), where its vertices go (the pose records' at,
// span and target: places in the group's block, which does not move), the build's description.
WaterGroup* GpuScene::water_group_make(ID3D12Device5* device, DescriptorHeap& heap, const std::vector<WaterUnit*>& units) {
    if(units.empty()) return nullptr;
    auto made=std::make_unique<WaterGroup>();
    WaterGroup& g=*made;
    g.slot=units[0]->slot; g.level=units[0]->level;
    const uint32_t perTriangle=kWaterLatticeVertices[g.level];
    g.members.reserve(units.size()); g.geometry.resize(units.size());
    for(size_t k=0;k<units.size();++k) {
        const WaterUnit& u=*units[k];
        WaterGroup::Member m; m.unit=units[k]; m.first=g.baseTriangles; m.count=u.triangles;
        for(int a=0;a<3;++a) m.corner[a]=u.corner[a];
        g.members.push_back(m);
        g.baseTriangles+=u.triangles;
        // Its geometry: the unit's own vertices and the level's pattern from its start, the triangles a unit's own
        // structure had (the addresses below, once the block is had).
        D3D12_RAYTRACING_GEOMETRY_DESC& geometry=g.geometry[k]; geometry={};
        geometry.Type=D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES; geometry.Flags=D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
        geometry.Triangles.VertexFormat=DXGI_FORMAT_R32G32B32_FLOAT; geometry.Triangles.VertexCount=u.triangles*perTriangle; geometry.Triangles.VertexBuffer.StrideInBytes=12;
        geometry.Triangles.IndexFormat=DXGI_FORMAT_R32_UINT; geometry.Triangles.IndexCount=(u.triangles<<(2*g.level))*3;
    }
    g.vertexCount=g.baseTriangles*perTriangle;
    const uint64_t bytes=uint64_t(g.vertexCount)*36;
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS in{};
    in.Type=D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    in.Flags=D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE|D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;
    in.NumDescs=UINT(g.geometry.size()); in.DescsLayout=D3D12_ELEMENTS_LAYOUT_ARRAY; in.pGeometryDescs=g.geometry.data();
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info{}; device->GetRaytracingAccelerationStructurePrebuildInfo(&in, &info);
    try {
        if(!info.ResultDataMaxSizeInBytes || !water_block(device, heap, waterVertexPool_, bytes, false, g.vertexPage, g.vertexAt, g.vertexBlock)) return nullptr;
        if(!water_block(device, heap, waterBlasPool_, info.ResultDataMaxSizeInBytes, true, g.blasPage, g.blasAt, g.blasBlock)) {
            water_block_free(waterVertexPool_, g.vertexPage, g.vertexAt, g.vertexBlock);
            return nullptr;
        }
        g.srv=heap.allocate();
    } catch(const std::exception& e) {
        if(g.blasPage!=kInvalid) water_block_free(waterBlasPool_, g.blasPage, g.blasAt, g.blasBlock);
        if(g.vertexPage!=kInvalid) water_block_free(waterVertexPool_, g.vertexPage, g.vertexAt, g.vertexBlock);
        log_throttled(std::string("A group of the water's pieces could not be made: ")+e.what());
        return nullptr;
    }
    g.scratchBytes=info.ScratchDataSizeInBytes; g.updateBytes=info.UpdateScratchDataSizeInBytes;
    const WaterPool::Page& page=waterVertexPool_.pages[g.vertexPage];
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC d{}; d.Format=DXGI_FORMAT_R32_TYPELESS; d.ViewDimension=D3D12_SRV_DIMENSION_BUFFER;
        d.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; d.Buffer.FirstElement=g.vertexAt/4; d.Buffer.NumElements=UINT(bytes/4);
        d.Buffer.Flags=D3D12_BUFFER_SRV_FLAG_RAW;
        device->CreateShaderResourceView(page.buffer.resource.Get(), &d, heap.cpu(g.srv));
    }
    const D3D12_GPU_VIRTUAL_ADDRESS vertices=page.buffer.va()+g.vertexAt, pattern=waterIndices_.va()+uint64_t(g.level)*kWaterLevelIndices*4;
    const uint32_t indicesEach=3u<<(2*g.level);
    g.records.reserve(units.size());
    for(size_t k=0;k<units.size();++k) {
        const WaterGroup::Member& m=g.members[k];
        g.geometry[k].Triangles.VertexBuffer.StartAddress=vertices+uint64_t(m.first)*perTriangle*12;
        g.geometry[k].Triangles.IndexBuffer=pattern;
        // Its geometry record, as a hit reads it: where in its level's pattern the indices of its base triangles start,
        // which are its vertices' numbers in the group's block (scene.hpp WaterGroup).
        g.records.push_back(GeometryRecordGpu{g.level*kWaterLevelIndices+m.first*indicesEach, m.count*indicesEach, 0, 0});
        WaterUnit& u=*units[k];
        water_unit_gone(u); // (of the group it was in, if any: that one is let go by whoever asked for this one)
        u.group=&g; u.member=uint32_t(k);
        for(uint32_t t=0;t<u.tris.size();++t) {
            WaterTriGpu& tri=u.tris[t];
            tri.at=g.vertexAt+(m.first+t)*perTriangle*12;
            tri.span=g.vertexCount*12;
            tri.target=page.uav;
        }
    }
    g.serial=++waterGroupSerial_; ++waterGroupsMadeEver_;
    g.used=uint32_t(units.size()); g.usedBase=g.baseTriangles; // (this frame's count: every one of them is wanted)
    waterGroups_.push_back(std::move(made));
    return waterGroups_.back().get();
}

// A group let go. Its blocks go back to their pools at once: a frame the GPU has not finished reads them before anything
// recorded later writes them (one queue). Its view waits for those frames (DescriptorHeap::release). Its units that
// still name it are without a group: they hold nothing on the GPU until a frame wants them again.
void GpuScene::water_group_free(WaterGroup& g, DescriptorHeap* heap) {
    for(auto& m:g.members) if(m.unit && m.unit->group==&g) m.unit->group=nullptr;
    if(g.vertexPage!=kInvalid) water_block_free(waterVertexPool_, g.vertexPage, g.vertexAt, g.vertexBlock);
    if(g.blasPage!=kInvalid) water_block_free(waterBlasPool_, g.blasPage, g.blasAt, g.blasBlock);
    if(heap) heap->release(g.srv);
    g.vertexPage=g.blasPage=kInvalid; g.srv=kInvalid; g.members.clear(); g.geometry.clear(); g.records.clear();
}

// PT-114 (scene.hpp): the tiles a slot's level water can lie under. A triangle's footprint (its three corners' x and z) is
// held against each tile's square grown by the margin: by their boxes, then by the triangle's three edges (a square wholly
// outside one edge's line is clear of it). A triangle with no footprint to speak of (it stands upright) counts by its box.
bool GpuScene::water_tiles(uint32_t slot, double x0, double z0, double tile, uint32_t n, double margin, uint64_t* rows) const {
    if(n==0 || n>64 || !rows) return false;
    const uint64_t all=n==64 ? ~0ull : ((1ull<<n)-1ull);
    if(slot>=4 || !waterCoverKnown_[slot] || !(tile>0.0) || !std::isfinite(x0) || !std::isfinite(z0) || !(margin>=0.0)) { std::fill(rows, rows+n, all); return false; }
    std::fill(rows, rows+n, 0ull);
    const double x1=x0+tile*double(n), z1=z0+tile*double(n);
    const double half=0.5*tile+margin;
    for(const WaterSurface* surface:waterCover_[slot]) {
        if(!surface || surface->ranges.empty()) continue;
        int32_t stack[96]; int top=0; stack[top++]=0;
        while(top>0) {
            const WaterSurface::Range& r=surface->ranges[size_t(stack[--top])];
            if(r.hi[0]<x0-margin || r.lo[0]>x1+margin || r.hi[2]<z0-margin || r.lo[2]>z1+margin) continue;
            // A range no wider than a tile: its box marks the tiles it comes within the margin of (four at most) without a
            // look at its triangles, each of which lies in the box. (A coarse level's tiles are tens of metres wide and a
            // river's mesh has thousands of triangles: the list must not cost the CPU what it saves the card.)
            if(std::max(r.hi[0]-r.lo[0], r.hi[2]-r.lo[2])+2.0*margin<=tile) {
                const uint32_t a0=uint32_t(std::clamp(std::floor((r.lo[0]-margin-x0)/tile), 0.0, double(n-1))), a1=uint32_t(std::clamp(std::floor((r.hi[0]+margin-x0)/tile), 0.0, double(n-1)));
                const uint32_t b0=uint32_t(std::clamp(std::floor((r.lo[2]-margin-z0)/tile), 0.0, double(n-1))), b1=uint32_t(std::clamp(std::floor((r.hi[2]+margin-z0)/tile), 0.0, double(n-1)));
                for(uint32_t j=b0;j<=b1;++j) for(uint32_t k=a0;k<=a1;++k) rows[j]|=1ull<<k;
                continue;
            }
            if(r.children[0]>=0 && r.children[1]>=0 && top+2<=int(std::size(stack))) { stack[top++]=r.children[0]; stack[top++]=r.children[1]; continue; }
            for(uint32_t i=r.first;i<r.first+r.count && i<surface->triangles.size();++i) {
                const WaterSurface::Triangle& t=surface->triangles[i];
                const double px[3]={t.corner[0][0], t.corner[1][0], t.corner[2][0]}, pz[3]={t.corner[0][2], t.corner[1][2], t.corner[2][2]};
                const double lox=std::min({px[0], px[1], px[2]})-margin, hix=std::max({px[0], px[1], px[2]})+margin;
                const double loz=std::min({pz[0], pz[1], pz[2]})-margin, hiz=std::max({pz[0], pz[1], pz[2]})+margin;
                if(hix<x0 || lox>x1 || hiz<z0 || loz>z1) continue;
                const uint32_t i0=uint32_t(std::clamp(std::floor((lox-x0)/tile), 0.0, double(n-1))), i1=uint32_t(std::clamp(std::floor((hix-x0)/tile), 0.0, double(n-1)));
                const uint32_t j0=uint32_t(std::clamp(std::floor((loz-z0)/tile), 0.0, double(n-1))), j1=uint32_t(std::clamp(std::floor((hiz-z0)/tile), 0.0, double(n-1)));
                // Its edges: each one's line, and the side its third corner is on (0: no footprint, its box alone counts).
                double ex[3], ez[3], side[3]; bool edges=true;
                for(int e=0;e<3 && edges;++e) {
                    const int a=e, b=(e+1)%3, c=(e+2)%3;
                    ex[e]=-(pz[b]-pz[a]); ez[e]=px[b]-px[a];
                    const double third=ex[e]*(px[c]-px[a])+ez[e]*(pz[c]-pz[a]);
                    const double scale=(std::fabs(ex[e])+std::fabs(ez[e]))*(std::fabs(px[c]-px[a])+std::fabs(pz[c]-pz[a]));
                    if(!(std::fabs(third)>1e-9*scale) || !(scale>0.0)) edges=false;
                    side[e]=third>0.0 ? 1.0 : -1.0;
                }
                // The whole grid inside this one triangle (the sea's, a lake's): every tile at once.
                if(edges) {
                    const double gx=0.5*(x0+x1), gz=0.5*(z0+z1), ghalf=0.5*(x1-x0)+margin;
                    bool whole=true;
                    for(int e=0;e<3 && whole;++e) whole=side[e]*(ex[e]*(gx-px[e])+ez[e]*(gz-pz[e]))-(std::fabs(ex[e])+std::fabs(ez[e]))*ghalf>0.0;
                    if(whole) { std::fill(rows, rows+n, all); return true; }
                }
                for(uint32_t j=j0;j<=j1;++j) {
                    const double cz=z0+(double(j)+0.5)*tile;
                    for(uint32_t k=i0;k<=i1;++k) {
                        if(rows[j]&(1ull<<k)) continue;
                        const double cx=x0+(double(k)+0.5)*tile;
                        bool clear=false;
                        // (The square's farthest reach towards the triangle's side of the edge still on the other side: clear.)
                        for(int e=0;e<3 && edges && !clear;++e)
                            clear=side[e]*(ex[e]*(cx-px[e])+ez[e]*(cz-pz[e]))+(std::fabs(ex[e])+std::fabs(ez[e]))*half<0.0;
                        if(!clear) rows[j]|=1ull<<k;
                    }
                }
            }
        }
    }
    return true;
}

void GpuScene::release_water(DescriptorHeap* heap, DeferredRelease& release, uint64_t retireValue) {
    for(auto& g:waterGroups_) if(heap) heap->release(g->srv);
    waterGroups_.clear(); waterGroupUses_.clear(); waterGeometries_.clear();
    waterUnits_.clear(); waterUnitIndex_.clear(); waterSurfaces_.clear(); waterUses_.clear();
    for(int slot=0;slot<4;++slot) { waterCover_[slot].clear(); waterCoverKnown_[slot]=false; } // (they named the surfaces)
    for(auto& [handle, r]:waterReadbacks_) if(r.buffer) release.add(retireValue, r.buffer.resource.Get());
    waterReadbacks_.clear();
    waterMeshRecords_.clear(); uploadedWaterMeshRecords_.clear(); waterTris_.clear(); uploadedWaterTris_.clear(); uploadedWaterRecords_.clear();
    for(WaterPool* pool:{&waterVertexPool_, &waterBlasPool_}) {
        for(auto& page:pool->pages) { if(page.buffer) release.add(retireValue, page.buffer.resource.Get()); if(heap) heap->release(page.uav); }
        *pool=WaterPool{};
    }
    for(Buffer* b:{&waterIndices_, &waterScratch_}) { if(*b) release.add(retireValue, b->resource.Get()); *b=Buffer{}; }
    if(heap) heap->release(waterIndicesSrv_);
    waterIndicesSrv_=kInvalid;
    for(Structured* s:{&waterTriBuffer_, &waterTableBuffer_}) {
        if(s->buffer) release.add(retireValue, s->buffer.resource.Get());
        if(heap) heap->release(s->srv);
        *s=Structured{};
    }
}

void GpuScene::plan_water(ID3D12Device5* device, ID3D12GraphicsCommandList4* list, DescriptorHeap& heap, UploadRing& ring, DeferredRelease& release,
                          uint64_t retireValue, const std::vector<WaterRecordGpu>& waterRecords, const std::vector<uint32_t>& waterSources,
                          const double origin[3], const double camera[3], uint32_t geometryBase, Barriers& barriers) {
    ++waterFrame_;
    waterCutsOff_=memory2_off(); // PT-87: the test's switch, as this frame finds it (and whether it is set at all)
    waterCutsTest_=GetEnvironmentVariableA("TGPT_TEST_MEMORY2_OFF", nullptr, 0)>0;
    waterUses_.clear(); waterMeshRecords_.clear(); waterTris_.clear(); waterMade_=0;
    waterGroupUses_.clear(); waterGeometries_.clear();
    waterHighest=-1e300;
    for(auto& h:waterHighestSlot) h=-1e300;
    std::fill(std::begin(waterLevelTris_), std::end(waterLevelTris_), 0u);
    const bool traced=waterPass && !waterRecords.empty();
    // PT-114 (water_tiles): a slot's surfaces are all of its water in the top level when every one of its water meshes
    // gets its surface below (no water mesh at all: none to meet, which is known too).
    for(int slot=0;slot<4;++slot) { waterCover_[slot].clear(); waterCoverKnown_[slot]=traced || waterRecords.empty(); }
    // Made once: the lattice's table and the pattern of indices.
    if(traced && !waterIndices_) {
        waterTable_=water_lattice();
        const std::vector<uint32_t> pattern=water_pattern(waterTable_);
        const uint64_t bytes=pattern.size()*4;
        waterIndices_=make_buffer(device, bytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COMMON, "Create the water's indices");
        Buffer upload=make_buffer(device, bytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, "Create the water's indices' upload");
        std::memcpy(upload.mapped, pattern.data(), bytes);
        barriers.transition(waterIndices_, D3D12_RESOURCE_STATE_COPY_DEST); barriers.flush(list);
        list->CopyBufferRegion(waterIndices_.resource.Get(), 0, upload.resource.Get(), 0, bytes);
        barriers.transition(waterIndices_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        release.add(retireValue, upload.resource.Get());
        waterIndicesSrv_=raw_srv(device, heap, waterIndices_.resource.Get(), bytes);
        upload_structured(device, list, heap, ring, release, retireValue, waterTableBuffer_, waterTable_.data(), uint32_t(waterTable_.size()), 4, barriers);
    }

    for(size_t r=0;traced && r<waterRecords.size();++r) {
        const WaterRecordGpu& record=waterRecords[r];
        const TGPT_Instance& inst=instances_[waterSources[r]];
        Mesh* mesh=meshes_[size_t(inst.mesh)-1].get();
        // Its triangles, which the CPU keeps for a mesh it knew as water when it was uploaded; else back from the tracer's
        // own copy, a few frames from now (the surface is not traced until then).
        if(!water_triangles(device, list, *mesh, release, retireValue, barriers)) { waterCoverKnown_[record.slot&3u]=false; continue; }
        const uint32_t slot=record.slot&3u;
        const WaterSlot& water=waterSlots_[slot];
        const double cell=water.live ? double(water.cell) : 0.0;
        // The surface: this mesh where this instance puts it, for this slot's up direction.
        uint64_t key=1469598103934665603ull;
        auto mix=[&key](const void* p, size_t n) { for(size_t b=0;b<n;++b) { key^=static_cast<const uint8_t*>(p)[b]; key*=1099511628211ull; } };
        mix(&inst.mesh, 4); mix(inst.transform, sizeof(inst.transform)); mix(&slot, 4); mix(water.up, sizeof(water.up));
        WaterSurface& surface=waterSurfaces_[key];
        if(surface.ranges.empty()) {
            const auto& positions=mesh->data.positions; const auto& indices=mesh->data.indices;
            double lo[3]={1e300, 1e300, 1e300}, hi[3]={-1e300, -1e300, -1e300};
            for(const GeometryRecordGpu& geometry:mesh->data.geometries) {
                for(uint32_t at=geometry.indexStart;at+2<geometry.indexStart+geometry.indexCount && at+2<indices.size();at+=3) {
                    WaterSurface::Triangle t{}; t.index=at;
                    bool finite=true;
                    for(int c=0;c<3;++c) {
                        const uint32_t v=indices[at+uint32_t(c)];
                        if(size_t(v)*3+2>=positions.size()) { finite=false; break; }
                        const float* p=&positions[size_t(v)*3];
                        for(int a=0;a<3;++a) {
                            t.corner[c][a]=double(inst.transform[a*4])*p[0]+double(inst.transform[a*4+1])*p[1]+double(inst.transform[a*4+2])*p[2]+double(inst.transform[a*4+3]);
                            if(!std::isfinite(t.corner[c][a])) finite=false;
                        }
                    }
                    if(!finite) continue;
                    double e[3][3], n[3];
                    for(int a=0;a<3;++a) { e[0][a]=t.corner[1][a]-t.corner[0][a]; e[1][a]=t.corner[2][a]-t.corner[0][a]; e[2][a]=t.corner[2][a]-t.corner[1][a]; }
                    n[0]=e[0][1]*e[1][2]-e[0][2]*e[1][1]; n[1]=e[0][2]*e[1][0]-e[0][0]*e[1][2]; n[2]=e[0][0]*e[1][1]-e[0][1]*e[1][0];
                    if(!(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]>0.0)) continue; // no area: no water
                    // Its front the water's outward side: the side its up direction is on.
                    t.swapped=n[0]*water.up[0]+n[1]*water.up[1]+n[2]*water.up[2]<0.0;
                    if(t.swapped) for(int a=0;a<3;++a) std::swap(t.corner[1][a], t.corner[2][a]);
                    for(int k=0;k<3;++k) t.longest=std::max(t.longest, std::sqrt(e[k][0]*e[k][0]+e[k][1]*e[k][1]+e[k][2]*e[k][2]));
                    for(int c=0;c<3;++c) for(int a=0;a<3;++a) { lo[a]=std::min(lo[a], t.corner[c][a]); hi[a]=std::max(hi[a], t.corner[c][a]); }
                    surface.triangles.push_back(t);
                }
            }
            // Along a space-filling curve by their middles, so a range of them lies together.
            std::vector<std::pair<uint64_t, uint32_t>> order(surface.triangles.size());
            for(size_t i=0;i<surface.triangles.size();++i) {
                uint64_t code=0;
                for(int a=0;a<3;++a) {
                    const auto& t=surface.triangles[i];
                    const double middle=(t.corner[0][a]+t.corner[1][a]+t.corner[2][a])/3.0;
                    const double unit=hi[a]>lo[a] ? (middle-lo[a])/(hi[a]-lo[a]) : 0.0;
                    code|=spread3(uint64_t(std::clamp(unit, 0.0, 1.0)*2097151.0))<<a;
                }
                order[i]={code, uint32_t(i)};
            }
            std::sort(order.begin(), order.end());
            std::vector<WaterSurface::Triangle> sorted(order.size());
            for(size_t i=0;i<order.size();++i) sorted[i]=surface.triangles[order[i].second];
            surface.triangles=std::move(sorted);
            // The tree of ranges, halved down to single triangles.
            struct Build { static int32_t range(WaterSurface& s, uint32_t first, uint32_t count) {
                const int32_t at=int32_t(s.ranges.size());
                s.ranges.push_back(WaterSurface::Range{});
                WaterSurface::Range r{}; r.first=first; r.count=count; r.children[0]=r.children[1]=-1;
                for(int a=0;a<3;++a) { r.lo[a]=1e300; r.hi[a]=-1e300; }
                for(uint32_t i=first;i<first+count;++i) {
                    const auto& t=s.triangles[i];
                    r.longest=std::max(r.longest, t.longest);
                    for(int c=0;c<3;++c) for(int a=0;a<3;++a) { r.lo[a]=std::min(r.lo[a], t.corner[c][a]); r.hi[a]=std::max(r.hi[a], t.corner[c][a]); }
                }
                if(count>1) { r.children[0]=range(s, first, count/2); r.children[1]=range(s, first+count/2, count-count/2); }
                s.ranges[size_t(at)]=r;
                return at;
            } };
            if(!surface.triangles.empty()) Build::range(surface, 0, uint32_t(surface.triangles.size()));
        }
        surface.usedFrame=waterFrame_;
        waterCover_[slot].push_back(&surface);
        if(surface.ranges.empty()) continue;
        waterHighest=std::max(waterHighest, surface.ranges[0].hi[1]); // its root range: every triangle of the surface (PT-99)
        waterHighestSlot[slot]=std::max(waterHighestSlot[slot], surface.ranges[0].hi[1]);

        // A unit this frame wants: found or made, and named this frame's record.
        // (nearest: how near the camera is to the piece, metres: kept for the log's count by distance alone, PT-87.)
        auto use=[&](uint32_t first, uint32_t count, uint32_t level, uint32_t node, uint32_t i, uint32_t j, bool flip, uint32_t cuts, double nearest) {
            const std::array<uint64_t, 4> unitKey{key, (uint64_t(first)<<32)|count, (uint64_t(level)<<40)|(uint64_t(flip ? 1u : 0u)<<32)|cuts|(uint64_t(node)<<48), (uint64_t(i)<<32)|j};
            // (PT-99: found through waterUnitIndex_, a hash of the same keys onto the map's own units, where the map
            // itself was searched for every piece every frame: 1,400 to 1,800 searches of eleven steps through its nodes.
            // The map still holds the units and is still what is walked where their order matters. A piece not in it
            // yet is put into both, as `waterUnits_[unitKey]` put it into the one.)
            WaterUnit* held=nullptr;
            if(const auto known=waterUnitIndex_.find(unitKey); known!=waterUnitIndex_.end()) held=known->second;
            else { held=&waterUnits_[unitKey]; waterUnitIndex_.emplace(unitKey, held); }
            WaterUnit& u=*held;
            if(u.usedFrame==waterFrame_) return; // the same piece of the same mesh at the same place twice in the scene's list
            if(!u.made) {
                // What the unit is: nothing on the GPU yet (its group has that: below, once the frame's units are known).
                u.made=true; u.key=unitKey;
                u.triangles=count; u.level=cuts; u.slot=slot;
                u.vertexCount=count*kWaterLatticeVertices[cuts];
                // Where it is collapsed to in a frame its group is traced without it: its own first corner, undisplaced
                // (a node's, or its first base triangle's), so its group's boxes stay where the group is.
                {
                    const WaterSurface::Triangle& triangle=surface.triangles[first];
                    const double scale=std::ldexp(1.0, -int(level)), cu=(flip ? i+1.0 : double(i))*scale, cv=(flip ? j+1.0 : double(j))*scale;
                    for(int a=0;a<3;++a) u.corner[a]=triangle.corner[0][a]*(1.0-cu-cv)+triangle.corner[1][a]*cu+triangle.corner[2][a]*cv;
                }
                u.tris.resize(count);
                for(uint32_t t=0;t<count;++t) {
                    const WaterSurface::Triangle& triangle=surface.triangles[first+t];
                    WaterTriGpu& g=u.tris[t];
                    g.record=0; g.index=triangle.index;
                    g.node=level|(flip ? 32u : 0u)|(triangle.swapped ? 64u : 0u)|(cuts<<8);
                    g.i=i; g.j=j;
                    g.at=0; g.span=0; g.target=kInvalid; // (its place in its group: water_group_make)
                }
                ++waterMade_;
            }
            u.usedFrame=waterFrame_; u.nearest=float(nearest);
            for(auto& t:u.tris) t.record=uint32_t(r);
            waterUses_.push_back(&u);
        };
        // A node of a base triangle's lattice (level L, cell i j, pointing the base triangle's way or the other): cut whole if
        // 2^kWaterLevels pieces an edge are enough for the camera where it is nearest, else its four quarters asked again.
        struct Walk {
            const WaterSurface& surface; const double* camera; double cell; decltype(use)& emit;
            void node(uint32_t triangle, uint32_t level, uint32_t i, uint32_t j, bool flip) {
                const WaterSurface::Triangle& t=surface.triangles[triangle];
                const double scale=std::ldexp(1.0, -int(level));
                const double uv[3][2]={{flip ? i+1.0 : double(i), flip ? j+1.0 : double(j)}, {flip ? double(i) : i+1.0, flip ? j+1.0 : double(j)}, {flip ? i+1.0 : double(i), flip ? double(j) : j+1.0}};
                double lo[3]={1e300, 1e300, 1e300}, hi[3]={-1e300, -1e300, -1e300};
                for(int c=0;c<3;++c) for(int a=0;a<3;++a) {
                    const double u=uv[c][0]*scale, v=uv[c][1]*scale;
                    const double p=t.corner[0][a]*(1.0-u-v)+t.corner[1][a]*u+t.corner[2][a]*v;
                    lo[a]=std::min(lo[a], p); hi[a]=std::max(hi[a], p);
                }
                const double nearest=box_distance(lo, hi, camera);
                const uint32_t cuts=water_cuts(t.longest*scale, nearest, cell);
                // Past level 19 a float no longer tells the lattice's points apart (24 bits with the node's own 5).
                if(cuts<=kWaterLevels || level>=19) { emit(triangle, 1, level, 1, i, j, flip, std::min(cuts, kWaterLevels), nearest); return; }
                if(!flip) {
                    node(triangle, level+1, 2*i, 2*j, false); node(triangle, level+1, 2*i+1, 2*j, false); node(triangle, level+1, 2*i, 2*j+1, false);
                    node(triangle, level+1, 2*i, 2*j, true);
                } else {
                    node(triangle, level+1, 2*i+1, 2*j+1, true); node(triangle, level+1, 2*i, 2*j+1, true); node(triangle, level+1, 2*i+1, 2*j, true);
                    node(triangle, level+1, 2*i+1, 2*j+1, false);
                }
            }
            void range(int32_t at) {
                const WaterSurface::Range& r=surface.ranges[size_t(at)];
                const double nearest=box_distance(r.lo, r.hi, camera);
                const uint32_t cuts=water_cuts(r.longest, nearest, cell);
                if(cuts<=kWaterLevels && (uint64_t(r.count)<<(2*cuts))<=kWaterUnitTriangles) { emit(r.first, r.count, 0, 0, 0, 0, false, cuts, nearest); return; }
                if(r.count>1) { range(r.children[0]); range(r.children[1]); return; }
                node(r.first, 1, 0, 0, false); node(r.first, 1, 1, 0, false); node(r.first, 1, 0, 1, false); node(r.first, 1, 0, 0, true);
            }
        } walk{surface, camera, cell, use};
        walk.range(0);
    }
    // Units and surfaces no frame used for a while go.
    for(auto it=waterUnits_.begin();it!=waterUnits_.end();) {
        if(waterFrame_-it->second.usedFrame>kWaterKeepFrames) { water_unit_gone(it->second); waterUnitIndex_.erase(it->first); it=waterUnits_.erase(it); }
        else ++it;
    }

    // THE GROUPS (PT-99, scene.hpp WaterGroup; docs/agents/waterblas.md).
    // The test's switch, as this frame finds it: a group holds one unit and nothing is put together (a bottom level, a
    // refit and an instance a unit: what the tracer did until 2026-10-12). Changed within a run: every group goes first.
    const bool ungrouped=GetEnvironmentVariableA("TGPT_TEST_WATER_UNGROUPED", nullptr, 0)>0;
    if(ungrouped!=waterUngrouped_) {
        for(auto& g:waterGroups_) water_group_free(*g, &heap);
        waterGroups_.clear();
        waterUngrouped_=ungrouped;
        log(ungrouped ? "PT test, the water's pieces: UNGROUPED (TGPT_TEST_WATER_UNGROUPED): a bottom level, a refit and an instance a piece, as before"
                      : "PT test, the water's pieces: in groups again");
    }
    // A collapsed unit's point was written relative to the scene's origin: when that moves, it is written again.
    if(origin[0]!=waterOrigin_[0] || origin[1]!=waterOrigin_[1] || origin[2]!=waterOrigin_[2]) {
        for(auto& g:waterGroups_) for(auto& m:g->members) m.collapsed=false;
        for(int a=0;a<3;++a) waterOrigin_[a]=origin[a];
    }
    waterGroupsNew_=waterGroupsMerged_=waterMergedFrom_=waterCollapsed_=waterDead_=0; waterBuiltTriangles_=0;
    const uint64_t mostTriangles=ungrouped ? 0u : kWaterGroupTriangles;
    const size_t mostMembers=ungrouped ? 1u : kWaterGroupMembers;
    auto triangles_of=[](const WaterUnit& u) { return uint64_t(u.triangles)<<(2*u.level); };
    // Each group's frame, counted anew (nothing of it is kept): ONE pass over this frame's units. A unit in a group
    // counts to it; one in no group (a new one, or one whose group was made anew in a frame that did not want it) is
    // put by for step 1.
    for(auto& g:waterGroups_) { g->used=0; g->usedBase=0; g->use=kInvalid; }
    std::vector<WaterUnit*> fresh[4][kWaterLevels+1];
    for(WaterUnit* u:waterUses_) {
        if(WaterGroup* g=u->group) { ++g->used; g->usedBase+=u->triangles; }
        else fresh[u->slot&3u][std::min(u->level, kWaterLevels)].push_back(u);
    }
    // 1. This frame's units that are in no group: together, a group a slot and level, in the order the walk met them
    // (along the surface's curve, so a group lies together). One that cannot have its blocks is halved, down to a single
    // unit; a unit that cannot have them is not traced this frame and is asked for again by the next (it leaves the map,
    // as a unit that could not be made always did). (water_group_make counts a new group's units: all are wanted.)
    {
        std::vector<WaterUnit*> failed;
        struct Place {
            GpuScene& scene; ID3D12Device5* device; DescriptorHeap& heap; std::vector<WaterUnit*>& failed;
            void operator()(const std::vector<WaterUnit*>& batch) {
                if(batch.empty()) return;
                if(scene.water_group_make(device, heap, batch)) { ++scene.waterGroupsNew_; return; }
                if(batch.size()==1) { failed.push_back(batch[0]); return; }
                const auto middle=batch.begin()+std::ptrdiff_t(batch.size()/2);
                (*this)(std::vector<WaterUnit*>(batch.begin(), middle)); (*this)(std::vector<WaterUnit*>(middle, batch.end()));
            }
        } place{*this, device, heap, failed};
        for(auto& slotUnits:fresh) for(auto& units:slotUnits) {
            if(units.empty()) continue;
            std::vector<WaterUnit*> batch; uint64_t batchTriangles=0;
            for(WaterUnit* u:units) {
                if(!batch.empty() && (batch.size()>=mostMembers || batchTriangles+triangles_of(*u)>mostTriangles)) { place(batch); batch.clear(); batchTriangles=0; }
                batch.push_back(u); batchTriangles+=triangles_of(*u);
            }
            place(batch);
        }
        for(WaterUnit* u:failed) {
            log_throttled("A piece of the water's surface could not be made: no room for it in the water's pools");
            waterUses_.erase(std::remove(waterUses_.begin(), waterUses_.end(), u), waterUses_.end());
            const std::array<uint64_t, 4> unitKey=u->key;
            water_unit_gone(*u);
            waterUnits_.erase(unitKey); waterUnitIndex_.erase(unitKey);
        }
    }
    // Each group's dropped units (their entries name no unit: read off the entries, no unit is looked at). A group with
    // no unit left goes.
    for(auto it=waterGroups_.begin();it!=waterGroups_.end();) {
        WaterGroup& g=**it;
        g.dead=0; g.deadBase=0;
        for(const auto& m:g.members) if(!m.unit) { ++g.dead; g.deadBase+=m.count; }
        if(g.dead==g.members.size()) { water_group_free(g, &heap); it=waterGroups_.erase(it); } else ++it;
    }
    // 2. At most one group a frame put together of several (a walk makes a small group a slot and level every frame it
    // brings new units), or made anew to be rid of its dropped units' geometry. THIS CHANGES SPEED AND MEMORY ALONE: the
    // same units are traced whichever groups hold them. First a group half of whose triangles are dropped units'; else,
    // while more groups are in use than half as many again as the fewest that could hold this frame's units (and
    // kWaterGroupsSpare more than those at least), the slot and level whose smallest groups put the most of them
    // into one: the smallest first, and after the first two a group only while it is no more than twice what is taken
    // already (so a large group is not built again for the sake of a few small ones every frame of a walk). At most
    // kWaterMergeTriangles are built for it. The group made holds this frame's units of those it is made of, in their
    // order; their other units hold nothing until a frame wants them again.
    if(!ungrouped) {
        uint32_t inUse=0, fewest=0;
        uint64_t bucketTriangles[4][kWaterLevels+1]{}, bucketMembers[4][kWaterLevels+1]{};
        WaterGroup* worst=nullptr;
        for(auto& held:waterGroups_) {
            WaterGroup& g=*held;
            if(!g.used) continue;
            ++inUse;
            bucketTriangles[g.slot&3u][std::min(g.level, kWaterLevels)]+=g.usedTriangles(); bucketMembers[g.slot&3u][std::min(g.level, kWaterLevels)]+=g.used;
            if(g.deadBase && g.deadBase>=g.usedBase && (!worst || g.deadTriangles()>worst->deadTriangles())) worst=&g;
        }
        for(uint32_t s=0;s<4;++s) for(uint32_t l=0;l<=kWaterLevels;++l)
            fewest+=uint32_t(std::max((bucketTriangles[s][l]+kWaterGroupTriangles-1)/kWaterGroupTriangles, (bucketMembers[s][l]+kWaterGroupMembers-1)/kWaterGroupMembers));
        const uint32_t wanted=fewest+std::max(kWaterGroupsSpare, fewest/2);
        const uint64_t room=std::min<uint64_t>(kWaterGroupTriangles, kWaterMergeTriangles);
        // (The frame's groups in use of a slot and level, the smallest first: made only when one is to be put together.)
        auto bucket=[&](uint32_t slot, uint32_t level) {
            std::vector<WaterGroup*> groups;
            for(auto& held:waterGroups_) if(held->used && (held->slot&3u)==slot && std::min(held->level, kWaterLevels)==level) groups.push_back(held.get());
            std::sort(groups.begin(), groups.end(), [](const WaterGroup* a, const WaterGroup* b) { return a->usedBase!=b->usedBase ? a->usedBase<b->usedBase : a->serial<b->serial; });
            return groups;
        };
        auto choose=[&](const std::vector<WaterGroup*>& groups, WaterGroup* seed) {
            std::vector<WaterGroup*> chosen;
            uint64_t triangles=0; size_t members=0;
            if(seed) { chosen.push_back(seed); triangles=seed->usedTriangles(); members=seed->used; }
            for(WaterGroup* g:groups) {
                if(g==seed) continue;
                if(triangles+g->usedTriangles()>room || members+g->used>kWaterGroupMembers) break;
                if(chosen.size()>=2 && g->usedTriangles()>2*triangles) break;
                chosen.push_back(g); triangles+=g->usedTriangles(); members+=g->used;
            }
            return chosen;
        };
        std::vector<WaterGroup*> chosen;
        if(worst && worst->usedTriangles()<=room) chosen=choose(bucket(worst->slot&3u, std::min(worst->level, kWaterLevels)), worst);
        else if(inUse>wanted) {
            for(uint32_t s=0;s<4;++s) for(uint32_t l=0;l<=kWaterLevels;++l) {
                if(!bucketMembers[s][l]) continue;
                std::vector<WaterGroup*> candidate=choose(bucket(s, l), nullptr);
                if(candidate.size()>=2 && candidate.size()>chosen.size()) chosen=std::move(candidate);
            }
        }
        if(!chosen.empty()) {
            std::sort(chosen.begin(), chosen.end(), [](const WaterGroup* a, const WaterGroup* b) { return a->serial<b->serial; });
            std::vector<WaterUnit*> units;
            for(const WaterGroup* g:chosen) for(const auto& m:g->members) if(m.unit && m.unit->usedFrame==waterFrame_) units.push_back(m.unit);
            // (The group made counts its own units, all wanted; those it is made of go; no other group's count changes.)
            if(water_group_make(device, heap, units)) {
                ++waterGroupsMerged_; ++waterMergesEver_; waterMergedFrom_=uint32_t(chosen.size());
                for(WaterGroup* g:chosen) {
                    water_group_free(*g, &heap);
                    waterGroups_.erase(std::find_if(waterGroups_.begin(), waterGroups_.end(), [g](const std::unique_ptr<WaterGroup>& held) { return held.get()==g; }));
                }
            }
        }
    }
    // FOR A CHECK (the environment's TGPT_TEST_RECORD_CHECK): the index names every unit of the map, and nothing else.
    if(recordCheck_) {
        bool agree=waterUnitIndex_.size()==waterUnits_.size();
        for(auto it=waterUnits_.begin();agree && it!=waterUnits_.end();++it) {
            const auto known=waterUnitIndex_.find(it->first);
            agree=known!=waterUnitIndex_.end() && known->second==&it->second;
        }
        if(!agree) log_throttled("PT record check: THE WATER'S INDEX DIFFERS from its map of units ("+std::to_string(waterUnitIndex_.size())+" against "+std::to_string(waterUnits_.size())+")");
        else if(!recordCheckSaid_[3] && !waterUnits_.empty()) { recordCheckSaid_[3]=true; log("PT record check: the water's index names the map's own units ("+std::to_string(waterUnits_.size())+"); checked at every frame"); }
    }
    for(auto it=waterSurfaces_.begin();it!=waterSurfaces_.end();) {
        if(waterFrame_-it->second.usedFrame>kWaterKeepFrames) it=waterSurfaces_.erase(it); else ++it;
    }
    // PT-87: a page no unit has had a block in for as long goes back to the driver (the GPU is long past its last reader),
    // with the free blocks that were in it.
    if(!(waterCutsOff_&kMemory2Water)) for(WaterPool* pool:{&waterVertexPool_, &waterBlasPool_}) for(uint32_t p=0;p<pool->pages.size();++p) {
        auto& page=pool->pages[p];
        if(!page.buffer || page.live || waterFrame_-page.idleSince<=kWaterKeepFrames) continue;
        release.add(retireValue, page.buffer.resource.Get());
        heap.release(page.uav);
        page=WaterPool::Page{};
        ++waterPagesBack_;
    }
    // The finest first: the pose's dispatch for a level runs over the triangles that reach it, which are then the first.
    std::stable_sort(waterUses_.begin(), waterUses_.end(), [](const WaterUnit* a, const WaterUnit* b) { return a->level>b->level; });
    // 3. The groups this frame traces (those with a unit it wants), in the order they were made; ungrouped, in the
    // units' order above (a group a unit: the order of before). Laid out anew every frame: no number of this is kept.
    if(ungrouped) { for(const WaterUnit* u:waterUses_) if(u->group) waterGroupUses_.push_back(u->group); }
    else for(auto& g:waterGroups_) if(g->used) waterGroupUses_.push_back(g.get());
    std::vector<WaterTriGpu> collapse[kWaterLevels+1]; // the pose records of the units collapsed this frame, by level (few or none)
    waterUsedVertices_=0; waterUsedTriangles_=0;
    for(size_t k=0;k<waterGroupUses_.size();++k) {
        WaterGroup& g=*waterGroupUses_[k];
        g.use=uint32_t(k);
        // Its mesh record: its vertices (all its units' positions, undisplaced points, positions a frame ago), the shared
        // pattern, a submesh a unit; and its units' geometry records, made with it (water_group_make).
        waterMeshRecords_.push_back(MeshRecordGpu{g.srv, kInvalid, kInvalid, waterIndicesSrv_, geometryBase+uint32_t(waterGeometries_.size()), uint32_t(g.members.size()),
                                                  g.vertexCount, kInvalid, kInvalid});
        waterGeometries_.insert(waterGeometries_.end(), g.records.begin(), g.records.end());
        const uint32_t perTriangle=kWaterLatticeVertices[g.level];
        waterUsedVertices_+=g.usedBase*perTriangle; waterUsedTriangles_+=g.usedTriangles();
        waterDead_+=g.dead;
        // Every unit of it is wanted and none is marked collapsed (the usual frame): nothing more to say of it.
        if(g.used==g.members.size() && !g.anyCollapsed) continue;
        g.anyCollapsed=false;
        for(auto& m:g.members) {
            if(m.unit && m.unit->usedFrame==waterFrame_) { m.collapsed=false; continue; }
            // A unit the frame does not want (or a dropped one's geometry): its vertices to one point, once; they stay
            // there until a frame wants it again (the pose pass then writes them as always).
            g.anyCollapsed=true;
            if(m.collapsed) continue;
            m.collapsed=true; ++waterCollapsed_;
            const float point[3]={float(m.corner[0]-origin[0]), float(m.corner[1]-origin[1]), float(m.corner[2]-origin[2])};
            WaterTriGpu tri{};
            std::memcpy(&tri.index, &point[0], 4); std::memcpy(&tri.i, &point[1], 4); std::memcpy(&tri.j, &point[2], 4);
            tri.node=kWaterTriCollapse|(g.level<<8); tri.span=g.vertexCount*12; tri.target=waterVertexPool_.pages[g.vertexPage].uav;
            for(uint32_t t=0;t<m.count;++t) { tri.at=g.vertexAt+(m.first+t)*perTriangle*12; collapse[g.level].push_back(tri); }
        }
    }
    // The pose pass's records: level by level, the finest first (the units stand in that order), each level's wanted
    // units' and then its collapsed units'; a level's dispatch runs over the records that reach it.
    {
        size_t next=0;
        for(uint32_t level=kWaterLevels+1;level-->0;) {
            const size_t before=waterTris_.size();
            for(;next<waterUses_.size() && std::min(waterUses_[next]->level, kWaterLevels)>=level;++next) {
                const WaterUnit& u=*waterUses_[next];
                if(u.group) waterTris_.insert(waterTris_.end(), u.tris.begin(), u.tris.end());
            }
            waterTris_.insert(waterTris_.end(), collapse[level].begin(), collapse[level].end());
            for(uint32_t reached=0;reached<=level;++reached) waterLevelTris_[reached]+=uint32_t(waterTris_.size()-before);
        }
    }
    if(recordCheck_) water_check();
}

// FOR A CHECK (the environment's TGPT_TEST_RECORD_CHECK, or the file record-check beside the DLL; docs/agents/waterblas.md):
// what is kept of the groups from frame to frame agrees with itself and with what would be made again. Every unit this
// frame wants is in exactly one group, at a place of its own in that group's block; its pose records point there; every
// group's description is the one its blocks and members give; no unit names a group that is not held.
void GpuScene::water_check() {
    std::string wrong;
    auto fail=[&wrong](const std::string& what) { if(wrong.empty()) wrong=what; };
    std::unordered_map<const WaterGroup*, size_t> held;
    for(size_t k=0;k<waterGroups_.size();++k) held.emplace(waterGroups_[k].get(), k);
    const D3D12_GPU_VIRTUAL_ADDRESS patternVa=waterIndices_.va();
    for(const auto& own:waterGroups_) {
        const WaterGroup& g=*own;
        const uint32_t perTriangle=kWaterLatticeVertices[std::min(g.level, kWaterLevels)];
        if(g.vertexPage>=waterVertexPool_.pages.size() || !waterVertexPool_.pages[g.vertexPage].buffer || g.blasPage>=waterBlasPool_.pages.size() ||
           !waterBlasPool_.pages[g.blasPage].buffer) { fail("a group's block is in a page that is not there"); continue; }
        if(g.geometry.size()!=g.members.size() || g.members.empty()) { fail("a group's description is not its members'"); continue; }
        if(uint64_t(g.vertexCount)*36>g.vertexBlock || g.vertexCount!=g.baseTriangles*perTriangle) fail("a group's vertices do not fit its block");
        const D3D12_GPU_VIRTUAL_ADDRESS vertices=waterVertexPool_.pages[g.vertexPage].buffer.va()+g.vertexAt;
        uint32_t next=0;
        // (This frame's count, made the plain way: every member looked at. And the kept geometry records, made again.)
        {
            uint32_t used=0, usedBase=0, dead=0, deadBase=0; bool collapsed=false;
            for(const auto& m:g.members) {
                if(!m.unit) { ++dead; deadBase+=m.count; } else if(m.unit->usedFrame==waterFrame_) { ++used; usedBase+=m.count; }
                collapsed=collapsed || m.collapsed;
            }
            if(used!=g.used || usedBase!=g.usedBase || dead!=g.dead || deadBase!=g.deadBase) fail("a group's count of the frame is not its members'");
            if(collapsed && !g.anyCollapsed) fail("a group holds a collapsed unit and does not say so");
            if(g.records.size()!=g.members.size()) fail("a group's kept geometry records are not one a member");
            else for(size_t k=0;k<g.members.size();++k) {
                const uint32_t indicesEach=3u<<(2*g.level);
                if(g.records[k].indexStart!=g.level*kWaterLevelIndices+g.members[k].first*indicesEach || g.records[k].indexCount!=g.members[k].count*indicesEach ||
                   g.records[k].layers || g.records[k].flags) fail("a group's kept geometry record is not what its member gives");
            }
        }
        for(size_t k=0;k<g.members.size();++k) {
            const WaterGroup::Member& m=g.members[k];
            if(m.first!=next) fail("a group's members do not lie one after another");
            next=m.first+m.count;
            const auto& t=g.geometry[k].Triangles;
            if(t.VertexBuffer.StartAddress!=vertices+uint64_t(m.first)*perTriangle*12 || t.VertexCount!=m.count*perTriangle || t.IndexCount!=(m.count<<(2*g.level))*3 ||
               t.IndexBuffer!=patternVa+uint64_t(g.level)*kWaterLevelIndices*4) fail("a group's kept description is not what its block and members give");
            if(uint64_t(m.first+m.count)<<(2*g.level)>kWaterGroupTriangles) fail("a member's indices lie past the pattern");
            if(!m.unit) continue;
            const WaterUnit& u=*m.unit;
            if(u.group!=&g || u.member!=k) fail("a member's unit names another group or place");
            if(u.level!=g.level || u.slot!=g.slot || u.triangles!=m.count || u.tris.size()!=m.count) fail("a member's unit is not of its group's kind");
            for(uint32_t i=0;i<u.tris.size() && i<m.count;++i)
                if(u.tris[i].at!=g.vertexAt+(m.first+i)*perTriangle*12 || u.tris[i].span!=g.vertexCount*12 || u.tris[i].target!=waterVertexPool_.pages[g.vertexPage].uav)
                    fail("a unit's pose records do not point at its place in its group");
            if(u.usedFrame==waterFrame_ && m.collapsed) fail("a unit this frame wants is marked collapsed");
            if(u.usedFrame==waterFrame_ && g.use==kInvalid) fail("a unit this frame wants is in a group the frame does not trace");
        }
        if(next!=g.baseTriangles) fail("a group's members do not add up to its base triangles");
        if(g.use!=kInvalid && (g.use>=waterGroupUses_.size() || waterGroupUses_[g.use]!=&g)) fail("a group's place among the frame's groups is not its own");
        if(g.use!=kInvalid) for(const auto& m:g.members) if(!(m.unit && m.unit->usedFrame==waterFrame_) && !m.collapsed) fail("a traced group holds a unit that is neither wanted nor collapsed");
    }
    size_t grouped=0;
    for(const auto& [key, u]:waterUnits_) {
        if(u.key!=key) fail("a unit's own key is not its key in the map");
        if(!u.group) continue;
        ++grouped;
        if(!held.count(u.group)) { fail("a unit names a group that is not held"); continue; }
        if(u.member>=u.group->members.size() || u.group->members[u.member].unit!=&u) fail("a unit's group does not name it");
    }
    for(const WaterUnit* u:waterUses_) if(!u->group) fail("a unit this frame wants is in no group");
    uint32_t geometries=0;
    for(const WaterGroup* g:waterGroupUses_) geometries+=uint32_t(g->members.size());
    if(geometries!=waterGeometries_.size() || waterGroupUses_.size()!=waterMeshRecords_.size()) fail("the frame's records are not the traced groups'");
    // The pools: every page is the groups' blocks in it and its free stretches, side by side with nothing between, over
    // or left; no two free stretches touch; the counts of blocks and bytes are the groups'.
    for(int which=0;which<2;++which) {
        const WaterPool& pool=which ? waterBlasPool_ : waterVertexPool_;
        const uint64_t pageBytes=which ? kBlasPageBytes : kVertexPageBytes;
        std::vector<std::vector<std::pair<uint32_t, uint32_t>>> blocks(pool.pages.size());
        uint64_t bytes=0;
        for(const auto& own:waterGroups_) {
            const uint32_t page=which ? own->blasPage : own->vertexPage, at=which ? own->blasAt : own->vertexAt, block=which ? own->blasBlock : own->vertexBlock;
            if(page<blocks.size()) blocks[page].emplace_back(at, block);
            bytes+=block;
        }
        if(bytes!=pool.liveBytes) fail(which ? "the structures' pool counts other bytes than the groups hold" : "the vertices' pool counts other bytes than the groups hold");
        for(size_t p=0;p<pool.pages.size();++p) {
            const WaterPool::Page& page=pool.pages[p];
            if(!page.buffer) { if(!blocks[p].empty()) fail("a group's block is in a page that was given back"); continue; }
            if(page.live!=blocks[p].size()) fail("a page counts other blocks than the groups hold in it");
            std::vector<std::pair<uint32_t, uint32_t>> all=blocks[p];
            uint32_t last=0xFFFFFFFFu;
            for(const auto& [at, stretch]:page.free) { if(at==last) fail("two free stretches of a page touch"); last=at+stretch; all.emplace_back(at, stretch); }
            std::sort(all.begin(), all.end());
            uint64_t next=0;
            for(const auto& [at, stretch]:all) {
                if(at!=next || !stretch || (at&255u)) fail("a page's blocks and free stretches overlap, leave a gap or start off a structure's alignment");
                next=uint64_t(at)+stretch;
            }
            if(next!=pageBytes) fail("a page's blocks and free stretches do not add up to the page");
        }
    }
    if(!wrong.empty()) log_throttled("PT record check: THE WATER'S GROUPS DIFFER from what would be made again: "+wrong+" ("+std::to_string(waterGroups_.size())+" groups, "+
                                     std::to_string(waterUnits_.size())+" units)");
    else if(!recordCheckSaid_[4] && !waterGroups_.empty()) {
        recordCheckSaid_[4]=true;
        log("PT record check: the water's groups agree with their units and blocks ("+std::to_string(waterGroups_.size())+" groups, "+std::to_string(grouped)+
            " units in them); checked at every frame");
    }
}

void GpuScene::pose_water(ID3D12Device5* device, ID3D12GraphicsCommandList4* list, DescriptorHeap& heap, UploadRing& ring, DeferredRelease& release,
                          uint64_t retireValue, uint32_t meshBase, uint32_t materialBase, std::vector<D3D12_RAYTRACING_INSTANCE_DESC>& descs,
                          std::vector<InstanceRecordGpu>& records, Barriers& barriers) {
    if(waterGroupUses_.empty() || !waterPass) return;
    if(!waterTriBuffer_.buffer || waterTris_.size()!=uploadedWaterTris_.size() ||
       std::memcmp(waterTris_.data(), uploadedWaterTris_.data(), waterTris_.size()*sizeof(WaterTriGpu))!=0) {
        upload_structured(device, list, heap, ring, release, retireValue, waterTriBuffer_, waterTris_.data(), uint32_t(waterTris_.size()), sizeof(WaterTriGpu), barriers);
        uploadedWaterTris_=waterTris_;
    }
    // This frame's pose: a dispatch a lattice level, the coarsest first (a point reads its parents).
    std::vector<uint8_t> used(waterVertexPool_.pages.size(), 0);
    for(const WaterGroup* g:waterGroupUses_) used[g->vertexPage]=1;
    for(size_t p=0;p<used.size();++p) if(used[p]) barriers.transition(waterVertexPool_.pages[p].buffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    barriers.flush(list);
    list->SetPipelineState(waterPass);
    for(uint32_t level=0;level<=kWaterLevels;++level) {
        if(!waterLevelTris_[level]) break;
        const uint32_t points=kWaterLatticeVertices[level]-(level ? kWaterLatticeVertices[level-1] : 0u);
        const uint32_t values[8]={waterTriBuffer_.srv, waterLevelTris_[level], level, waterTableBuffer_.srv, 0, 0, 0, 0};
        list->SetComputeRoot32BitConstants(1, 8, values, 0);
        list->Dispatch(UINT((uint64_t(waterLevelTris_[level])*points+63)/64), 1, 1);
        barriers.uav(nullptr); barriers.flush(list);
    }
    for(size_t p=0;p<used.size();++p) if(used[p]) barriers.transition(waterVertexPool_.pages[p].buffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    barriers.flush(list);
    // The groups' acceleration structures: built where new, refitted to the pose otherwise, side by side in the scratch
    // (a build's scratch for a build, a refit's for a refit). ONE CALL A GROUP (PT-99: it was one a unit).
    uint64_t scratchNeeded=0;
    for(const WaterGroup* g:waterGroupUses_) scratchNeeded+=((g->built ? g->updateBytes : g->scratchBytes)+255)/256*256+256;
    if(waterScratch_.size<scratchNeeded) {
        if(waterScratch_) release.add(retireValue, waterScratch_.resource.Get());
        waterScratch_=make_buffer(device, std::max<uint64_t>(scratchNeeded*3/2, 8ull<<20), D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                                  D3D12_RESOURCE_STATE_UNORDERED_ACCESS, "Create the water's BLAS scratch");
    }
    uint64_t offset=0;
    const uint32_t vertices=waterUsedVertices_; const uint64_t triangles=waterUsedTriangles_; // (this frame's traced units': plan_water)
    // (PT-99: each buffer's address asked of D3D12 once a frame: the same addresses.)
    std::vector<D3D12_GPU_VIRTUAL_ADDRESS> blasVa(waterBlasPool_.pages.size(), 0);
    for(const WaterGroup* g:waterGroupUses_) if(!blasVa[g->blasPage]) blasVa[g->blasPage]=waterBlasPool_.pages[g->blasPage].buffer.va();
    const D3D12_GPU_VIRTUAL_ADDRESS scratchVa=waterScratch_.va();
    uint32_t built=0;
    for(size_t k=0;k<waterGroupUses_.size();++k) {
        WaterGroup& g=*waterGroupUses_[k];
        // Its description is the group's own, made with it (water_group_make): a geometry a unit, each the unit's part of
        // the group's block and the level's pattern; the same at its build and at every refit.
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC desc{};
        desc.Inputs.Type=D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
        desc.Inputs.Flags=D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE|D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;
        desc.Inputs.NumDescs=UINT(g.geometry.size()); desc.Inputs.DescsLayout=D3D12_ELEMENTS_LAYOUT_ARRAY; desc.Inputs.pGeometryDescs=g.geometry.data();
        offset=(offset+255)/256*256;
        const D3D12_GPU_VIRTUAL_ADDRESS blas=blasVa[g.blasPage]+g.blasAt;
        desc.DestAccelerationStructureData=blas; desc.ScratchAccelerationStructureData=scratchVa+offset;
        if(g.built) { desc.Inputs.Flags|=D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE; desc.SourceAccelerationStructureData=blas; }
        list->BuildRaytracingAccelerationStructure(&desc, 0, nullptr);
        const uint64_t all=uint64_t(g.baseTriangles)<<(2*g.level);
        { auto& w=structureWork[kStructureWater]; if(g.built) ++w.refits; else ++w.builds; w.triangles+=all; }
        offset+=g.built ? g.updateBytes : g.scratchBytes;
        if(!g.built) { ++built; waterBuiltTriangles_+=all; }
        g.built=true;
        // An instance like any other, in the scene's own space; its triangles solid, their front the outward side.
        D3D12_RAYTRACING_INSTANCE_DESC d{};
        d.Transform[0][0]=d.Transform[1][1]=d.Transform[2][2]=1.f;
        d.InstanceID=uint32_t(records.size())&0xFFFFFF;
        d.InstanceMask=kMaskWaves;
        d.Flags=D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE|D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_OPAQUE;
        d.AccelerationStructure=blas;
        descs.push_back(d);
        records.push_back(InstanceRecordGpu{meshBase+uint32_t(k), materialBase, kInstanceWaves|(g.slot<<8), kInvalid});
    }
    barriers.uav(nullptr); barriers.flush(list);
    // FOR A CHECK (recordCheck_): a hit reads its material at the instance's base plus its geometry's number, so the
    // frame's material list must hold the water's material for every unit of the largest group traced (scene.cpp
    // waterMaterialBase). (No picture would show a shorter list today: the water's own path never asks its material.)
    if(recordCheck_) {
        size_t most=0;
        for(const WaterGroup* g:waterGroupUses_) most=std::max(most, g->members.size());
        bool listed=size_t(materialBase)+most<=combinedIndices_.size();
        for(size_t k=0;listed && k<most;++k) listed=combinedIndices_[materialBase+k]==uint32_t(materials_.size());
        if(!listed) log_throttled("PT record check: THE WATER'S GROUPS DIFFER from what would be made again: the frame's material list does not hold the water's material for every "
                                  "piece of a group ("+std::to_string(most)+" pieces in the largest, "+std::to_string(combinedIndices_.size()-std::min(combinedIndices_.size(), size_t(materialBase)))+
                                  " entries from the water's base on)");
    }
    // In the log when pieces were made (the camera came nearer, or the water came into reach), at most every 5 s: how much
    // of the surface is traced, and how finely.
    // (With the test's switch set: also in every frame that built a group or collapsed a piece, PT-99's check reads them.)
    if(waterMade_ || (waterCutsTest_ && (built || waterCollapsed_))) {
        static std::chrono::steady_clock::time_point told{};
        const auto now=std::chrono::steady_clock::now();
        if(waterCutsTest_ || now-told>std::chrono::seconds(5)) {
            told=now;
            // (PT-87: the pages by what they are and what the pieces' blocks take of them; "MB of pages" as before for
            // whoever reads it.)
            uint64_t vertexPages=0, blasPages=0;
            for(const auto& page:waterVertexPool_.pages) if(page.buffer) vertexPages+=kVertexPageBytes;
            for(const auto& page:waterBlasPool_.pages) if(page.buffer) blasPages+=kBlasPageBytes;
            char pools[320];
            std::snprintf(pools, sizeof pools, ": the vertices' %.0f MB with %.1f MB of blocks in use, the structures' %.0f MB with %.1f MB of blocks in use; %u pages given back "
                          "so far%s", vertexPages/1048576.0, waterVertexPool_.liveBytes/1048576.0, blasPages/1048576.0, waterBlasPool_.liveBytes/1048576.0, waterPagesBack_,
                          (waterCutsOff_&kMemory2Water) ? " [the water's cut is off (scene.hpp kMemory2Waiting): blocks of the next power of two and pages kept, as before]" : "");
            // (PT-99: and the groups: those traced, one refit or build each, of those held; this frame's builds.)
            log("The water's surface: "+std::to_string(waterUses_.size())+" pieces in "+std::to_string(waterGroupUses_.size())+" groups, "+std::to_string(triangles)+
                " triangles, "+std::to_string(vertices)+
                " vertices ("+std::to_string(waterMade_)+" pieces made this frame, "+std::to_string(waterUnits_.size())+" kept, "+
                std::to_string((vertexPages+blasPages)>>20)+" MB of pages"+pools+"; "+std::to_string(waterGroups_.size())+" groups held, "+std::to_string(built)+
                " built this frame ("+std::to_string(waterGroupsNew_)+" of new pieces, "+std::to_string(waterGroupsMerged_)+" put together of "+
                std::to_string(waterMergedFrom_)+"), "+std::to_string(waterBuiltTriangles_)+" triangles built, "+std::to_string(waterCollapsed_)+
                " pieces collapsed this frame, "+std::to_string(waterDead_)+" geometries of dropped pieces"+(waterUngrouped_ ? " [UNGROUPED: the test's switch]" : "")+")");
        }
    }
    // PT-99: every 10 s, what the groups cost: the calls a frame (a refit or a build a group traced; it was one a
    // piece), and what a walk made the frames build (new pieces' groups, groups put together, their triangles).
    {
        static std::chrono::steady_clock::time_point since{};
        static uint64_t frames=0, calls=0, pieces=0, newGroups=0, merges=0, mergedFrom=0, builtGroups=0, builtTriangles=0, collapsed=0, made=0, most=0;
        const auto now=std::chrono::steady_clock::now();
        if(!frames) since=now;
        ++frames; calls+=waterGroupUses_.size(); pieces+=waterUses_.size(); newGroups+=waterGroupsNew_; merges+=waterGroupsMerged_; mergedFrom+=waterMergedFrom_;
        builtGroups+=built; builtTriangles+=waterBuiltTriangles_; collapsed+=waterCollapsed_; made+=waterMade_; most=std::max<uint64_t>(most, waterGroupUses_.size());
        if(now-since>=std::chrono::seconds(10)) {
            char text[640];
            std::snprintf(text, sizeof text, "PT water groups 10s: %llu frames; a frame traced %.0f pieces in %.1f groups (at most %llu): %.1f refit or build calls a frame "
                          "where each piece had one; built in these frames: %llu groups (%llu of new pieces, %llu put together of %llu), %.1f a frame, %.0f triangles a frame; "
                          "%llu pieces made, %llu collapsed; now %zu groups held, %zu pieces kept%s",
                          (unsigned long long)frames, double(pieces)/double(frames), double(calls)/double(frames), (unsigned long long)most, double(calls)/double(frames),
                          (unsigned long long)builtGroups, (unsigned long long)newGroups, (unsigned long long)merges, (unsigned long long)mergedFrom,
                          double(builtGroups)/double(frames), double(builtTriangles)/double(frames), (unsigned long long)made, (unsigned long long)collapsed,
                          waterGroups_.size(), waterUnits_.size(), waterUngrouped_ ? " [UNGROUPED: the test's switch]" : "");
            log(text);
            frames=calls=pieces=newGroups=merges=mergedFrom=builtGroups=builtTriangles=collapsed=made=most=0;
        }
    }
    std::lock_guard<std::mutex> s(statsMutex_);
    stats_.waterUnits=uint32_t(waterUses_.size()); stats_.waterUnitVertices=vertices; stats_.waterUnitTriangles=triangles;
    stats_.waterUnitsMade=waterMade_; stats_.waterUnitsKept=uint32_t(waterUnits_.size());
    stats_.waterGroups=uint32_t(waterGroupUses_.size()); stats_.waterGroupsBuilt=built; stats_.waterGroupsMerged=waterGroupsMerged_;
    stats_.waterCollapsed=waterCollapsed_; stats_.waterDead=waterDead_; stats_.waterBuiltTriangles=waterBuiltTriangles_;
}

} // namespace tgpt
