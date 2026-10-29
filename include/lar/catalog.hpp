#pragma once

#include <string>

namespace lar::catalog {

// Pure, host-parameterized allowlist predicates used by the release resolver and
// regression tests. These are deliberately based on official asset filenames;
// callers must still verify the HTTPS URL, published SHA-256, and size metadata.
bool is_supported_engine_archive(const std::string& name, const std::string& variant,
                                 const std::string& os, const std::string& architecture);
bool is_supported_cuda_runtime_archive(const std::string& name, const std::string& variant,
                                       const std::string& os, const std::string& architecture);

} // namespace lar::catalog
