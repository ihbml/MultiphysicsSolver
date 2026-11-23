#include "cases.hpp"
#include <cstdio>
#ifdef CB_CUDA
#include "cuda_api.hpp"
#endif

using namespace cb;

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
static std::unique_ptr<Run> instance;
extern "C" {
int EMSCRIPTEN_KEEPALIVE cb_create(int id,int scheme,int kind,double cfl,int hybrid,int cavitation,int quality) {
    try {
        instance=std::make_unique<Run>(static_cast<Case>(id),static_cast<Kind>(kind),
            Options{static_cast<Scheme>(scheme),cfl,hybrid!=0,cavitation!=0},quality);
        return 1;
    } catch (...) { instance.reset(); return 0; }
}
int EMSCRIPTEN_KEEPALIVE cb_advance(double target) {
    if (!instance) return 0;
    try { instance->advance(target); return 1; } catch (...) { return 0; }
}
int EMSCRIPTEN_KEEPALIVE cb_nx() { return instance->nx; }
int EMSCRIPTEN_KEEPALIVE cb_ny() { return instance->ny; }
int EMSCRIPTEN_KEEPALIVE cb_frames() { return instance->frames; }
double EMSCRIPTEN_KEEPALIVE cb_lx() { return instance->lx; }
double EMSCRIPTEN_KEEPALIVE cb_ly() { return instance->ly; }
double EMSCRIPTEN_KEEPALIVE cb_end() { return instance->end; }
const double* EMSCRIPTEN_KEEPALIVE cb_metrics() { return instance->m.data(); }
const float* EMSCRIPTEN_KEEPALIVE cb_fields() { return instance->fields.data(); }
}
#else
#include <cstdint>
#include <fstream>
#include <iostream>

int main(int argc,char** argv) {
    try {
        if (argc<2) {
            std::cout<<"Usage: cluster-b <bubble|sod|acoustic|film> [binary file] [quality 0..2] [hllc|ausm|standard] [ideal|stiffened|pr] [CFL] [hybrid 0|1] [cavitation 0|1]\n";
            return 0;
        }
#ifdef CB_CUDA
        if (!cuda_available()) throw std::runtime_error("CUDA device unavailable; run the CPU executable instead");
        face_flux_batch=&cuda_flux;
#endif
        Case c=Case::Bubble;
        bool found=false;
        for (auto v:{Case::Bubble,Case::Sod,Case::Acoustic,Case::Film}) if (name(v)==argv[1]) { c=v; found=true; }
        if (!found) throw std::invalid_argument("unknown case");
        const std::string path=argc>2 ? argv[2] : std::string(name(c))+".cbf";
        const int quality=argc>3 ? std::stoi(argv[3]) : 1;
        const std::string method=argc>4 ? argv[4] : "hllc";
        const auto s=method=="ausm" ? Scheme::Ausm : method=="standard" ? Scheme::Hllc : Scheme::AllMach;
        const std::string eos_arg=argc>5 ? argv[5] : c==Case::Bubble ? "stiffened" : "ideal";
        const auto k=eos_arg=="pr" ? Kind::PengRobinson : eos_arg=="stiffened" ? Kind::Stiffened : Kind::Ideal;
        const double cfl=argc>6 ? std::stod(argv[6]) : .42;
        if (!(cfl>=.10&&cfl<=.65)||quality<0||quality>2) throw std::invalid_argument("CFL must be in [0.10,0.65], quality in [0,2]");
        Run r(c,k,{s,cfl,argc<=7||std::stoi(argv[7])!=0,argc<=8||std::stoi(argv[8])!=0},quality);
        std::ofstream out(path,std::ios::binary);
        if (!out) throw std::runtime_error("cannot open snapshot file");
        out.write("CBF1",4);
        const std::int32_t dimensions[]={r.nx,r.ny,r.frames,channels,metrics};
        const double metadata[]={r.lx,r.ly,r.end};
        out.write(reinterpret_cast<const char*>(dimensions),sizeof dimensions);
        out.write(reinterpret_cast<const char*>(metadata),sizeof metadata);
        for (int f=0;f<r.frames;++f) {
            r.advance(r.end*f/(r.frames-1.));
            out.write(reinterpret_cast<const char*>(r.m.data()),sizeof(double)*metrics);
            out.write(reinterpret_cast<const char*>(r.fields.data()),sizeof(float)*r.fields.size());
        }
        if (!out) throw std::runtime_error("cannot write snapshots");
        std::printf("%s: %dx%d, %d frames, %g final time, %g relative mass defect, %d fallback attempts\n",
                    argv[1],r.nx,r.ny,r.frames,r.m[0],r.m[3],static_cast<int>(r.m[16]));
        return 0;
    } catch (const std::exception& e) {
        std::cerr<<"cluster-b: "<<e.what()<<'\n'; return 1;
    }
}
#endif