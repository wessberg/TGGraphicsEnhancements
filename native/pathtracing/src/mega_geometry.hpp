// SPDX-License-Identifier: MIT
// PT-109: NVIDIA RTX Mega Geometry for the tracer's acceleration structures: cluster-level acceleration structures
// (a mesh cut into clusters of at most 256 triangles, each built once or stamped from a template, and a bottom
// level built from clusters) and a partitioned top level (only the partitions whose instances changed are
// built again). On Direct3D 12 both come through NVAPI (nvapi.h, MIT), which the installed NVIDIA driver answers.
//
// Compiled in only with CMake's TGPT_MEGA_GEOMETRY (OFF by default: a build without it holds no line of this and
// links no NVAPI). With it, the plain path stays the default and serves whenever the driver does not report a
// feature or a call fails: capabilities come from what the runtime reports, never from a table of cards.
#pragma once
#include "gpu.hpp"
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace tgpt {
struct MegaCaps {
    bool asked=false;          // NVAPI started and answered both questions
    bool clusters=false;       // cluster operations: CLAS, templates, bottom levels from clusters
    bool partitionedTop=false; // the partitioned top level
    bool omm=false;            // NVAPI's own opacity micromap arrays (the only ones its cluster calls document: megageo-5)
    uint32_t driver=0;         // the driver's version times 100 (58142 is 581.42); 0 when it did not say
};
// Asks the driver what this device offers and what the tracer's own counts would cost with it (the driver's own
// sizes for a town's plants as clusters and for a top level of its instances in partitions). Never throws; each
// line is for the log. On a card that is not NVIDIA's NVAPI is not even started.
MegaCaps mega_probe(ID3D12Device* device, uint32_t vendor, std::vector<std::string>& lines);
// Where the driver reports clusters: every pipeline made on the device from now on is made with the driver's support for
// bottom levels built from clusters (NVAPI's pipeline creation flag; NVIDIA: tracing into one with a pipeline made without
// it is undefined, and the support may cost a little on every trace: so a build with the trial compiled in is compared
// with itself). Called before the tracer's passes are made. mega_cluster_pipelines(): whether it was set (the pass cache
// keeps such pipelines apart).
void mega_enable_cluster_pipelines(ID3D12Device* device, const MegaCaps& caps, std::vector<std::string>& lines);
bool mega_cluster_pipelines();
// And (megageo-5) whether they were made with its support for NVAPI's opacity micromaps too, where it reports those: a cluster that
// carries micromaps is traced only through such pipelines (NVAPI: "If the pipeline is not created with the correct primitive
// support, and that primitive type is encountered during traversal, behavior is undefined").
bool mega_cluster_micromaps();

// The first piece, ON TRIAL (the plugin's hidden "Advanced / Mega Geometry trial" 1, 2 or 3; TGPT_Frame.flags,
// TGPT_FRAME_MEGA_SHIFT): the frame's top level as a PARTITIONED one. The plain top level is built anew over every
// instance each frame. This one is kept from frame to frame, and each frame the driver is told only which instances
// changed; it then builds again only the partitions those are in.
//
// THE SAME INSTANCES, THE SAME HITS: a slot of the partitioned top level is the instance of the same number in the
// frame's list (GpuScene::record's `descs`), written with the same transform, InstanceID, mask, flags and bottom
// level. An inline ray query takes it as it takes a plain one (the driver's documents: bound and traced alike), and
// reports the same InstanceID, geometry and primitive, so no shader knows the difference.
//
// The partitions: cells over 2048 m around the scene's origin (which itself follows the camera in 256 m steps: the
// cells stand still in the world between two steps), for what stands still: 1024 cells of 64 m with the trial at 1,
// 256 of 128 m at 2, 64 of 256 m at 3 (NVIDIA advises 100 to 1000 instances a partition; which size suits this game's
// world, and whether rays are slower through any of them than through the plain top level, is the game's to say:
// larger cells are fewer and fuller, and more is built again when one thing in a cell changes). And the driver's
// "global" partition for
// what does not: everything the scene poses, refits or moves each frame (the things the game moves, the groups of
// plants, the water's pieces, the characters, swaying cloth). Those are told to the driver every frame (their bottom
// levels are refitted in place: the top level must take their new bounds), the rest only when its slot's instance
// changed. Nothing here is a limit on the picture: the room grows with the scene (the structure is then written
// whole once), and a partition may hold every instance.
//
// What a partitioned top level cannot say is an instance's "no micromaps" flag (DISABLE_OMMS: its instance flags are
// the four of cull, winding, opaque and non-opaque). So while this serves, GpuScene::record gives such an instance
// the mesh's plain bottom level (which holds no micromaps) in place of the micromap one with the flag: the same
// triangles, tested the same way.
class MegaTopLevel {
public:
    // What one frame's build did, for the log's "PT structures" line.
    struct Frame {
        uint32_t instances=0, written=0, updated=0; // of the frame's instances: written whole, told to take new bounds
        uint64_t bytes=0;                           // the structure's room
        bool whole=false;                           // made anew (first use, or the room grew): every instance written
    };
    // Records this frame's top level on the list and returns its address, or 0 when the plain build must serve this
    // frame (nothing was recorded then). `still`: the instances before this index stand still unless their bottom
    // level is in `restless` (sorted addresses: swaying cloth's copies); the ones from it on are posed, refitted or
    // moved every frame. `fresh` (sorted addresses): bottom levels built at their address since the last call that
    // returned an address; their instances are written again whatever their slots hold (a new structure may stand
    // where a retired one stood, with other bounds). `grid`: the cells a side of the 2048 m the partitions cover (32:
    // cells of 64 m, 16: 128 m, 8: 256 m; the trial's three sizes); another than the structure has makes it anew.
    // Never throws. A failure is said once in `lines` and is for good: the plain path serves from then on.
    D3D12_GPU_VIRTUAL_ADDRESS build(ID3D12Device5* device, ID3D12GraphicsCommandList4* list, UploadRing& ring, DeferredRelease& release, uint64_t retireValue,
                                    const D3D12_RAYTRACING_INSTANCE_DESC* descs, size_t count, size_t still, const std::vector<D3D12_GPU_VIRTUAL_ADDRESS>& restless,
                                    const std::vector<D3D12_GPU_VIRTUAL_ADDRESS>& fresh, uint32_t grid, Barriers& barriers, Frame& frame, std::vector<std::string>& lines);
    bool failed() const { return failed_; }
    // The scene was cleared, or the trial was switched off: the structure goes (the next use writes it whole).
    void release(DeferredRelease& release, uint64_t retireValue);
    uint64_t bytes() const { return allocation_bytes(tlas_.resource.Get()); }
    uint64_t scratch_bytes() const { return allocation_bytes(scratch_.resource.Get()); }
private:
    struct Slot { D3D12_RAYTRACING_INSTANCE_DESC desc{}; uint32_t partition=0, live=0; };
    void fail(const std::string& what, DeferredRelease& release, uint64_t retireValue, std::vector<std::string>& lines);
    Buffer tlas_, scratch_;
    std::vector<Slot> slots_;      // what the structure holds, slot by slot
    uint32_t capacity_=0;          // its instance count (its room): fixed while the structure lives
    uint32_t grid_=0;              // its partitions: grid_ x grid_ cells over 2048 m, fixed while it lives
    bool built_=false, failed_=false, saidFlags_=false;
};

// ---- The second piece, ON TRIAL (the hidden "Mega Geometry trial" 5, 7, 8; docs/agents/megageo-4.md, megageo-5.md): plant
// groups built from clusters stamped from templates, as NVIDIA's own samples set them up (vk_animated_clusters,
// vk_lod_clusters; measured offline in docs/agents/megageo-3.md): a mesh is cut once into clusters by NVIDIA's
// clusterizer (meshoptimizer's spatial meshlet builder, at most kMegaClusterTriangles triangles and kMegaClusterVertices
// vertices, at least a quarter of that, fill 0.5, each cluster's triangles put in order by meshoptimizer), a template is
// built once a cluster, and every frame each member of a group has its clusters stamped from the templates with its
// posed vertices (32-bit indices into the member's own vertices in the group's buffer); then the group's bottom level is
// built from them (5), or each member's own (7, 8: NVIDIA's layout, a structure a copy); FAST_TRACE everywhere.
//
// THE SAME TRIANGLES, THE SAME HITS: a stamped cluster is a submesh record of its group of its own (its geometry index),
// its triangles in the group's index buffer in the template's order (pt_sway's step 4 merges the member's indices from
// the cut's order: mega order), each with its own corners in their own order, so a hit's geometry and primitive index
// find the triangle in the group as a plain group's do, and no shader knows the difference.
//
// MICROMAPS (megageo-5): NVAPI's cluster calls take an opacity micromap array and an index buffer per template, in NVAPI's
// own arrays (nvapi.h: NvAPI_D3D12_BuildRaytracingOpacityMicromapArray; the cluster arguments' opacityMicromapArray and
// opacityMicromapIndexBuffer; FLAG_ALLOW_OMM on every call that builds or reads such an object). So a plant mesh's
// micromaps, baked as before (pt_omm_bake), are built a second time as an NVAPI array from the same bake output, and its
// index buffer is written in each cluster's triangle order from the bake's own indices (MegaMicromaps, mega_templates).
// The cluster size (megageo-5): NVIDIA's own for animated clusters (vk_animated_clusters: 64 triangles, 64 vertices),
// which megageo-3 measured best for rays through trees and equal on small plants.
constexpr uint32_t kMegaClusterTriangles=64, kMegaClusterVertices=64;
// A stamped cluster's submesh number in its group, at most (it names the cluster's record): every call says it, and a
// group with more clusters than that stays plain (65,536 clusters of up to 64 triangles in a cell of 16 m).
constexpr uint32_t kMegaGeometryMost=65535;
// A mesh's submesh as the cut reads it: its indices in the mesh's index list, and how many of its first triangles are of
// pieces that close a volume (scene.cpp mark_closed; GeometryRecordGpu's upper bits): those stay first in each cluster.
struct MegaSubmesh { uint32_t indexStart=0, indexCount=0, solid=0; };
// A mesh's micromaps as NVAPI's own array (megageo-5): built from the same bake as the mesh's DirectX Raytracing 1.2 one, in
// the same frame (GpuScene::record_omm), for that build of them (`serial`: Mesh::ommSerial). `indices`: the bake's index
// buffer as it came back to the CPU (one a cut-out triangle, in the mesh's own order: its micromap or a special index),
// empty until then; `count` of them.
struct MegaMicromaps {
    Buffer array;
    uint32_t serial=0, count=0;
    std::vector<int32_t> indices;
    bool ready() const { return array && count && indices.size()==count; }
};
// Builds `out` from a bake's output (`input`: `count` micromaps of `level`, `bytes` each, one after another; the state
// NON_PIXEL_SHADER_RESOURCE), recorded on the list. False with `error` when it could not (the mesh's clusters then take no
// micromaps).
bool mega_build_micromaps(ID3D12Device5* device, ID3D12GraphicsCommandList4* list, DeferredRelease& release, uint64_t retireValue, D3D12_GPU_VIRTUAL_ADDRESS input,
                          uint32_t count, uint32_t level, uint32_t bytes, uint32_t serial, MegaMicromaps& out, Barriers& barriers, std::string& error);
// Which templates: the pattern of opaque submeshes (an instance's materials say which are cut-outs; a template's clusters
// carry the OPAQUE flag of theirs), the pattern of submeshes whose clusters link the mesh's micromaps, that build of
// them, and whether the template's calls carry FLAG_ALLOW_OMM (bit i for submesh i).
struct MegaTemplateKey {
    uint64_t opaque=0, linked=0; uint32_t serial=0; bool omm=false;
    bool operator<(const MegaTemplateKey& o) const {
        if(opaque!=o.opaque) return opaque<o.opaque;
        if(linked!=o.linked) return linked<o.linked;
        if(serial!=o.serial) return serial<o.serial;
        return omm<o.omm;
    }
};
// One mesh cut into clusters, and its templates. `order` holds the mesh's indices with each submesh's triangles in its
// clusters' order, in the same places as the mesh's own (a submesh's triangles stay within its index range), each
// triangle with its own corners in the mesh's own order (a micromap hangs on them): the templates' index buffer, and the
// one pt_sway reads a member made of clusters from. `source`: per triangle in that order, its number in its submesh (the
// bake's indices are in the submesh's own order). A cluster's triangles keep its closed pieces first (`solid` of them).
// `ommOrders`: the micromap index buffer in the cut's order, for each build of the micromaps it links (mega_templates).
struct MegaMeshClusters {
    struct Cluster { uint32_t submesh=0, firstIndex=0, triangles=0, vertices=0, solid=0; };
    std::vector<Cluster> clusters;       // by submesh, then in the cut's order
    std::vector<uint32_t> firstCluster;  // per submesh its first cluster, and the count after the last
    std::vector<uint32_t> indices;       // the order on the CPU, until it is uploaded
    std::vector<uint32_t> source;        // per triangle of the order: its number in its submesh
    Buffer order; uint32_t orderSrv=kInvalid;
    std::map<uint32_t, Buffer> ommOrders; // per build of the micromaps (its serial): their index buffer in the cut's order
    struct Templates { Buffer buffer; std::vector<D3D12_GPU_VIRTUAL_ADDRESS> at; };
    std::map<MegaTemplateKey, Templates> templates;
    uint64_t triangles=0;
    double boxDiagonal=0;                // a cluster's box's diagonal, on average (for the log)
};
// Cuts a mesh (positions: 3 floats a vertex; indices into them). False with `error` when it cannot be cut.
bool mega_cut(const float* positions, uint32_t vertexCount, const uint32_t* indices, size_t indexCount, const std::vector<MegaSubmesh>& submeshes,
              MegaMeshClusters& out, std::string& error);
// Puts the cut's order on the GPU (once; recorded on the list), with its raw view; false with `error` when it could not.
bool mega_upload_order(ID3D12Device5* device, ID3D12GraphicsCommandList4* list, DescriptorHeap& heap, DeferredRelease& release, uint64_t retireValue,
                       MegaMeshClusters& mesh, Barriers& barriers, std::string& error);
// Uploads the cut's order (once) and builds the templates for `key` (once each), recorded on the list; false with `error`
// when a call failed (the trial's clusters are then off for good: the caller says so). `positions`: the mesh's own
// positions, which only shape the templates (NVIDIA's samples pass them). Where `key.linked` names submeshes: `maps` (the
// mesh's NVAPI micromaps of key.serial, ready) and `ommFirst` (Mesh::ommFirst: per submesh, its first triangle in the
// bake's indices, kInvalid where it has none).
bool mega_templates(ID3D12Device5* device, ID3D12GraphicsCommandList4* list, DescriptorHeap& heap, DeferredRelease& release, uint64_t retireValue,
                    MegaMeshClusters& mesh, const MegaTemplateKey& key, D3D12_GPU_VIRTUAL_ADDRESS positions, uint32_t vertexCount,
                    const MegaMicromaps* maps, const std::vector<uint32_t>* ommFirst, Barriers& barriers, std::string& error);
// The templates (and the micromap index buffer) made for micromaps of `serial`, which are gone: they go too.
void mega_release_maps(MegaMeshClusters& mesh, uint32_t serial, std::vector<Buffer>& gone); // (their buffers into `gone`, for the caller's release)
void mega_release(MegaMeshClusters& mesh, DescriptorHeap* heap, DeferredRelease& release, uint64_t retireValue);

// The frame's stamps and the bottom levels from them: one call for every stamp, then one call a size class of bottom
// levels (megageo-6, below), every posed group of clusters in them.
//
// megageo-6, NVAPI's rule for explicit destinations (nvapi.h, the cluster operation's destinationAddressArray): "each
// element of the array must contain the destination address with sufficient memory for either resultDataMaxSizeInBytes
// or a previous call with ..._MODE_GET_SIZES", where resultDataMaxSizeInBytes is the driver's answer for THAT call's
// inputs. Until megageo-6 a plant's bottom level had a room sized for its own count of clusters, while the frame's one
// call said at most `most` clusters a bottom level (the largest tree's): every smaller plant's room was below the call's
// per-object size. Now a bottom level's room is its size class's (the smallest power of two at or above its count of
// clusters, mega_bottom_class) and the frame's bottom levels go in one call a class, each call saying its class as its
// most: every room holds the driver's answer for its call, which is asked for and checked before the call is recorded
// (a larger answer fails the trial, said once, in place of a write past a room). NVIDIA's own sample takes the other
// way NVAPI documents (implicit destinations: one room for all, the addresses written by the driver and copied into
// the instances by a pass of its own); ours stays explicit, since our instances are written on the CPU.
//
// And what the driver wrote: each call writes the size of every object it built (resultSizeArray); every
// kMegaSizeCheckEvery frames those are copied back and held against their rooms when the GPU is past them (check()):
// a size past its room is said and fails the trial. The check that our rooms hold what the driver builds.
constexpr uint32_t kMegaSizeCheckEvery=16;
// A bottom level of `clusters` clusters is built and roomed as one of its class: the smallest power of two at or above.
inline uint32_t mega_bottom_class(uint32_t clusters) { uint32_t c=1; while(c<clusters && c<(1u<<30)) c<<=1; return c; }
class MegaClusterBuilder {
public:
    // A stamp: a template of a member's mesh, the member's posed vertices (its first in the group's buffer), the
    // cluster's record in the group (the template's own number plus the member's first record), and where it goes.
    struct Stamp { D3D12_GPU_VIRTUAL_ADDRESS templ=0, vertices=0, dest=0; uint32_t geometryOffset=0, triangles=0, vertexCount=0; };
    // A bottom level (a group's, or one member's in NVIDIA's layout): its stamps (first, count in its group's stamps) and
    // where it goes (`dest`: its offset in the group's `blas`).
    struct Bottom { D3D12_GPU_VIRTUAL_ADDRESS dest=0; uint32_t first=0, count=0; uint64_t bytes=0; };
    // One posed group's part of the frame: its stamps, its bottom levels and the buffer they go in.
    struct Part {
        const Stamp* stamps=nullptr; uint32_t stampCount=0;
        const Bottom* bottoms=nullptr; uint32_t bottomCount=0;
        D3D12_GPU_VIRTUAL_ADDRESS blas=0, clas=0; uint64_t blasBytes=0, clasBytes=0, serial=0;
    };
    // The room a stamp of any cluster takes, and a bottom level of up to `clusters` clusters: its class's (the driver's own
    // sizes, asked once; `omm`: with FLAG_ALLOW_OMM, as the frame's calls then carry it).
    uint64_t stamp_bytes(ID3D12Device5* device, bool omm);
    uint64_t bottom_bytes(ID3D12Device5* device, uint32_t clusters, bool omm);
    // Records the frame's stamps, then the bottom levels; false with `error` when a call failed or the driver's answer for a
    // call is larger than the rooms. `omm`: the calls carry FLAG_ALLOW_OMM (some templates link micromaps). Each part has
    // a lifetime serial, buffer bounds and immutable arguments. The exact same parts reuse the GPU arguments; changed
    // order, membership, addresses or room rebuild them before use. No hash can mistake a new group for the old one.
    bool build(ID3D12Device5* device, ID3D12GraphicsCommandList4* list, UploadRing& ring, DeferredRelease& release, uint64_t retireValue,
               const std::vector<Part>& parts, bool omm, Barriers& barriers, std::string& error);
    // The driver's sizes copied back (build, every kMegaSizeCheckEvery frames) and held against their rooms once the GPU is
    // past `completed`: false with `error` when one is larger than its room. `line`: a summary every time a copy is read.
    bool check(uint64_t completed, std::string& line, std::string& error);
    void release(DeferredRelease& release, uint64_t retireValue);
    uint64_t bytes() const { return allocation_bytes(scratch_.resource.Get())+allocation_bytes(dest_.resource.Get())+allocation_bytes(sizes_.resource.Get())+
                                    allocation_bytes(args_.resource.Get()); }
    uint64_t readback_bytes() const { uint64_t n=0; for(const auto& c:checks_) n+=allocation_bytes(c.readback.resource.Get()); return n; }
    // What the last build did, for the CPU's count (scene.cpp): the arguments written anew (false: used again), and the
    // count of bottom-level calls.
    bool rewrote=false; uint32_t bottomCalls=0;
private:
    Buffer scratch_, dest_, sizes_, args_;
    uint64_t stampBytes_[2]{};
    std::map<std::pair<uint32_t, bool>, uint64_t> bottomBytes_;
    // The arguments on the GPU (args_: the stamps', then each class's bottom levels'; dest_: the stamps' destinations, then
    // each class's), for `parts_`; and each class's place in them.
    struct Class { uint32_t clusters=0, count=0, total=0, first=0; };
    std::vector<Part> parts_; // identity/bounds only: never dereference an old part's pointers
    uint32_t stampCount_=0; uint64_t triangles_=0, vertices_=0, scratchBytes_=0; bool omm_=false;
    std::vector<Class> classes_;
    uint64_t stampArgsAt_=0, bottomArgsAt_=0; // where the bottom levels' arguments start in args_ (the stamps' at 0)
    uint64_t frames_=0;
    // The sizes copied back: the stamps' count and room, each bottom level's room in the order of sizes_.
    struct SizeCheck { uint64_t fence=0; Buffer readback; uint32_t stamps=0; uint64_t stampRoom=0; std::vector<uint32_t> rooms; };
    std::vector<SizeCheck> checks_;
    std::vector<uint32_t> rooms_; // each bottom level's room in the order of sizes_, for `parts_`
    uint64_t checked_=0, checkedStamps_=0, checkedBottoms_=0; double worstStamp_=0, worstBottom_=0;
};
} // namespace tgpt
