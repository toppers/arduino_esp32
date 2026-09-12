#include "ToppersFMP3_M5CoreS3.h"

namespace toppers {
namespace fmp3 {
namespace m5cores3 {

LibraryInfo libraryInfo()
{
    //  The version must match library.properties. Nothing enforced that, and
    //  it drifted a whole release behind; scripts/check_release_artifacts.py
    //  now compares the two.
    return {
        "ToppersFMP3-M5Stack",
        "0.4.2",
        "TOPPERS/FMP3 runtime for M5Stack boards",
        true,
    };
}

}  // namespace m5cores3
}  // namespace fmp3
}  // namespace toppers
