// Keeps the GPU busy for about two seconds, for trying out parprof --gpu.
// The CPU thread spins while it waits; --blocking-sync makes it sleep instead.
#include <chrono>
#include <cstdio>
#include <cstring>

__global__ void spin(float *data, int iters) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    float v = data[i];
    for (int k = 0; k < iters; ++k) v = v * 0.9999f + 0.0001f;
    data[i] = v;
}

int main(int argc, char *argv[]) {
    bool blocking = argc > 1 && strcmp(argv[1], "--blocking-sync") == 0;
    cudaSetDeviceFlags(blocking ? cudaDeviceScheduleBlockingSync : cudaDeviceScheduleSpin);

    const int n = 64 << 20;  // 256 MiB of floats
    float *data;
    if (cudaMalloc(&data, n * sizeof(float)) != cudaSuccess) {
        fprintf(stderr, "no usable CUDA GPU\n");
        return 1;
    }
    cudaMemset(data, 0, n * sizeof(float));

    auto start = std::chrono::steady_clock::now();
    int launches = 0;
    while (std::chrono::steady_clock::now() - start < std::chrono::seconds(2)) {
        spin<<<n / 256, 256>>>(data, 200);
        cudaDeviceSynchronize();
        ++launches;
    }

    cudaError_t err = cudaGetLastError();
    cudaFree(data);
    if (err != cudaSuccess) {
        fprintf(stderr, "kernel failed: %s\n", cudaGetErrorString(err));
        return 1;
    }
    printf("gpu_busy (%s): %d kernel launches\n", blocking ? "blocking sync" : "spin wait", launches);
    return 0;
}
