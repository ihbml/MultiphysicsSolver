#include "cuda_api.hpp"
#include <cuda_runtime.h>
#include <stdexcept>
#include <string>

namespace cb {
namespace {

void require(cudaError_t s) {
    if (s!=cudaSuccess) throw std::runtime_error(std::string("CUDA: ")+cudaGetErrorString(s));
}

template<class T>
struct Device {
    T* ptr{};
    std::size_t capacity{};
    Device()=default;
    Device(const Device&)=delete;
    Device& operator=(const Device&)=delete;
    ~Device() { if (ptr) cudaFree(ptr); }
    void reserve(std::size_t n) {
        if (capacity>=n) return;
        if (ptr) { require(cudaFree(ptr)); ptr=nullptr; capacity=0; }
        require(cudaMalloc(reinterpret_cast<void**>(&ptr),n*sizeof(T)));
        capacity=n;
    }
};

__global__ void riemann_kernel(Scheme s,const FaceTask* in,Flux* out,std::size_t n) {
    const std::size_t i=blockIdx.x*blockDim.x+threadIdx.x;
    if (i<n) out[i]=flux(s,in[i].l,in[i].r,in[i].n);
}

__global__ void source_kernel(const SourceTask* in,SourceResult* out,std::size_t n) {
    const std::size_t i=blockIdx.x*blockDim.x+threadIdx.x;
    if (i<n) out[i]=local_sources(in[i]);
}

template<class T,class U,class Launch>
void batch(std::span<const T> in,std::span<U> out,Launch launch) {
    if (in.size()!=out.size()) throw std::invalid_argument("CUDA batch span lengths differ");
    if (in.empty()) return;
    static thread_local Device<T> source;
    static thread_local Device<U> dest;
    source.reserve(in.size()); dest.reserve(out.size());
    require(cudaMemcpy(source.ptr,in.data(),in.size_bytes(),cudaMemcpyHostToDevice));
    launch(source.ptr,dest.ptr,in.size());
    require(cudaGetLastError());
    require(cudaMemcpy(out.data(),dest.ptr,out.size_bytes(),cudaMemcpyDeviceToHost));
}

} // namespace

bool cuda_available() { int n=0; return cudaGetDeviceCount(&n)==cudaSuccess&&n>0; }

void cuda_flux(Scheme s,std::span<const FaceTask> in,std::span<Flux> out) {
    batch(in,out,[s](const FaceTask* a,Flux* b,std::size_t n) { riemann_kernel<<<(n+127)/128,128>>>(s,a,b,n); });
}

void cuda_sources(std::span<const SourceTask> in,std::span<SourceResult> out) {
    batch(in,out,[](const SourceTask* a,SourceResult* b,std::size_t n) { source_kernel<<<(n+127)/128,128>>>(a,b,n); });
}

} // namespace cb