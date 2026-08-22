#include "example_util.hpp"
int main() { Runtime rt({}); if (run_host_to_host(rt, 4u<<20)) { std::printf("[ok] host->host 4 MiB\n"); return 0; } return 1; }
