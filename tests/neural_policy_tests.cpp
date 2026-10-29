// SPDX-License-Identifier: MIT
#include "tgnr_policy.hpp"
#include <cstdio>
#include <limits>
int main() {
    int failures=0;
    auto expect=[&](bool value,const char* name) { std::printf("%s %s\n",value?"PASS":"FAIL",name); if(!value) ++failures; };
    TGNR_Frame f{sizeof(TGNR_Frame),TGNR_ABI,12,7,1,2,3,1920,1080,960,540,1,1,0,0,0.1f,1000};
    expect(tgnr::valid(f,true) && tgnr::valid(f,false),"valid frame and guides");
    auto other=f; other.camera=8; expect(!tgnr::matching(f,other),"other camera rejected");
    other=f; --other.frame; expect(!tgnr::matching(f,other),"stale guides rejected");
    other=f; other.renderWidth=1280; expect(!tgnr::matching(f,other),"different guide viewport rejected");
    other=f; other.width=1280; expect(!tgnr::matching(f,other),"different output viewport rejected");
    other=f; other.flags=0; expect(!tgnr::matching(f,other),"different depth convention rejected");
    other=f; other.jitterX=0.5f; expect(!tgnr::matching(f,other),"different jitter rejected");
    other=f; other.depth=0; expect(!tgnr::valid(other,true),"missing depth rejected");
    other=f; other.motion=0; expect(!tgnr::valid(other,true),"missing motion rejected");
    other=f; other.color=0; expect(!tgnr::valid(other,false),"missing color rejected");
    other=f; other.strength=std::numeric_limits<float>::quiet_NaN(); expect(!tgnr::valid(other,false),"NaN setting rejected");
    other=f; other.renderWidth=0; expect(!tgnr::valid(other,true),"empty viewport rejected");
    other=f; other.nearPlane=0; expect(!tgnr::valid(other,true),"invalid depth range rejected");
    other=f; other.frame=13;
    expect(!tgnr::reset_history(other,f,true),"normal frame keeps history");
    expect(tgnr::reset_history(other,f,false),"activation resets history");
    other.frame=14; expect(tgnr::reset_history(other,f,true),"frame gap resets history");
    other=f; other.frame=13; other.flags|=TGNR_RESET; expect(tgnr::reset_history(other,f,true),"camera cut resets history");
    other=f; other.frame=13; other.strength=0.5f; expect(!tgnr::reset_history(other,f,true),"intensity keeps history");
    other=f; other.frame=13; other.model=1; expect(tgnr::reset_history(other,f,true),"model resets history");
    other=f; other.frame=13; other.localTone=0; expect(tgnr::reset_history(other,f,true),"tone resets history");
    other=f; other.model=3; expect(tgnr::valid(other,false),"style C accepted");
    other=f; other.model=4; expect(!tgnr::valid(other,false),"unvalidated style rejected");
    other=f; other.localStructure=2; expect(!tgnr::valid(other,false),"invalid detail rejected");
    other=f; other.passes=0; expect(!tgnr::valid(other,false),"zero passes rejected");
    other=f; other.passes=TGNR_MAX_PASSES+1; expect(!tgnr::valid(other,false),"excess passes rejected");
    other=f; other.passes=TGNR_MAX_PASSES; expect(tgnr::valid(other,false),"multiple passes accepted");
    other=f; other.frame=13; other.passes=2; expect(tgnr::reset_history(other,f,true),"pass count resets all histories");
    other=f; other.frame=13; other.nearPlane=1; expect(tgnr::reset_history(other,f,true),"depth range changes reset history");
    return failures ? 1:0;
}
