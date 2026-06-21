#pragma once

namespace cuda {

using cudaError_t = int;
using size_t = __SIZE_TYPE__;

inline constexpr cudaError_t cudaSuccess = 0;
inline constexpr int cudaMemcpyHostToDevice = 1;
inline constexpr int cudaMemcpyDeviceToHost = 2;

struct blockIdx {
    static int x;
};

struct blockDim {
    static int x;
};

struct threadIdx {
    static int x;
};

cudaError_t cudaMalloc(void** ptr, size_t bytes);
cudaError_t cudaFree(void* ptr);
cudaError_t cudaMemcpy(void* dst, const void* src, size_t bytes, int kind);

template <typename Kernel, typename... Args>
void launch(Kernel kernel, int grid, int block, Args... args);

} // namespace cuda
