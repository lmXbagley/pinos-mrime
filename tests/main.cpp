#include "test_util.hpp"

int main() {
  mpt::runAll();
  if (mpt::failures() > 0) {
    std::fprintf(stderr, "\n%d FAILURE(S)\n", mpt::failures());
    return 1;
  }
  std::fprintf(stderr, "\nALL TESTS PASSED\n");
  return 0;
}
