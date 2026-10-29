// SPDX-License-Identifier: MIT
// The lamps' search tree (native/pathtracing/src/lamp_tree.hpp; PT-99, docs/agents/perf-1.md "perf2") against the loop
// over every lamp that pt_common.hlsli reflected_lamps was until 2026-10-10 (docs/agents/rule-1.md: the tree is the rule
// since, and the loop lives on here alone, as what the walk is held against). No GPU.
//
// What it proves: for every ray, the walk through the tree (walk, a line-for-line copy of the shader's
// reflected_lamps) reaches every lamp that the loop's own question (asks_yes, a line-for-line copy of the loop's
// body) says yes to, and the lamps counted are the same lamps (counted_by_loop against counted_by_tree, with the two
// light lists as scene.cpp build_light_grid makes them: a cell's list, the far list, a lamp on both counted once).
// The question is single-precision arithmetic, and a GPU may round a sum of products another way than this CPU
// (fused multiply-add): so the loop's question is asked both ways and the walk is walked both ways, and every
// yes of either question must be reached by either walk.
// The rays are the hard ones: aimed at lamps and just past them (a grazing ray is where rounding decides), from a
// centimetre to kilometres away, from inside a lamp's ball, with stretches that end just before and just after the
// lamp, and with directions whose length is a little off 1.
#include "../native/pathtracing/src/lamp_tree.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <vector>

// For breaking the walk on purpose (a check of this check; the build never sets them): the walk's share that grows with
// the distance (the shader's 2.3), and its balls scaled (1: as built). With the share at 0.5 or the balls at 0.4 the
// test must fail.
#ifndef LAMP_TREE_TEST_SLACK
#define LAMP_TREE_TEST_SLACK 2.3f
#endif
#ifndef LAMP_TREE_TEST_BALLS
#define LAMP_TREE_TEST_BALLS 1.0f
#endif
// (And with LAMP_TREE_TEST_UNORDERED defined the walk adds its lamps as it meets them: the sums' bits must then differ
// for some ray through three lamps or more.)

namespace {
int failures=0;
void expect(bool ok, const std::string& what) {
    if(ok) return;
    if(++failures<=20) std::printf("FAIL %s\n", what.c_str());
}

struct Rng {
    uint64_t s;
    explicit Rng(uint64_t seed) : s(seed*0x9E3779B97F4A7C15ull+0x1234567ull) {}
    uint64_t next() { s^=s<<13; s^=s>>7; s^=s<<17; return s; }
    double unit() { return double(next()>>11)*(1.0/9007199254740992.0); }
    double range(double lo, double hi) { return lo+(hi-lo)*unit(); }
    uint32_t below(uint32_t n) { return uint32_t(next()%n); }
};

// A sum of three products, the plain way and the way a GPU may fuse it.
template<bool Fused> float dot3(const float a[3], const float b[3]) {
    if(Fused) return std::fma(a[2], b[2], std::fma(a[1], b[1], a[0]*b[0]));
    return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];
}

// pt_common.hlsli lamp_trial_radius (the hidden trial B: 0 the lamp's own ball, 1 HDRP's, 2 five times its own).
float trial_radius(const TGPT_Light& l, unsigned mode) {
    if(mode==0u || l.type>1u) return l.radius;
    if(mode>=2u) return l.radius*5.0f;
    const float hdrp=float(l.flags>>TGPT_LIGHT_HDRP_RADIUS_SHIFT)*0.001f;
    return hdrp>0.f ? hdrp : l.radius;
}
// The loop's question of one lamp (the lines the walk still asks a leaf's lamp with: pt_common.hlsli reflected_lamps).
template<bool Fused> bool asks_yes(const TGPT_Light& l, const float origin[3], const float dir[3], float tmax, unsigned mode) {
    if(!(l.type<=1u && l.radius>0.f && (l.flags&TGPT_LIGHT_NO_SPECULAR)==0u)) return false; // reflected_as_sphere
    const float ball=trial_radius(l, mode);
    const float oc[3]={origin[0]-l.position[0], origin[1]-l.position[1], origin[2]-l.position[2]};
    const float b=dot3<Fused>(oc, dir);
    const float disc=Fused ? std::fma(b, b, -(dot3<Fused>(oc, oc)-ball*ball)) : b*b-(dot3<Fused>(oc, oc)-ball*ball);
    if(disc<=0.f) return false;
    const float t=-b-std::sqrt(disc)>0.f ? -b-std::sqrt(disc) : -b+std::sqrt(disc);
    if(t<=0.f || t>=tmax) return false;
    return true;
}
// The shader's walk (pt_common.hlsli reflected_lamps): the leaves it reaches, in its order. `records`: the frame's
// lights and after them the tree, as scene.cpp uploads them; `root`: the number of lights.
template<bool Fused> void walk(const std::vector<TGPT_Light>& records, uint32_t root, const float origin[3], const float dir[3], float tmax,
                               std::vector<uint32_t>& reached, uint64_t& asked) {
    const uint32_t count=records[root].type;
    const float length2=dot3<Fused>(dir, dir);
    const float off=std::fabs(length2-1.0f);
    const float slack=LAMP_TREE_TEST_SLACK*std::sqrt(2.0e-6f+off)+off;
    const float perLength=1.0f/length2;
    for(uint32_t i=0;i<count;) {
        const TGPT_Light& node=records[root+i];
        ++asked;
        const float w[3]={node.position[0]-origin[0], node.position[1]-origin[1], node.position[2]-origin[2]};
        const float s=std::min(std::max(dot3<Fused>(w, dir)*perLength, 0.0f), tmax);
        const float q[3]={w[0]-s*dir[0], w[1]-s*dir[1], w[2]-s*dir[2]};
        const float range=node.range*LAMP_TREE_TEST_BALLS;
        const float reach=range+slack*(std::sqrt(dot3<Fused>(w, w))+range);
        if(dot3<Fused>(q, q)>reach*reach) { i=std::max(node.type, i+1); continue; }
        ++i;
        if(node.id==lamp_tree::kInner) continue;
        reached.push_back(node.id);
    }
}

// The two light lists of a point as scene.cpp build_light_grid makes them, for the counting below.
struct Lists {
    bool inGrid=true;
    std::vector<uint32_t> cell; // the cell's lights, in the order of their indices (cell_lists searches them)
    std::vector<uint32_t> far;  // inside the grid: every usable lamp that falls off with distance alone; outside: every usable one
};
bool usable(const TGPT_Light& l) { return l.type<=1u && std::isfinite(l.position[0]) && std::isfinite(l.position[1]) && std::isfinite(l.position[2]); }
Lists make_lists(const std::vector<TGPT_Light>& lights, bool inGrid, Rng& rng) {
    Lists s; s.inGrid=inGrid;
    for(uint32_t i=0;i<lights.size();++i) {
        const TGPT_Light& l=lights[i];
        // A cell lists a light whose range reaches it (type 0 or 1, a range, a finite place): here, by chance.
        if(inGrid && usable(l) && l.range>0.f && std::isfinite(l.range) && rng.unit()<0.3) s.cell.push_back(i);
        if(usable(l) && (!inGrid || (l.flags&TGPT_LIGHT_NO_RANGE)!=0u)) s.far.push_back(i);
    }
    return s;
}
// The lamps the loop counted: the cell's list, then the far list less what the cell lists (the loop as it was).
template<bool Fused> std::vector<uint32_t> counted_by_loop(const std::vector<TGPT_Light>& lights, const Lists& s, const float origin[3], const float dir[3], float tmax, unsigned mode) {
    std::vector<uint32_t> counted;
    for(size_t i=0;i<s.cell.size()+s.far.size();++i) {
        const bool far=i>=s.cell.size();
        const uint32_t index=far ? s.far[i-s.cell.size()] : s.cell[i];
        if(!asks_yes<Fused>(lights[index], origin, dir, tmax, mode)) continue;
        if(far && std::binary_search(s.cell.begin(), s.cell.end(), index)) continue;
        counted.push_back(index);
    }
    return counted;
}
// And the lamps the walk counts: a leaf's lamp that says yes, where the far list or the cell's list holds it.
template<bool Fused> std::vector<uint32_t> counted_by_tree(const std::vector<TGPT_Light>& records, uint32_t root, const Lists& s, const float origin[3], const float dir[3], float tmax, unsigned mode, uint64_t& asked) {
    std::vector<uint32_t> reached, counted;
    walk<Fused>(records, root, origin, dir, tmax, reached, asked);
    for(uint32_t index:reached) {
        const TGPT_Light& l=records[index];
        if(!asks_yes<Fused>(l, origin, dir, tmax, mode)) continue;
        if((l.flags&TGPT_LIGHT_NO_RANGE)==0u && s.inGrid && !std::binary_search(s.cell.begin(), s.cell.end(), index)) continue;
        counted.push_back(index);
    }
    return counted;
}

// The sums. What a lamp adds, here: a number of its own, of any size (the light's real term is worked out by the same lines
// in the loop and in the walk; what must be shown is that the walk adds the terms in the loop's order).
float term_of(uint32_t index) { return float(1u+((index*2654435761u)>>9)%1000003u)*std::pow(10.0f, float(int(index%9u))-4.0f); }
// The loop adds them as it meets them: the cell's lamps by their index, then the far list's that the cell does not hold.
float sum_by_loop(const std::vector<uint32_t>& counted) {
    float sum=0.f;
    for(uint32_t index:counted) sum+=term_of(index);
    return sum;
}
// The walk (reflected_lamps): up to kHeld lamps held in the loop's order until it ends; a fifth at once added as met.
constexpr uint32_t kHeld=4;
float sum_by_tree(const std::vector<uint32_t>& counted, const Lists& s) {
    float held[kHeld]; uint32_t keys[kHeld];
    for(uint32_t z=0;z<kHeld;++z) { held[z]=0.f; keys[z]=lamp_tree::kInner; }
    float sum=0.f;
    for(uint32_t index:counted) {
        const bool listed=s.inGrid && std::binary_search(s.cell.begin(), s.cell.end(), index);
        float term=term_of(index);
#ifdef LAMP_TREE_TEST_UNORDERED
        sum+=term; (void)listed; (void)keys;
#else
        uint32_t key=(listed ? 0u : 0x80000000u)|index;
        for(uint32_t z=0;z<kHeld;++z)
            if(key<keys[z]) { const float heldTerm=held[z]; const uint32_t heldKey=keys[z]; held[z]=term; keys[z]=key; term=heldTerm; key=heldKey; }
        if(key!=lamp_tree::kInner) sum+=term;
#endif
    }
    for(uint32_t z=0;z<kHeld;++z) if(keys[z]!=lamp_tree::kInner) sum+=held[z];
    return sum;
}

struct World { const char* name; uint32_t lamps; double extent[3]; double centre[3]; double radiusLo, radiusHi; };

void normalize(float v[3]) {
    const float length=std::sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);
    for(int a=0;a<3;++a) v[a]/=length;
}

void run_world(const World& world, uint64_t seed, uint32_t rays, uint64_t& totalYes, uint64_t& totalGrazing, uint64_t& totalSeveral, uint64_t& totalOrdered) {
    Rng rng(seed);
    std::vector<TGPT_Light> lights(world.lamps);
    for(uint32_t i=0;i<world.lamps;++i) {
        TGPT_Light& l=lights[i];
        for(int a=0;a<3;++a) l.position[a]=float(world.centre[a]+rng.range(-0.5, 0.5)*world.extent[a]);
        const double kind=rng.unit();
        l.type=kind<0.80 ? 0u : (kind<0.96 ? 1u : 2u); // a few directional ones: never in the tree
        l.radius=float(rng.range(world.radiusLo, world.radiusHi));
        l.range=float(rng.range(1.0, 40.0));
        l.flags=rng.unit()<0.85 ? TGPT_LIGHT_NO_RANGE : 0u;
        if(rng.unit()<0.05) l.flags|=TGPT_LIGHT_NO_SPECULAR;                                         // never counted
        if(rng.unit()<0.30) l.flags|=uint32_t(rng.range(20.0, 1500.0))<<TGPT_LIGHT_HDRP_RADIUS_SHIFT; // HDRP's radius, mm
        if(rng.unit()<0.02) l.radius=0.f;                                                             // never counted
        if(rng.unit()<0.01) l.position[rng.below(3)]=std::numeric_limits<float>::quiet_NaN();         // no list takes it
        if(rng.unit()<0.03 && i>0) for(int a=0;a<3;++a) l.position[a]=lights[i-1].position[a];        // two lamps at one place
        l.id=i+1;
    }
    std::vector<TGPT_Light> records=lights;
    const std::vector<TGPT_Light> tree=lamp_tree::build(lights.data(), lights.size());
    records.insert(records.end(), tree.begin(), tree.end());
    const uint32_t root=uint32_t(lights.size());

    // The tree's own shape: every lamp the loop can count is exactly one leaf, each node's ball holds what is under it,
    // and the walk's "go on at" always goes forward and ends at the node count.
    const uint32_t count=tree[0].type;
    uint32_t members=0;
    for(const auto& l:lights) members+=lamp_tree::member(l) ? 1u : 0u;
    expect(members==0 ? count==0 : count==2*members-1, std::string(world.name)+": "+std::to_string(members)+" lamps make "+std::to_string(count)+" nodes");
    expect(tree.size()==std::max<size_t>(count, 1), std::string(world.name)+": the tree's records");
    std::vector<uint32_t> seen(lights.size(), 0);
    for(uint32_t i=0;i<count;++i) {
        const TGPT_Light& n=tree[i];
        expect(n.type>i && n.type<=count, std::string(world.name)+": node "+std::to_string(i)+" goes on at "+std::to_string(n.type));
        if(n.id!=lamp_tree::kInner) {
            expect(n.id<lights.size() && lamp_tree::member(lights[n.id]) && n.type==i+1, std::string(world.name)+": leaf "+std::to_string(i));
            if(n.id<lights.size()) ++seen[n.id];
        }
        // Every leaf under the node (the nodes up to where it goes on) lies inside its ball.
        for(uint32_t k=i+1;k<n.type && n.id==lamp_tree::kInner;++k) {
            const TGPT_Light& under=tree[k];
            if(under.id==lamp_tree::kInner) continue;
            double d2=0;
            for(int a=0;a<3;++a) { const double d=double(under.position[a])-double(n.position[a]); d2+=d*d; }
            expect(std::sqrt(d2)+lamp_tree::kLeafBalls*lamp_tree::largest_ball(lights[under.id])<=double(n.range), std::string(world.name)+": node "+std::to_string(i)+" holds leaf "+std::to_string(k));
        }
    }
    for(uint32_t i=0;i<lights.size();++i) expect(seen[i]==(lamp_tree::member(lights[i]) ? 1u : 0u), std::string(world.name)+": lamp "+std::to_string(i)+" is "+std::to_string(seen[i])+" leaves");
    if(members==0) return;

    std::vector<uint32_t> aimable;
    for(uint32_t i=0;i<lights.size();++i) if(lamp_tree::member(lights[i])) aimable.push_back(i);
    uint64_t yes=0, grazing=0, several=0, asked=0, walks=0;
    static const double offLength[]={0, 0, 0, 0, 1e-7, -1e-7, 1e-6, -1e-6, 1e-5, -1e-5, 1e-4, -1e-4, 1e-3, -1e-3};
    for(uint32_t r=0;r<rays;++r) {
        const unsigned mode=rng.below(3);
        const TGPT_Light& aim=lights[aimable[rng.below(uint32_t(aimable.size()))]];
        const float ball=trial_radius(aim, mode);
        // Where the ray starts: anywhere in the world, close by the lamp, or inside its ball.
        float origin[3], dir[3];
        const double from=rng.unit();
        double distance=0;
        for(int a=0;a<3;++a) {
            // (One random number a statement, wherever two are needed: the order two calls in one expression run in is the
            // compiler's to choose, and the rays, and so the counts printed below, would change from build to build.)
            const double side=rng.range(-1.0, 1.0), away10=std::pow(10.0, rng.range(-2.0, 1.5));
            const double p=from<0.5 ? world.centre[a]+side*0.6*world.extent[a]
                         : from<0.9 ? double(aim.position[a])+side*away10
                                    : double(aim.position[a])+side*0.6*double(ball);
            origin[a]=float(p);
            distance+=(double(aim.position[a])-p)*(double(aim.position[a])-p);
        }
        distance=std::sqrt(distance);
        // Where it aims: at the lamp, or past it by up to three times the ball and the rounding's reach at this distance
        // (most rays graze); one ray in ten anywhere.
        const double miss=rng.unit()<0.1 ? 0.0 : rng.range(0.0, 3.0)*(double(ball)+2.5e-3*distance);
        double away[3]; double norm=0;
        for(int a=0;a<3;++a) { away[a]=rng.range(-1.0, 1.0); norm+=away[a]*away[a]; }
        norm=std::sqrt(std::max(norm, 1e-12));
        const bool anywhere=rng.unit()<0.1;
        for(int a=0;a<3;++a) dir[a]=anywhere ? float(rng.range(-1.0, 1.0)) : float(double(aim.position[a])+away[a]/norm*miss-double(origin[a]));
        if(!(dir[0]*dir[0]+dir[1]*dir[1]+dir[2]*dir[2]>0.f)) dir[0]=1.f;
        normalize(dir);
        const float stretch=1.0f+float(offLength[rng.below(uint32_t(sizeof offLength/sizeof offLength[0]))]);
        for(int a=0;a<3;++a) dir[a]*=stretch;
        // How far the ray goes: for ever, or to about where the lamp is.
        const double ends=rng.unit();
        const double nearly=rng.range(0.999, 1.001), balls=rng.range(-2.0, 2.0), anyShare=rng.range(0.2, 3.0);
        const float tmax=ends<0.4 ? 1e30f : ends<0.7 ? float(distance*nearly+balls*double(ball)) : float(distance*anyShare);
        const Lists lists=make_lists(lights, rng.unit()<0.85, rng);

        std::vector<uint32_t> reached[2];
        uint64_t unused=0;
        walk<false>(records, root, origin, dir, tmax, reached[0], asked); ++walks;
        walk<true>(records, root, origin, dir, tmax, reached[1], unused);
        std::set<uint32_t> reachedSet[2]={std::set<uint32_t>(reached[0].begin(), reached[0].end()), std::set<uint32_t>(reached[1].begin(), reached[1].end())};
        uint32_t met=0;
        for(uint32_t i:aimable) {
            const bool plain=asks_yes<false>(lights[i], origin, dir, tmax, mode), fused=asks_yes<true>(lights[i], origin, dir, tmax, mode);
            if(!plain && !fused) continue;
            ++yes; ++met;
            // (A yes for a ray that passes the ball by, as exact arithmetic has it: the rounding's own.)
            double oc[3], b=0, c=0, d2=0;
            for(int a=0;a<3;++a) { oc[a]=double(origin[a])-double(lights[i].position[a]); b+=oc[a]*double(dir[a]); c+=oc[a]*oc[a]; d2+=double(dir[a])*double(dir[a]); }
            if(c-b*b/d2>double(trial_radius(lights[i], mode))*double(trial_radius(lights[i], mode))) ++grazing;
            for(int v=0;v<2;++v)
                if(!reachedSet[v].count(i)) {
                    char text[320];
                    std::snprintf(text, sizeof text, "%s: ray %u: lamp %u says yes (plain %d, fused %d) and the %s walk does not reach it (distance %.3f m, ball %.4f m, the direction's length %.7f, tmax %g)",
                                  world.name, r, i, int(plain), int(fused), v ? "fused" : "plain", distance, double(trial_radius(lights[i], mode)), double(stretch), double(tmax));
                    expect(false, text);
                }
        }
        if(met>=3) ++several;
        // The lamps counted, list by list, the loop's way and the tree's: the same lamps (each once).
        for(int v=0;v<2;++v) {
            std::vector<uint32_t> loop=v ? counted_by_loop<true>(lights, lists, origin, dir, tmax, mode) : counted_by_loop<false>(lights, lists, origin, dir, tmax, mode);
            std::vector<uint32_t> byTree=v ? counted_by_tree<true>(records, root, lists, origin, dir, tmax, mode, unused) : counted_by_tree<false>(records, root, lists, origin, dir, tmax, mode, unused);
            // The sums' own bits: the same for a ray through up to kHeld lamps at once (the walk meets them in another order).
            if(loop.size()<=kHeld && loop.size()==byTree.size()) {
                const float a=sum_by_loop(loop), b=sum_by_tree(byTree, lists);
                if(loop.size()>=3 && v==0) ++totalOrdered;
                expect(std::memcmp(&a, &b, sizeof a)==0, std::string(world.name)+": ray "+std::to_string(r)+": "+std::to_string(loop.size())+" lamps sum to other bits in the walk than in the loop");
            }
            std::sort(loop.begin(), loop.end()); std::sort(byTree.begin(), byTree.end());
            expect(std::adjacent_find(loop.begin(), loop.end())==loop.end(), std::string(world.name)+": the loop counts a lamp twice");
            expect(loop==byTree, std::string(world.name)+": ray "+std::to_string(r)+": the loop counts "+std::to_string(loop.size())+" lamps, the tree "+std::to_string(byTree.size()));
        }
    }
    std::printf("%-28s %5u lamps (%u in the tree, %u nodes): %u rays, %llu times a lamp said yes (%llu of them by rounding alone), %llu rays through 3 lamps or more; "
                "a walk reads %.1f nodes on average, the loop every lamp\n",
                world.name, world.lamps, members, count, rays, static_cast<unsigned long long>(yes), static_cast<unsigned long long>(grazing),
                static_cast<unsigned long long>(several), double(asked)/double(std::max<uint64_t>(walks, 1)));
    totalYes+=yes; totalGrazing+=grazing; totalSeveral+=several;
}

// Not a check: how many nodes a walk reads for rays as a frame has them (off water lying beside or under the lamps, in any
// upward direction, and rays in any direction from among the lamps), with lamps of the sizes the game's have (flames of
// a centimetre or two, a few hearths). The loop reads every lamp for each of these rays.
void report_ordinary(const World& world, uint64_t seed, double waterAway) {
    Rng rng(seed);
    std::vector<TGPT_Light> lights(world.lamps);
    for(uint32_t i=0;i<world.lamps;++i) {
        TGPT_Light& l=lights[i];
        for(int a=0;a<3;++a) l.position[a]=float(world.centre[a]+rng.range(-0.5, 0.5)*world.extent[a]);
        const double size=rng.unit();
        l.type=0u; l.flags=TGPT_LIGHT_NO_RANGE; l.range=20.f; l.id=i+1;
        l.radius=float(size<0.80 ? rng.range(0.005, 0.03) : (size<0.95 ? rng.range(0.03, 0.10) : rng.range(0.10, 0.40)));
    }
    std::vector<TGPT_Light> records=lights;
    const std::vector<TGPT_Light> tree=lamp_tree::build(lights.data(), lights.size());
    records.insert(records.end(), tree.begin(), tree.end());
    const uint32_t root=uint32_t(lights.size());
    // (What making the tree costs the tracer's thread each frame: the least of 200 makings.)
    double least=1e30;
    for(int k=0;k<200;++k) {
        const auto began=std::chrono::steady_clock::now();
        const std::vector<TGPT_Light> again=lamp_tree::build(lights.data(), lights.size());
        least=std::min(least, std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now()-began).count()+(again.empty() ? 1e30 : 0.0));
    }
    const uint32_t rays=40000;
    uint64_t askedWater=0, askedAmong=0, most=0, lampsAsked=0;
    for(uint32_t r=0;r<rays;++r) {
        const bool water=r%2==0;
        float origin[3], dir[3];
        if(water) {
            // On a level sheet under the lamps' box, beside it by up to waterAway metres.
            const double inX=rng.range(-0.5, 0.5), besideX=rng.range(-1.0, 1.0), under=rng.range(0.5, 3.0), inZ=rng.range(-0.5, 0.5), besideZ=rng.range(-1.0, 1.0);
            origin[0]=float(world.centre[0]+inX*world.extent[0]+besideX*waterAway);
            origin[1]=float(world.centre[1]-0.5*world.extent[1]-under);
            origin[2]=float(world.centre[2]+inZ*world.extent[2]+besideZ*waterAway);
        } else for(int a=0;a<3;++a) origin[a]=float(world.centre[a]+rng.range(-0.5, 0.5)*world.extent[a]);
        const double z=water ? rng.unit() : rng.range(-1.0, 1.0), phi=rng.range(0.0, 6.283185307179586), flat=std::sqrt(std::max(0.0, 1.0-z*z));
        dir[0]=float(flat*std::cos(phi)); dir[1]=float(z); dir[2]=float(flat*std::sin(phi));
        const float tmax=rng.unit()<0.5 ? 1e30f : float(rng.range(0.5, 150.0));
        std::vector<uint32_t> reached;
        uint64_t asked=0;
        walk<false>(records, root, origin, dir, tmax, reached, asked);
        (water ? askedWater : askedAmong)+=asked; most=std::max(most, asked); lampsAsked+=reached.size();
    }
    std::printf("ordinary rays, %-22s %4u lamps: a ray off water beside it reads %.1f nodes, a ray from among the lamps %.1f (at most %llu); %.2f lamps asked a ray; the loop: %u; the tree is made in %.0f microseconds\n",
                world.name, world.lamps, double(askedWater)/(rays/2), double(askedAmong)/(rays/2), static_cast<unsigned long long>(most), double(lampsAsked)/rays, world.lamps, least);
}
}

int main(int argc, char** argv) {
    const uint32_t scale=argc>1 ? uint32_t(std::max(1, std::atoi(argv[1]))) : 1u;
    // Flames a centimetre across and hearths half a metre; a room, a town on its plane, lamps scattered over kilometres
    // (far from the scene origin: single precision is coarsest there), a chandelier's cluster, one lamp, none.
    const World worlds[]={
        {"a room", 40, {12, 4, 9}, {3, 1.5, -2}, 0.005, 0.08},
        {"a tavern", 514, {40, 9, 30}, {-20, 3, 14}, 0.005, 0.25},
        {"a town on its plane", 976, {420, 14, 380}, {150, 6, -220}, 0.005, 0.5},
        {"kilometres, far out", 1500, {5000, 300, 5000}, {-2100, 40, -3700}, 0.005, 0.5},
        {"a cluster of candles", 64, {0.6, 0.3, 0.6}, {8, 2.6, 4}, 0.004, 0.02},
        {"one lamp", 1, {1, 1, 1}, {5, 1, 5}, 0.01, 0.02},
        {"two lamps", 2, {30, 2, 30}, {0, 1, 0}, 0.01, 0.3},
        {"large lamps", 200, {60, 10, 60}, {0, 5, 0}, 0.5, 6.0},
    };
    uint64_t yes=0, grazing=0, several=0, ordered=0;
    uint64_t seed=1;
    for(const World& w:worlds) run_world(w, seed++, (w.lamps>1000 ? 6000u : 12000u)*scale, yes, grazing, several, ordered);
    // No lamp at all: one record, a root that says "no nodes".
    {
        const std::vector<TGPT_Light> none=lamp_tree::build(nullptr, 0);
        expect(none.size()==1 && none[0].type==0u, "an empty tree is one record with no nodes");
        TGPT_Light sun{}; sun.type=2u; sun.radius=1.f;
        const std::vector<TGPT_Light> onlySun=lamp_tree::build(&sun, 1);
        expect(onlySun.size()==1 && onlySun[0].type==0u, "a tree of a sun alone is empty");
    }
    report_ordinary(worlds[1], 101, 5.0);
    report_ordinary(worlds[2], 102, 60.0);
    report_ordinary(worlds[2], 103, 600.0);
    // The test has teeth only if lamps did say yes, and by rounding alone too.
    expect(yes>50000ull*scale, "lamps said yes "+std::to_string(yes)+" times: too few for the check to mean anything");
    expect(grazing>500ull*scale, "only "+std::to_string(grazing)+" yeses by rounding alone: too few");
    expect(several>20ull*scale, "only "+std::to_string(several)+" rays through three lamps or more");
    expect(ordered>500ull*scale, "only "+std::to_string(ordered)+" rays whose three or four lamps' sums were compared bit for bit");
    std::printf("%llu rays through three or four counted lamps: their sums' bits compared\n", static_cast<unsigned long long>(ordered));
    std::printf("%s: %llu yeses checked, %llu by rounding alone, %d failures\n", failures ? "FAILED" : "OK", static_cast<unsigned long long>(yes), static_cast<unsigned long long>(grazing), failures);
    return failures ? 1 : 0;
}
