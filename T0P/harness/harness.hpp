#pragma once
#include "common.hpp"
#include <memory>
#include <string>
#include <vector>

namespace harness {
struct CellSpec { int m=1, output=1024; std::string id="smoke_uniform", variant="mm"; };
struct Region { const void* data=nullptr; std::size_t bytes=0; bool large=false; const char* name=""; };
struct Provider {
  virtual ~Provider()=default;
  virtual const char* name() const noexcept=0;
  virtual void prepare(const CellSpec&, unsigned worker)=0;
  virtual void run_once(unsigned worker, int m)=0;
  virtual bool validate(std::string& why)=0;
  virtual std::vector<Region> bytes_regions() const=0;
  virtual t0common::Cell& cell()=0;
};
std::unique_ptr<Provider> make_omega();
std::unique_ptr<Provider> make_ggml_flat();
std::unique_ptr<Provider> make_torch_out();
int run_smoke(const std::string& outdir);
}
