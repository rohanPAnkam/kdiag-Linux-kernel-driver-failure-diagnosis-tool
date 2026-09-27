// Shared file loading for the normalized collection JSON format.

#ifndef FBB01191_D2FE_4EB6_B9F6_E1EAD444129C
#define FBB01191_D2FE_4EB6_B9F6_E1EAD444129C

#include <string>

#include "kdiag/event.hpp"

namespace kdiag {

/// Read and validate a collection file produced by `kdiag collect`.
/// @throws std::runtime_error with the path and failure stage on error.
CollectionRun LoadCollectionRun(const std::string& path);

}  // namespace kdiag


#endif /* FBB01191_D2FE_4EB6_B9F6_E1EAD444129C */
