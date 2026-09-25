// SPDX-License-Identifier: MIT
// HDRP renders into Y-inverted render targets and flips the image back in its final blit, so the
// frame a generation runtime interpolates - the presented back buffer, and the HUD-less copy that is
// verified against it every present - is vertically mirrored against the depth and motion vectors
// HDRP produced. No frame generation SDK can be told that a tagged texture is upside down:
// Streamline, the FSR API and XeSS all sample depth and motion at the coordinates of the frame they
// interpolate. The bridge therefore copies depth and motion in presentation orientation for every
// backend, and the camera has to describe that same clip space.
//
// The change of orientation is F = diag(1,-1,1,1) in clip space, which is its own inverse. For
// Unity's column-vector projection P the presentation form is F*P, so a transform C between two clip
// spaces becomes F*C*F. TGFG_Camera carries these transposed, as row-vector row-major matrices, and
// transposing reverses the order: (F*M)^T = M^T * F negates column 1 of the packet, while
// (M*F)^T = F * M^T negates row 1 of it.
#pragma once
namespace tgfg {
// Row 1 of a row-vector row-major 4x4: the transpose of right-multiplying the column-vector form by F.
inline void negate_clip_row(float* m) { for(unsigned c=0;c<4;++c) m[4+c]=-m[4+c]; }
// Column 1 of a row-vector row-major 4x4: the transpose of left-multiplying the column-vector form by F.
inline void negate_clip_column(float* m) { for(unsigned r=0;r<4;++r) m[r*4+1]=-m[r*4+1]; }
// In place, on a camera packet as the plugin sent it: the orientation HDRP rendered with, turned into
// the orientation everything downstream works in. Applying it twice restores the original.
template<class Camera> void to_presentation_clip(Camera& camera) {
    negate_clip_column(camera.viewToClip);                              // (F*P)^T      = P^T * F
    negate_clip_row(camera.clipToView);                                 // (P^-1*F)^T   = F * (P^-1)^T
    negate_clip_row(camera.clipToPrev); negate_clip_column(camera.clipToPrev);  // (F*C*F)^T
    negate_clip_row(camera.prevToClip); negate_clip_column(camera.prevToClip);  // (F*C^-1*F)^T
    // worldToView, the camera basis and the scalar frustum are outside clip space and unchanged.
}
} // namespace tgfg
