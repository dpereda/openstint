#include "rc4_registry.hpp"
#include <cstdio>
#include <fstream>
#include <sstream>

// Global registry instance
RC4Registry g_rc4_registry;

void RC4Registry::register_transponder(uint16_t fingerprint,
                                       uint32_t transponder_id) {
  registry_[fingerprint] = transponder_id;
  std::fprintf(stderr, "[RC4] Registered fingerprint 0x%04X -> ID %u\n",
               fingerprint, transponder_id);
}

std::optional<uint32_t> RC4Registry::lookup(uint16_t fingerprint) const {
  auto it = registry_.find(fingerprint);
  if (it != registry_.end()) {
    return it->second;
  }
  return std::nullopt;
}

bool RC4Registry::is_registered(uint16_t fingerprint) const {
  return registry_.find(fingerprint) != registry_.end();
}

bool RC4Registry::save_to_file(const std::string &path) const {
  std::ofstream file(path);
  if (!file.is_open()) {
    std::fprintf(stderr, "[RC4] Failed to open %s for writing\n", path.c_str());
    return false;
  }

  file << "# RC4 Transponder Registry\n";
  file << "# Format: fingerprint_hex transponder_id\n";

  for (const auto &[fp, id] : registry_) {
    file << std::hex << fp << " " << std::dec << id << "\n";
  }

  std::fprintf(stderr, "[RC4] Saved %zu registrations to %s\n",
               registry_.size(), path.c_str());
  return true;
}

bool RC4Registry::load_from_file(const std::string &path) {
  std::ifstream file(path);
  if (!file.is_open()) {
    // File doesn't exist - not an error, just no prior registrations
    return true;
  }

  std::string line;
  size_t loaded = 0;

  while (std::getline(file, line)) {
    // Skip comments and empty lines
    if (line.empty() || line[0] == '#')
      continue;

    std::istringstream iss(line);
    uint16_t fp;
    uint32_t id;

    iss >> std::hex >> fp >> std::dec >> id;
    if (!iss.fail()) {
      registry_[fp] = id;
      loaded++;
    }
  }

  std::fprintf(stderr, "[RC4] Loaded %zu registrations from %s\n", loaded,
               path.c_str());
  return true;
}

uint16_t extract_rc4_fingerprint(const uint8_t *softbits) {
  // Convert first 16 softbits to hard bits (threshold at 0x80)
  uint16_t fingerprint = 0;
  for (int i = 0; i < 16; i++) {
    fingerprint <<= 1;
    if (softbits[i] >= 0x80) {
      fingerprint |= 1;
    }
  }
  return fingerprint;
}
