#ifndef TOPPERS_FMP3_M5CORES3_H
#define TOPPERS_FMP3_M5CORES3_H

#include <Arduino.h>

namespace toppers {
namespace fmp3 {
namespace m5cores3 {

/**
 * Metadata describing this build of the integration layer.
 *
 * kernelLinked is a constant, not a detection: nothing in the sketch build
 * defines a marker this code could test. It is true because the library is
 * distributed inside the TOPPERS/FMP3 platform and Arduino only offers a
 * platform-bundled library when that board is selected. Do not read it as
 * proof that the kernel is present.
 */
struct LibraryInfo {
    const char *name;
    const char *version;
    const char *description;
    bool kernelLinked;
};

/**
 * Returns metadata describing the currently linked integration layer.
 */
LibraryInfo libraryInfo();

}  // namespace m5cores3
}  // namespace fmp3
}  // namespace toppers

#endif  // TOPPERS_FMP3_M5CORES3_H
