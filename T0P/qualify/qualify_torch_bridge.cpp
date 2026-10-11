#define main qualify_torch_full_qa_entry
#include "../ref_torch/adapter/ref_torch.cpp"
#undef main

int main() try {
    large_page_privilege = check_large_page_privilege();
    configure();
    at::NoGradGuard no_grad;
    at::addGlobalCallback(at::RecordFunctionCallback(&on_op));
    t0common::verifier_unit_tests();
    bool all = large_page_privilege;
    for (int M : {1, 4, 8, 16}) {
        const auto c = t0common::generate(M, t0common::Fixture::Uniform);
        std::vector<float> xv(size_t(M) * t0common::I);
        for (int m = 0; m < M; ++m) for (int i = 0; i < t0common::I; ++i)
            xv[size_t(m) * t0common::I + i] = float(c.xq[size_t(m) * t0common::I + i]) * c.xs[size_t(m) * t0common::G + i / 32];
        auto X = tensor(xv, {M, t0common::I});
        auto wv = unpack_weights(c, false), wtv = unpack_weights(c, true);
        auto W = tensor(wv, {t0common::O, t0common::I});
        auto Wt = tensor(wtv, {t0common::I, t0common::O});
        for (const std::string name : {"mm", "matmul", "linear", "mv"}) {
            if (name == "mv" && M != 1) continue;
            auto Y = common_output(M, name);
            ops.clear();
            auto out = run_candidate(name, X, W, Wt, at::Tensor(), at::Tensor(), M, Y);
            const char* weight_name = (name == "mm" || name == "matmul") ? "Wt" : "W";
            const at::Tensor& ew = (name == "mm" || name == "matmul") ? Wt : W;
            const std::string cand_storage = "torch_" + name;
            const char* cand = cand_storage.c_str();
            const bool pages = report_buffer("uniform", M, cand, "X", "input", "dryrun_X", X) &&
                report_buffer("uniform", M, cand, weight_name, "weight", "dryrun_W", ew) &&
                report_buffer("uniform", M, cand, "Y", "output", "dryrun_Y", Y);
            std::vector<float> values(out.data_ptr<float>(), out.data_ptr<float>() + out.numel());
            const auto check = t0common::verify_s12(c, values);
            const bool pass = pages && check.pass && out.device().is_cpu() && !ops.empty();
            std::cout << "DRY_ROUTE candidate=" << cand << " m=" << M << " status=" << (pass ? "PASS" : "FAIL")
                      << " numerical=" << (check.pass ? "PASS" : "FAIL") << " page=" << (pages ? "PASS" : "FAIL")
                      << " dispatch_observed=" << (!ops.empty() ? "true" : "false") << " timing_fields=null\n";
            all &= pass;
        }
    }
    std::cout << "DRY_REFERENCE_TORCH status=" << (all ? "PASS" : "FAIL") << " timed=false\n";
    return all ? 0 : 21;
} catch (const std::exception& e) { std::cerr << "DRY_REFERENCE_TORCH_ERROR " << e.what() << "\n"; return 22; }
