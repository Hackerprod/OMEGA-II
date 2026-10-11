#define main qualify_ggml_full_qa_entry
#include "../ref_ggml/adapter/main.cpp"
#undef main

int main(int argc, char** argv) try {
    if (argc != 2) return 2;
    const fs::path data = fs::absolute(argv[1]);
    large_page_privilege = check_large_page_privilege();
    const char* omp = std::getenv("OMP_NUM_THREADS");
    const char* dyn = std::getenv("OMP_DYNAMIC");
    if (!large_page_privilege || !omp || std::string(omp) != "1" || !dyn || _stricmp(dyn, "FALSE") != 0)
        throw std::runtime_error("dry-run resource/thread preflight failed");
    for (unsigned i = 0; i < (1u << 16); ++i) ggml_table_f32_f16[i] = ggml_fp16_to_fp32((ggml_fp16_t)i);
    const auto gemv = &ggml_gemv_q4_0_8x8_q8_0;
    const auto gemm = &ggml_gemm_q4_0_8x8_q8_0;
    if (!gemv || !gemm) throw std::runtime_error("repack symbols unavailable");
    t0common::verifier_unit_tests();
    bool all = true;
    for (int M : {1, 4, 8, 16}) {
        auto qa = prepare_static(data, M, t0common::Fixture::Uniform, CandidateRoute::QA);
        prepare_dynamic(qa, CandidateRoute::QA);
        if (!storage_controls(qa)) throw std::runtime_error("effective ggml representation control failed");
        for (const auto route : {CandidateRoute::Flat, CandidateRoute::Repack}) {
            auto p = prepare_static(data, M, t0common::Fixture::Uniform, route);
            prepare_dynamic(p, route);
            bool pages = page_query("uniform", M, route == CandidateRoute::Flat ? "ggml_flat" : "ggml_repack",
                "weights", "static_prepare", "dryrun_weights", route == CandidateRoute::Flat ? p.flatw.data() : p.packw.data(),
                route == CandidateRoute::Flat ? p.flatw.size() : p.packw.size());
            pages &= page_query("uniform", M, route == CandidateRoute::Flat ? "ggml_flat" : "ggml_repack",
                "activations", "dynamic_prepare", route == CandidateRoute::Flat ? "dryrun_flat_X" : "dryrun_repack_X",
                route == CandidateRoute::Flat ? p.flata.data() : (M == 1 ? p.flata.data() : p.packa.data()),
                route == CandidateRoute::Flat || M == 1 ? p.flata.size() : p.packa.size());
            pages &= page_query("uniform", M, route == CandidateRoute::Flat ? "ggml_flat" : "ggml_repack",
                "Y", "output", "dryrun_Y", p.out.data(), p.out.size() * sizeof(float));
            if (route == CandidateRoute::Flat) {
                if (!p.graph || (p.plan.work_size && !p.plan.work_data) || p.plan.use_ref) throw std::runtime_error("flat dispatch/setup not ready");
                run_once_flat(p);
            } else {
                (M == 1 ? gemv : gemm)(I, p.out.data(), O, p.packw.data(),
                    M == 1 ? static_cast<const void*>(p.flata.data()) : static_cast<const void*>(p.packa.data()), M, O);
            }
            const auto result = assess(p.c, p.out.data(), p.out.size());
            const bool pass = pages && result.pass && result.finite;
            std::cout << "DRY_ROUTE candidate=" << (route == CandidateRoute::Flat ? "ggml_flat" : "ggml_repack")
                      << " m=" << M << " status=" << (pass ? "PASS" : "FAIL")
                      << " numerical=" << (result.pass ? "PASS" : "FAIL")
                      << " page=" << (pages ? "PASS" : "FAIL")
                      << " dispatch=" << (route == CandidateRoute::Flat ? "ggml_graph_compute_use_ref_false" : (M == 1 ? "upstream_gemv" : "upstream_gemm"))
                      << " timing_fields=null\n";
            all &= pass;
        }
    }
    std::cout << "DRY_REFERENCE_GGML status=" << (all ? "PASS" : "FAIL") << " timed=false\n";
    return all ? 0 : 31;
} catch (const std::exception& e) { std::cerr << "DRY_REFERENCE_GGML_ERROR " << e.what() << "\n"; return 32; }
