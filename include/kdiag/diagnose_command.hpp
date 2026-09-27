// Options and entry point for deterministic diagnosis of a saved collection.
#ifndef A615690E_CD64_40FB_8E6D_2E45BEFA248F
#define A615690E_CD64_40FB_8E6D_2E45BEFA248F

#include <string>

namespace kdiag {

struct DiagnoseOptions {
    std::string input_path;  ///< Collection JSON produced by `kdiag collect`.
    bool verbose = false;  ///< Include LOW-confidence hypotheses.
};

/// Load, correlate, and diagnose a collection file.
/// @return 0 on success; nonzero if the file cannot be read or parsed.
int RunDiagnoseCommand(const DiagnoseOptions& options);

}  // namespace kdiag


#endif /* A615690E_CD64_40FB_8E6D_2E45BEFA248F */
