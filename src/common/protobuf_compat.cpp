// Workaround: Provide missing protobuf memswap<N> specialization
// Required by generated protobuf code for RepeatedPtrField small element swap.

#include <cstring>

namespace google {
namespace protobuf {
namespace internal {

template <size_t N>
void memswap(char* a, char* b) {
    char tmp[N];
    std::memcpy(tmp, a, N);
    std::memcpy(a, b, N);
    std::memcpy(b, tmp, N);
}

// Explicit instantiation for N=16 (used by RepeatedPtrField)
template void memswap<16>(char*, char*);

}  // namespace internal
}  // namespace protobuf
}  // namespace google
