// SPDX-License-Identifier: MIT
// The clip-space turn that puts the camera in the same orientation as the depth, motion and
// HUD-less copies the bridge hands to DLSS-G, FSR and XeSS. Everything here is arithmetic: no D3D
// device, no GPU and no SDK is exercised, so it runs on every build host.
//
// The reference matrices are built the way Unity builds them - column-vector, m[row][column] - for
// both orientations, with the presentation forms derived through an independent matrix inverse
// rather than through the conversion under test.
#include "camera_orientation.hpp"
#include "tgfg_api.h"
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
static unsigned checks=0;
static bool failed=false;
#define CHECK(x) do {++checks;if(!(x)){std::cerr<<"FAILED line "<<__LINE__<<": " #x "\n";failed=true;}}while(0)

struct Mat { double m[4][4]{}; };
static Mat identity() { Mat r; for(int i=0;i<4;++i) r.m[i][i]=1; return r; }
static Mat mul(const Mat& a,const Mat& b) {
    Mat r;
    for(int i=0;i<4;++i) for(int j=0;j<4;++j) { double s=0; for(int k=0;k<4;++k) s+=a.m[i][k]*b.m[k][j]; r.m[i][j]=s; }
    return r;
}
// Gauss-Jordan with partial pivoting. Checked against the product below before it is relied on.
static Mat inverse(const Mat& a) {
    double w[4][8]{};
    for(int i=0;i<4;++i) { for(int j=0;j<4;++j) w[i][j]=a.m[i][j]; w[i][4+i]=1; }
    for(int col=0;col<4;++col) {
        int pivot=col;
        for(int row=col+1;row<4;++row) if(std::fabs(w[row][col])>std::fabs(w[pivot][col])) pivot=row;
        if(std::fabs(w[pivot][col])<1e-12) { std::cerr<<"singular reference matrix\n"; std::exit(EXIT_FAILURE); }
        if(pivot!=col) for(int j=0;j<8;++j) std::swap(w[col][j],w[pivot][j]);
        const double scale=w[col][col];
        for(int j=0;j<8;++j) w[col][j]/=scale;
        for(int row=0;row<4;++row) if(row!=col) {
            const double factor=w[row][col];
            if(factor!=0) for(int j=0;j<8;++j) w[row][j]-=factor*w[col][j];
        }
    }
    Mat r; for(int i=0;i<4;++i) for(int j=0;j<4;++j) r.m[i][j]=w[i][4+j];
    return r;
}
// Unity's column-vector transform into the row-vector row-major form TGFG_Camera carries, exactly
// as FrameBridge.MakeCamera writes it: packet[r*4+c] = matrix[c,r].
static void store(const Mat& a,float* out) { for(int r=0;r<4;++r) for(int c=0;c<4;++c) out[r*4+c]=float(a.m[c][r]); }
// y = x * S, the row-vector product the runtimes apply to a clip-space position.
static void apply_row(const float* s,const double* x,double* y) {
    for(int j=0;j<4;++j) { double v=0; for(int i=0;i<4;++i) v+=x[i]*double(s[i*4+j]); y[j]=v; }
}
static bool same(const float* a,const float* b,double tolerance=1e-6) {
    for(int i=0;i<16;++i) {
        const double scale=std::fmax(1.0,std::fabs(double(b[i])));
        if(std::fabs(double(a[i])-double(b[i]))>tolerance*scale) return false;
    }
    return true;
}
// Y is negated in clip space; the transform is its own inverse.
static Mat flip() { Mat f=identity(); f.m[1][1]=-1; return f; }
// A reverse-Z Direct3D projection, as GL.GetGPUProjectionMatrix produces for HDRP: the near plane
// maps to 1 and the far plane to 0. renderIntoTexture:true additionally negates row 1, which is
// exactly the flip above, so this is the presentation form. yOffset makes the frustum vertically
// off-centre, putting a term in row 1 away from the diagonal; without one, negating the wrong row
// or column of the transposed packet produces the same matrix and no test could tell them apart.
static Mat projection(double yOffset=0) {
    const double fovRadians=1.0472,aspect=16.0/9.0,nearPlane=0.3,farPlane=1000.0;
    const double f=1.0/std::tan(fovRadians*0.5);
    Mat p;
    p.m[0][0]=f/aspect; p.m[1][1]=f; p.m[1][2]=yOffset;
    p.m[2][2]=nearPlane/(farPlane-nearPlane); p.m[2][3]=farPlane*nearPlane/(farPlane-nearPlane);
    p.m[3][2]=-1;
    return p;
}
// A rigid world-to-view transform: yaw about Y, then translation. View space looks down -Z.
static Mat view(double yaw,double x,double y,double z) {
    const double c=std::cos(yaw),s=std::sin(yaw);
    Mat v=identity();
    v.m[0][0]=c;  v.m[0][2]=s;
    v.m[2][0]=-s; v.m[2][2]=c;
    v.m[0][3]=x; v.m[1][3]=y; v.m[2][3]=z;
    return v;
}
// One frustum, checked end to end. yOffset selects a symmetric or a vertically off-centre one.
static void check_frustum(double yOffset) {
    const Mat F=flip();
    const Mat projectionPres=projection(yOffset);        // renderIntoTexture:false
    const Mat projectionRt=mul(F,projectionPres);        // renderIntoTexture:true
    const Mat viewNow=view(0.35,2.5,-1.25,9.0), viewPrev=view(0.31,2.2,-1.20,8.4);

    // Both orientations built the way FrameBridge does: clipToPrev = previousViewProjection * inverse(viewProjection).
    const Mat vpPres=mul(projectionPres,viewNow), prevVpPres=mul(projectionPres,viewPrev);
    const Mat vpRt=mul(projectionRt,viewNow), prevVpRt=mul(projectionRt,viewPrev);
    const Mat clipToPrevPres=mul(prevVpPres,inverse(vpPres));
    const Mat clipToPrevRt=mul(prevVpRt,inverse(vpRt));

    TGFG_Camera camera{};
    store(projectionRt,camera.viewToClip);
    store(inverse(projectionRt),camera.clipToView);
    store(clipToPrevRt,camera.clipToPrev);
    store(inverse(clipToPrevRt),camera.prevToClip);
    store(viewNow,camera.worldToView);
    camera.position[0]=2.5f; camera.up[1]=1; camera.right[0]=1; camera.forward[2]=-1;
    camera.nearPlane=0.3f; camera.farPlane=1000; camera.fovRadians=1.0472f; camera.aspect=16.f/9.f;
    const TGFG_Camera original=camera;

    TGFG_Camera expected{};
    store(projectionPres,expected.viewToClip);
    store(inverse(projectionPres),expected.clipToView);
    store(clipToPrevPres,expected.clipToPrev);
    store(inverse(clipToPrevPres),expected.prevToClip);

    // An off-centre frustum must make row and column negation distinguishable, or the four checks
    // below would pass with the two helpers swapped.
    if(yOffset!=0) {
        float asRow[16],asColumn[16];
        std::memcpy(asRow,camera.viewToClip,sizeof asRow);
        std::memcpy(asColumn,camera.viewToClip,sizeof asColumn);
        tgfg::negate_clip_row(asRow); tgfg::negate_clip_column(asColumn);
        CHECK(!same(asRow,asColumn));
        std::memcpy(asRow,camera.clipToView,sizeof asRow);
        std::memcpy(asColumn,camera.clipToView,sizeof asColumn);
        tgfg::negate_clip_row(asRow); tgfg::negate_clip_column(asColumn);
        CHECK(!same(asRow,asColumn));
    }

    tgfg::to_presentation_clip(camera);
    CHECK(same(camera.viewToClip,expected.viewToClip));
    CHECK(same(camera.clipToView,expected.clipToView));
    CHECK(same(camera.clipToPrev,expected.clipToPrev));
    CHECK(same(camera.prevToClip,expected.prevToClip));
    // Nothing outside clip space moves.
    CHECK(std::memcmp(camera.worldToView,original.worldToView,sizeof camera.worldToView)==0);
    CHECK(std::memcmp(camera.position,original.position,sizeof camera.position)==0);
    CHECK(std::memcmp(camera.up,original.up,sizeof camera.up)==0);
    CHECK(std::memcmp(camera.right,original.right,sizeof camera.right)==0);
    CHECK(std::memcmp(camera.forward,original.forward,sizeof camera.forward)==0);
    CHECK(camera.nearPlane==original.nearPlane && camera.farPlane==original.farPlane);
    CHECK(camera.fovRadians==original.fovRadians && camera.aspect==original.aspect);

    // The property the runtimes depend on: the converted clipToPrev takes a presentation clip-space
    // position to where that same world point was in the previous frame's presentation clip space.
    const double world[4]={4.0,1.5,-6.0,1.0};
    double clipNow[4]{},clipPrev[4]{},reprojected[4]{};
    for(int i=0;i<4;++i) {
        double n=0,q=0;
        for(int k=0;k<4;++k) { n+=vpPres.m[i][k]*world[k]; q+=prevVpPres.m[i][k]*world[k]; }
        clipNow[i]=n; clipPrev[i]=q;
    }
    apply_row(camera.clipToPrev,clipNow,reprojected);
    for(int i=0;i<4;++i) CHECK(std::fabs(reprojected[i]-clipPrev[i])<1e-4*std::fmax(1.0,std::fabs(clipPrev[i])));
    // And prevToClip takes it back.
    double returned[4]{};
    apply_row(camera.prevToClip,reprojected,returned);
    for(int i=0;i<4;++i) CHECK(std::fabs(returned[i]-clipNow[i])<1e-4*std::fmax(1.0,std::fabs(clipNow[i])));

    // Turning the orientation twice is the identity, bit for bit: every step is a sign change.
    tgfg::to_presentation_clip(camera);
    CHECK(std::memcmp(&camera,&original,sizeof camera)==0);

    // A camera that never moved keeps an identity clipToPrev through the conversion.
    TGFG_Camera still{};
    store(identity(),still.clipToPrev); store(identity(),still.prevToClip);
    store(projectionRt,still.viewToClip); store(inverse(projectionRt),still.clipToView);
    tgfg::to_presentation_clip(still);
    float wanted[16]; store(identity(),wanted);
    // Numerically, not bitwise: negating a zero produces a negative zero, which every consumer
    // treats as zero but memcmp does not.
    CHECK(same(still.clipToPrev,wanted));
    CHECK(same(still.prevToClip,wanted));
}
int main() {
    // The reference inverse is trusted only after it round-trips a non-trivial matrix.
    const Mat sample=mul(projection(0.17),view(0.4,3,-2,7));
    const Mat roundTrip=mul(sample,inverse(sample));
    for(int i=0;i<4;++i) for(int j=0;j<4;++j) CHECK(std::fabs(roundTrip.m[i][j]-(i==j?1.0:0.0))<1e-9);

    check_frustum(0.0);   // symmetric, as the game renders
    check_frustum(0.17);  // off-centre, so a row/column mix-up cannot hide on the diagonal

    if(failed) return EXIT_FAILURE;
    std::cout<<checks<<" camera orientation checks passed. No D3D device, GPU or vendor SDK was exercised.\n";
}
