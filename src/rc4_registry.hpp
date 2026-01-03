#pragma once
#include <cstdint>
#include <map>
#include <optional>
#include <string>

/**
 * RC4 Transponder Registration System
 *
 * Maps fingerprints to user-assigned transponder IDs.
 * Fingerprints are extracted from raw softbits and uniquely identify each
 * transponder.
 */
class RC4Registry {
public:
  // Register a fingerprint with a transponder ID
  void register_transponder(uint16_t fingerprint, uint32_t transponder_id);

  // Look up a transponder ID by fingerprint
  // Returns nullopt if fingerprint not registered
  std::optional<uint32_t> lookup(uint16_t fingerprint) const;

  // Check if a fingerprint is registered
  bool is_registered(uint16_t fingerprint) const;

  // Get number of registered transponders
  size_t size() const { return registry_.size(); }

  // Save registry to file
  bool save_to_file(const std::string &path) const;

  // Load registry from file
  bool load_from_file(const std::string &path);

  // Clear all registrations
  void clear() { registry_.clear(); }

private:
  std::map<uint16_t, uint32_t> registry_; // fingerprint -> transponder_id
};

// Extract 16-bit fingerprint from raw softbits
uint16_t extract_rc4_fingerprint(const uint8_t *softbits);

// Global registry instance
extern RC4Registry g_rc4_registry;
