// Keeps GPU 0 busy for about two seconds, for trying out parprof --gpu.
//
// The kernel is compiled at runtime with NVRTC and launched through the CUDA
// driver API, so this builds with the regular host compiler (nvcc 12.x doesn't
// accept GCC 15).
//
// By default the CPU thread spins inside cuCtxSynchronize() while it waits for
// the GPU, so it shows ~100% CPU. --blocking-sync makes it sleep instead.
#include <cuda.h>
#include <nvrtc.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

static const char* kKernel = R"(
extern "C" __global__ void spin(float* data, int iters) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    float v = data[i];
    for (int k = 0; k < iters; ++k) v = v * 0.9999f + 0.0001f;
    data[i] = v;
}
)";

static void check(CUresult r, const char* what) {
    if (r == CUDA_SUCCESS) return;
    const char* msg = "unknown error";
    cuGetErrorString(r, &msg);
    std::fprintf(stderr, "%s: %s\n", what, msg);
    std::exit(1);
}

static std::string compile_ptx(int major, int minor) {
    nvrtcProgram prog;
    nvrtcCreateProgram(&prog, kKernel, "spin.cu", 0, nullptr, nullptr);
    std::string arch = "--gpu-architecture=compute_" + std::to_string(major * 10 + minor);
    const char* opts[] = {arch.c_str()};
    if (nvrtcCompileProgram(prog, 1, opts) != NVRTC_SUCCESS) {
        size_t size;
        nvrtcGetProgramLogSize(prog, &size);
        std::string log(size, '\0');
        nvrtcGetProgramLog(prog, log.data());
        std::fprintf(stderr, "kernel compile failed:\n%s\n", log.c_str());
        std::exit(1);
    }
    size_t size;
    nvrtcGetPTXSize(prog, &size);
    std::string ptx(size, '\0');
    nvrtcGetPTX(prog, ptx.data());
    nvrtcDestroyProgram(&prog);
    return ptx;
}

int main(int argc, char* argv[]) {
    bool blocking = argc > 1 && std::strcmp(argv[1], "--blocking-sync") == 0;
    CUdevice dev;
    CUcontext ctx;
    int major, minor;
    check(cuInit(0), "cuInit");
    check(cuDeviceGet(&dev, 0), "cuDeviceGet");
    check(cuDeviceGetAttribute(&major, CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR, dev), "attr");
    check(cuDeviceGetAttribute(&minor, CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR, dev), "attr");
    check(cuCtxCreate(&ctx, blocking ? CU_CTX_SCHED_BLOCKING_SYNC : 0, dev), "cuCtxCreate");

    CUmodule module;
    CUfunction spin;
    std::string ptx = compile_ptx(major, minor);
    check(cuModuleLoadData(&module, ptx.c_str()), "cuModuleLoadData");
    check(cuModuleGetFunction(&spin, module, "spin"), "cuModuleGetFunction");

    const unsigned n = 64u << 20;  // 64M floats = 256 MiB of device memory
    CUdeviceptr data;
    check(cuMemAlloc(&data, n * sizeof(float)), "cuMemAlloc");
    check(cuMemsetD32(data, 0, n), "cuMemsetD32");

    int iters = 200;
    void* args[] = {&data, &iters};
    auto start = std::chrono::steady_clock::now();
    int launches = 0;
    while (std::chrono::steady_clock::now() - start < std::chrono::seconds(2)) {
        check(cuLaunchKernel(spin, n / 256, 1, 1, 256, 1, 1, 0, nullptr, args, nullptr), "launch");
        check(cuCtxSynchronize(), "cuCtxSynchronize");
        ++launches;
    }

    cuMemFree(data);
    cuModuleUnload(module);
    cuCtxDestroy(ctx);
    std::printf("gpu_busy (%s): %d kernel launches\n", blocking ? "blocking sync" : "spin wait", launches);
}
