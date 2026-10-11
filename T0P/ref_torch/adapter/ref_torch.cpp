#include "q4_kernel.hpp"
#include <ATen/ATen.h>
#include <ATen/Context.h>
#include <ATen/Version.h>
#include <torch/headeronly/version.h>
#include <torch/csrc/api/include/torch/utils.h>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

static float half_to_float(std::uint16_t h) {
    const unsigned sign = (h >> 15) & 1, exp = (h >> 10) & 31, frac = h & 1023;
    if (exp == 0) return std::ldexp(static_cast<float>(frac), -24) * (sign ? -1.f : 1.f);
    if (exp == 31) return frac ? NAN : (sign ? -INFINITY : INFINITY);
    return (sign ? -1.f : 1.f) * std::ldexp(1.f + static_cast<float>(frac) / 1024.f, static_cast<int>(exp) - 15);
}
static int q4(const std::uint8_t* p, std::size_t j) {
    unsigned n = (j & 1) ? p[j / 2] >> 4 : p[j / 2] & 15;
    return n >= 8 ? static_cast<int>(n) - 16 : static_cast<int>(n);
}
int main(int argc, char** argv) try {
    constexpr std::size_t O=1024, I=512;
    if (argc != 3) throw std::runtime_error("usage: ref_torch.exe M threads");
    const std::size_t M=std::stoul(argv[1]); const int threads=std::stoi(argv[2]);
    if (!(M==1 || M==4 || M==8 || M==16) || threads<1) throw std::runtime_error("invalid M or thread count");
    at::set_num_threads(threads); at::set_num_interop_threads(1);
    at::globalContext().setFloat32MatmulPrecision("highest");
    at::NoGradGuard no_grad;
    const auto w=t0q4::generate_weights(O,I,0x544F505F515034ULL,false);
    const auto a=t0q4::generate_activations(M,I,0x544F505F414354ULL,false);
    std::vector<float> x(M*I), wt(I*O);
    for(std::size_t m=0;m<M;++m) for(std::size_t j=0;j<I;++j)
        x[m*I+j]=a.scale[m*a.groups_per_row+j/32]*static_cast<float>(a.q[m*I+j]);
    // Static packing: dequantize and transpose once into contiguous [I,O] for X @ W^T.
    for(std::size_t o=0;o<O;++o) for(std::size_t j=0;j<I;++j) {
        const auto* g=w.storage.data()+(o*w.groups_per_row+j/32)*18;
        wt[j*O+o]=half_to_float(static_cast<std::uint16_t>(g[16] | (g[17]<<8)))*static_cast<float>(q4(g,j%32));
    }
    auto X=at::from_blob(x.data(),{static_cast<std::int64_t>(M),static_cast<std::int64_t>(I)},at::TensorOptions().dtype(at::kFloat)).clone();
    auto Wt=at::from_blob(wt.data(),{static_cast<std::int64_t>(I),static_cast<std::int64_t>(O)},at::TensorOptions().dtype(at::kFloat)).clone();
    at::Tensor Y=at::matmul(X,Wt);
    Y=Y.contiguous();
    const float* y=Y.data_ptr<float>(); double max_abs=0, max_rel=0; std::size_t bad=0;
    for(std::size_t m=0;m<M;++m) for(std::size_t o=0;o<O;++o) {
        double ref=0;
        for(std::size_t g=0;g<w.groups_per_row;++g) {
            const auto* p=w.storage.data()+(o*w.groups_per_row+g)*18;
            std::int32_t dot=0;
            for(std::size_t k=0;k<32;++k) dot += static_cast<std::int32_t>(a.q[m*I+g*32+k])*q4(p,k);
            const double ws=half_to_float(static_cast<std::uint16_t>(p[16] | (p[17]<<8)));
            ref += static_cast<double>(dot)*static_cast<double>(a.scale[m*w.groups_per_row+g])*ws;
        }
        const double err=std::abs(static_cast<double>(y[m*O+o])-ref);
        const double tol=1e-5+1e-5*std::abs(ref);
        if(err>max_abs) max_abs=err;
        if(err/(std::abs(ref)+1e-30)>max_rel) max_rel=err/(std::abs(ref)+1e-30);
        if(err>tol) ++bad;
    }
    std::cout << "torch_version=" << TORCH_VERSION << " M=" << M << " threads=" << threads << "\n";
    std::cout << "mkldnn=" << at::globalContext().hasMKLDNN() << " mkl=" << at::get_mkl_version() << " oneDNN=" << at::get_mkldnn_version() << "\n";
    std::cout << "precision=highest grad=disabled op=at::matmul shape=["<<M<<",512]x[512,1024]\n";
    std::cout << "max_abs_error=" << max_abs << " max_rel_error=" << max_rel << " failures=" << bad << "\n";
    std::cout << "--- ATen build config ---\n" << at::show_config() << "\n";
    return bad ? 2 : 0;
} catch(const std::exception& e) { std::cerr << "ERROR: " << e.what() << "\n"; return 1; }



