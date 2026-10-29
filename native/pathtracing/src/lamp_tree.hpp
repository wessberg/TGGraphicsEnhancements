// SPDX-License-Identifier: MIT
#pragma once
// The lamps a reflection ray can run into, found by a search tree in place of a loop over every lamp (PT-99; on trial
// until 2026-10-10, docs/agents/perf-1.md "perf2" and rule-1.md; in every build since).
//
// pt_common.hlsli reflected_lamps asks, for a ray off water or see-through glass, which lamps' balls the ray runs into
// before it meets anything else. Until 2026-10-10 it asked EVERY lamp of the point's two light lists, which together
// are every lamp of the scene (the far list holds each lamp that falls off with distance alone): 500 to 1,000 records
// read and as many rays against a ball, at every turn of every water pixel's path and at every face of glass. This
// builds, once a frame, a tree of balls over the lamps (a lamp a leaf, two children a node, each node a ball round
// everything under it), laid out in the order a walk visits it, each node with the index to go on at when the ray
// passes its ball by. The shader walks it (reflected_lamps) and asks only the lamps whose ball the ray can meet.
//
// THE SAME ANSWER as that loop's, not a near one. A leaf's lamp is asked the very question the loop asked (the same
// lines), and it is counted only where the loop counted it (the cell's list or the far list holds it). What the tree
// must promise is that the walk reaches every lamp that question would say yes to. It is worked out in single precision, so
// far from a lamp it says yes to rays that pass it by a little (the rounding of b * b - (oc . oc - r * r) is some
// 2e-6 of the squared distance): a lamp it accepts lies within 2.02 r + (2.2 sqrt(2e-6 + e) + e) D of the ray's
// stretch, r its ball's radius, D its distance from the ray's start, e how far the direction's squared length is from
// 1 (the reckoning: docs/agents/perf-1.md, perf2.2). So a leaf's ball is kLeafBalls times the lamp's largest ball (its own
// radius, five times it, or HDRP's: the three sizes pt_common's lamp_trial_radius can give), a node's ball holds its
// leaves' balls, and the shader adds the share that grows with the distance, per ray, from the ray's own direction.
// tests/lamp_tree_test.cpp checks the promise against the loop (which it keeps, as what the walk is held against), lamp
// by lamp, over millions of rays (no GPU).
//
// Where it lives: after the frame's lights in their own buffer (scene.cpp update), as records of the lights' layout: the
// root is record number <light count>. (So it took no frame constant and no buffer while it was on trial. A buffer and
// a constant of its own would be tidier; not built: docs/agents/rule-1.md.)
//   position  the ball's centre (relative to the scene origin, as the lights')
//   range     the ball's radius
//   type      where the walk goes on when the ray passes this ball by: the node after everything under it
//             (the root's: the number of nodes; 0: the tree is empty)
//   id        a leaf's lamp (its index among the lights); kInner for a node with children
#include "tgpt_api.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace lamp_tree {
constexpr uint32_t kInner=0xFFFFFFFFu;
constexpr double kLeafBalls=2.1; // a leaf's ball over the lamp's largest: 2.02 by the reckoning, and room for rounding

// The lamps the loop can count at all (pt_common reflected_as_sphere), at a place the light lists take (scene.cpp
// build_light_grid: usable).
inline bool member(const TGPT_Light& l) {
    return l.type<=1u && l.radius>0.f && (l.flags&TGPT_LIGHT_NO_SPECULAR)==0u &&
           std::isfinite(l.position[0]) && std::isfinite(l.position[1]) && std::isfinite(l.position[2]);
}
// The largest ball pt_common's lamp_trial_radius can give the lamp, whatever the hidden trial says.
inline double largest_ball(const TGPT_Light& l) {
    return std::max(double(l.radius)*5.0, double(l.flags>>TGPT_LIGHT_HDRP_RADIUS_SHIFT)*0.001);
}
inline float round_up(double v) {
    float f=float(v);
    if(double(f)<v) f=std::nextafter(f, std::numeric_limits<float>::infinity());
    return f;
}

struct Leaf { uint32_t light; double position[3]; double reach; };

// The tree's nodes for these lights (positions relative to the scene origin, as they are uploaded), in the walk's order.
// Always at least one record: an empty tree is a root whose `type` is 0.
inline std::vector<TGPT_Light> build(const TGPT_Light* lights, size_t count) {
    std::vector<Leaf> leaves;
    leaves.reserve(count);
    for(size_t i=0;i<count;++i) {
        if(!member(lights[i])) continue;
        Leaf leaf{uint32_t(i), {double(lights[i].position[0]), double(lights[i].position[1]), double(lights[i].position[2])}, kLeafBalls*largest_ball(lights[i])};
        leaves.push_back(leaf);
    }
    std::vector<TGPT_Light> nodes;
    if(leaves.empty()) { nodes.emplace_back(); nodes.back().id=kInner; return nodes; }
    nodes.reserve(leaves.size()*2);
    // Halved at the middle lamp along the longest side of the lamps' box: never deeper than log2(lamps) + 1.
    auto emit=[&](auto&& self, size_t lo, size_t hi) -> void {
        const size_t at=nodes.size();
        nodes.emplace_back();
        if(hi-lo==1) {
            const Leaf& leaf=leaves[lo];
            TGPT_Light& n=nodes[at];
            for(int a=0;a<3;++a) n.position[a]=lights[leaf.light].position[a];
            n.range=round_up(leaf.reach*(1.0+1e-6));
            n.id=leaf.light; n.type=uint32_t(at+1);
            return;
        }
        double min[3]={leaves[lo].position[0], leaves[lo].position[1], leaves[lo].position[2]}, max[3]={min[0], min[1], min[2]};
        for(size_t k=lo+1;k<hi;++k) for(int a=0;a<3;++a) { min[a]=std::min(min[a], leaves[k].position[a]); max[a]=std::max(max[a], leaves[k].position[a]); }
        int axis=0;
        for(int a=1;a<3;++a) if(max[a]-min[a]>max[axis]-min[axis]) axis=a;
        const size_t mid=lo+(hi-lo)/2;
        std::nth_element(leaves.begin()+lo, leaves.begin()+mid, leaves.begin()+hi, [axis](const Leaf& x, const Leaf& y) { return x.position[axis]<y.position[axis]; });
        // The ball: about the middle of the lamps' box as a float (what the shader reads), out to the farthest leaf's ball.
        float centre[3];
        for(int a=0;a<3;++a) centre[a]=float(0.5*(min[a]+max[a]));
        double radius=0;
        for(size_t k=lo;k<hi;++k) {
            double d2=0;
            for(int a=0;a<3;++a) { const double d=leaves[k].position[a]-double(centre[a]); d2+=d*d; }
            radius=std::max(radius, std::sqrt(d2)+leaves[k].reach);
        }
        self(self, lo, mid);
        self(self, mid, hi);
        TGPT_Light& n=nodes[at]; // (after the children: the vector may have moved)
        for(int a=0;a<3;++a) n.position[a]=centre[a];
        n.range=round_up(radius*(1.0+1e-6));
        n.id=kInner; n.type=uint32_t(nodes.size());
    };
    emit(emit, 0, leaves.size());
    return nodes;
}
}
