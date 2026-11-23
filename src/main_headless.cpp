// minosprime — headless CLI (ядро v0.2; GUI — отдельная цель minosprime_gui).
//
// Команды:
//   minosprime validate <файл.mnp.json>              — R1/8.x флаги, exit 1 при красных
//   minosprime serve <файл.mnp.json> [--port N]      — API (10.x) на 127.0.0.1 + такт симуляции
//   minosprime stands <файл.mnp.json>                — 9.4 прогон регрессии стендов
#include "mp/api.hpp"
#include "mp/serialize.hpp"
#include "mp/stands.hpp"
#include "mp/validator.hpp"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

using namespace mp;

static bool loadFile(const std::string& path, Project& doc, std::string& err) {
  FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) { err = "cannot open " + path; return false; }
  std::string txt;
  char buf[65536];
  size_t n;
  while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) txt.append(buf, n);
  std::fclose(f);
  return loadProjectJson(txt, doc, err);
}

static int cmdValidate(const std::string& path) {
  Project doc; std::string err;
  if (!loadFile(path, doc, err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 2; }
  std::vector<Flag> fl = validate(doc);
  int red = 0, yellow = 0;
  for (const auto& f : fl) {
    if (f.level == FlagLevel::Red) ++red;
    else ++yellow;
    std::printf("%-7s %-16s %-12s %s\n", f.level == FlagLevel::Red ? "RED" : "YELLOW",
                f.code.c_str(), f.unit.c_str(), f.txt.c_str());
  }
  std::printf("%zu flags: %d red, %d yellow\n", fl.size(), red, yellow);
  return red > 0 ? 1 : 0;
}

static int cmdStands(const std::string& path) {
  Project doc; std::string err;
  if (!loadFile(path, doc, err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 2; }
  std::vector<StandRunResult> rs = runRegression(doc);
  if (rs.empty()) { std::printf("no stands recorded\n"); return 0; }
  int fail = 0;
  for (const auto& r : rs) {
    bool ok = !r.hashMismatch && r.replayOk && r.regressionOk;
    if (!ok) ++fail;
    std::printf("%-6s %-20s spikes=%lld (%.1f/s) hash=%s%s%s\n",
                ok ? "PASS" : "FAIL", r.name.c_str(), r.spikesTotal, r.spikesPerSec,
                r.resultHash.c_str(),
                r.hashMismatch ? " [schema hash mismatch!]" : "",
                r.replayOk ? "" : " [replay errors!]");
  }
  return fail ? 1 : 0;
}

static int cmdServe(const std::string& path, int port) {
  Project doc; std::string err;
  if (!loadFile(path, doc, err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 2; }
  Engine eng(doc);
  ApiServer api(doc, eng, port);
  if (!api.start(err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 2; }
  std::printf("MINOSPRIME API on 127.0.0.1:%d  (Ctrl+C to stop)\n", port);
  std::printf("  /get_neuron?id=ID   /set_sensor?id=ID&v=X   /set_sensors?ids=A,B&v=X,Y\n");
  std::printf("  /get_schema         /stream?ids=A,B&zone=Z&hz=N\n");
  while (true) {        // 1 tick = 1 ms (docs/SIMULATION.md)
    eng.tick();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  api.stop();
  return 0;
}

int main(int argc, char** argv) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: minosprime validate|serve|stands <file.mnp.json> [--port N]\n");
    return 2;
  }
  std::string cmd = argv[1], path = argv[2];
  int port = 18080;
  for (int i = 3; i + 1 < argc; ++i)
    if (std::strcmp(argv[i], "--port") == 0) port = std::atoi(argv[i + 1]);
  if (cmd == "validate") return cmdValidate(path);
  if (cmd == "serve") return cmdServe(path, port);
  if (cmd == "stands") return cmdStands(path);
  std::fprintf(stderr, "unknown command %s\n", cmd.c_str());
  return 2;
}
