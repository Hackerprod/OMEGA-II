#pragma once
#include "common.hpp"
#include <memory>
#include <fstream>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace harness {
struct CellSpec { int m=1, output=1024; std::string id="smoke_uniform", variant="mm", frontier="prepared"; };
struct Region { const void* data=nullptr; std::size_t bytes=0; bool large=false; const char* name=""; };
struct Provider {
  virtual ~Provider()=default;
  virtual const char* name() const noexcept=0;
  virtual void prepare(const CellSpec&, unsigned worker)=0;
  virtual void run_once(unsigned worker, int m)=0;
  virtual void run_once_frontier(unsigned worker, int m, const std::string& frontier) {
    if (frontier != "prepared" && frontier != "dynamic") throw std::invalid_argument("unknown measurement frontier");
    run_once(worker, m);
  }
  virtual std::uint64_t dynamic_prep_bytes() const noexcept { return 0; }
  virtual bool validate(std::string& why)=0;
  virtual std::vector<Region> bytes_regions() const=0;
  virtual t0common::Cell& cell()=0;
};
std::unique_ptr<Provider> make_omega();
std::unique_ptr<Provider> make_ggml_flat();
std::unique_ptr<Provider> make_ggml_repack();
std::unique_ptr<Provider> make_torch_out();
int run_aux_smoke(std::ofstream& raw,std::ofstream& summary,int sample_count,const std::string& disassembly_qa_path);
int run_aux_official(std::ofstream& raw,std::ofstream& summary,int sample_count,const std::string& disassembly_qa_path);
int run_aux_smoke_entry_r12(const std::string& outdir,const std::string& disassembly_qa_path);
int run_smoke(const std::string& outdir,const std::string& plan_path,const std::string& verified_plan_sha256,int sample_count=3);
int run_smoke_r12(const std::string& outdir,const std::string& plan_path,const std::string& verified_plan_sha256,int sample_count=3);
int run_geometry_smoke(const std::string& outdir,const std::string& plan_path,const std::string& verified_plan_sha256);
int run_official();
int run_official_measurement(const std::string&outdir,const std::string&plan_path,const std::string&verified_plan_sha256);
int check_auth_fixture(const std::string&path,const std::string&plan_path);
int run_provider_qa(const std::string&provider,int output,int m);
int run_residency_smoke(const std::string&outdir,const std::string&conditions_csv,int sample_count);
int run_residency_campaign(const std::string&outdir,const std::string&conditions_csv,int sample_count);
}
