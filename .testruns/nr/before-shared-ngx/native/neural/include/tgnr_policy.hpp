// SPDX-License-Identifier: MIT
#pragma once
#include "tgnr_api.h"
#include <cmath>
namespace tgnr {
inline bool valid(const TGNR_Frame& f, bool guides) {
    return f.size==sizeof(f) && f.abi==TGNR_ABI && f.width && f.height && f.renderWidth && f.renderHeight &&
        f.width<=16384 && f.height<=16384 && f.renderWidth<=16384 && f.renderHeight<=16384 &&
        !(f.flags&~15u) && std::isfinite(f.strength) && f.strength>=0 && f.strength<=1 &&
        f.model<=2 && f.autoMask<=1 &&
        std::isfinite(f.localTone) && f.localTone>=0 && f.localTone<=1 &&
        std::isfinite(f.localStructure) && f.localStructure>=0 && f.localStructure<=1 &&
        std::isfinite(f.skinStructure) && f.skinStructure>=0 && f.skinStructure<=1 &&
        std::isfinite(f.jitterX) && std::isfinite(f.jitterY) &&
        std::isfinite(f.nearPlane) && std::isfinite(f.farPlane) && f.nearPlane>0 && f.farPlane>f.nearPlane &&
        (guides ? f.depth && f.motion : f.color!=0);
}
inline bool matching(const TGNR_Frame& guides, const TGNR_Frame& color) {
    return guides.frame==color.frame && guides.camera==color.camera &&
        guides.renderWidth==color.renderWidth && guides.renderHeight==color.renderHeight &&
        guides.width==color.width && guides.height==color.height &&
        (guides.flags&TGNR_REVERSED_DEPTH)==(color.flags&TGNR_REVERSED_DEPTH) &&
        guides.jitterX==color.jitterX && guides.jitterY==color.jitterY &&
        guides.nearPlane==color.nearPlane && guides.farPlane==color.farPlane;
}
inline bool reset_history(const TGNR_Frame& current, const TGNR_Frame& previous, bool havePrevious) {
    return !havePrevious || (current.flags&TGNR_RESET) || current.frame!=previous.frame+1 ||
        current.camera!=previous.camera || current.width!=previous.width || current.height!=previous.height ||
        current.renderWidth!=previous.renderWidth || current.renderHeight!=previous.renderHeight ||
        current.nearPlane!=previous.nearPlane || current.farPlane!=previous.farPlane ||
        (current.flags&TGNR_REVERSED_DEPTH)!=(previous.flags&TGNR_REVERSED_DEPTH) ||
        (current.flags&TGNR_HDR_INPUT)!=(previous.flags&TGNR_HDR_INPUT) ||
        current.model!=previous.model || current.autoMask!=previous.autoMask ||
        current.localTone!=previous.localTone || current.localStructure!=previous.localStructure ||
        current.skinStructure!=previous.skinStructure;
}
}
