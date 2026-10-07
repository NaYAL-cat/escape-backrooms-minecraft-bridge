#include "../host/reshade/src/upload_layout.h"
#include <cassert>
#include <cstdio>
#include <vector>
#include <cstring>
#include <array>
int main() {
    for(auto dims:std::array<std::array<unsigned,2>,5>{{{320,200},{1920,1080},{1919,1079},{3440,1440},{3840,2160}}}) {
        const auto l=bridge_upload::layout(dims[0],dims[1]);
        assert(l.row_pitch%256==0 && l.layer_stride%512==0 && l.row_pitch>=l.row_bytes);
        assert(l.layer_stride>=uint64_t(l.row_pitch)*dims[1]);
        // Exercise the production pitch/offset calculation for all three distinct layers, including padding.
        std::vector<unsigned char> buffer(size_t(l.total_bytes),0xcd),row(l.row_bytes);
        for(unsigned layer=0;layer<3;layer++) for(unsigned y=0;y<dims[1];y++) {
            std::fill(row.begin(),row.end(),static_cast<unsigned char>(layer*53+y%51));
            std::memcpy(buffer.data()+layer*l.layer_stride+uint64_t(y)*l.row_pitch,row.data(),l.row_bytes);
        }
        for(unsigned layer=0;layer<3;layer++) for(unsigned y=0;y<dims[1];y++) {
            const auto offset=layer*l.layer_stride+uint64_t(y)*l.row_pitch;
            assert(buffer[offset]==layer*53+y%51);
            assert(buffer[offset+l.row_bytes-1]==layer*53+y%51);
            if(l.row_pitch>l.row_bytes) assert(buffer[offset+l.row_bytes]==0xcd);
        }
    }
    assert(bridge_upload::reusable(0,0));
    assert(!bridge_upload::reusable(5,4));
    assert(bridge_upload::reusable(5,5));
    assert(!bridge_upload::reusable(UINT64_MAX,100));
    std::puts("PASS: upload pitch/placement, three-layer separation, in-flight and failed-fence reuse guards.");
}
