#include "common.hpp"
#include <iomanip>
#include <iostream>

int main() {
    using namespace t0common;
    std::cout << std::setprecision(17);
    for (int fixture = 0; fixture < int(Fixture::Count); ++fixture) {
        for (int m : {1, 4, 8, 16}) {
            const auto c = generate(m, Fixture(fixture));
            const auto r = reference64(c);
            for (int slot = 0; slot < m; ++slot)
                for (int o : {0, 1, 127, 255, 511, 777, 1023})
                    std::cout << fixture_name(Fixture(fixture)) << ',' << m << ',' << slot << ',' << o << ',' << r.B[size_t(slot)*O+o] << '\n';
        }
    }
}
