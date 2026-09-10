/* Shim for a header the upstream v0.2 release tarball is missing.
 *
 * src/vec.h's SCALAR fallback path - the one taken whenever neither
 * __AVX__/__SSE2__ nor NEON is defined, which is exactly what MSVC does
 * by default - does #include "os_support.h" and calls OPUS_CLEAR(). That
 * header is one of the files rnnoise inherits from Opus, and it simply
 * isn't in the v0.2 tarball or listed in its Makefile.am, so upstream's
 * own autotools build would hit this too on any target without those
 * vector paths. It only ever needed the one macro here.
 *
 * Deliberately NOT a copy of Opus's real os_support.h: that file also
 * declares allocation and stack-alloc helpers rnnoise never references,
 * and vendoring code we don't use would be more to keep correct, not
 * less. Verified by grepping the whole tree - OPUS_CLEAR is the only
 * symbol from it that rnnoise's sources touch.
 */

#ifndef INKWYRD_RNNOISE_OS_SUPPORT_H
#define INKWYRD_RNNOISE_OS_SUPPORT_H

#include <string.h>

/* Same semantics as Opus's own: n is a COUNT OF ELEMENTS, not bytes. */
#define OPUS_CLEAR(dst, n) (memset((dst), 0, (n) * sizeof(*(dst))))
#define OPUS_COPY(dst, src, n) (memcpy((dst), (src), (n) * sizeof(*(dst))))
#define OPUS_MOVE(dst, src, n) (memmove((dst), (src), (n) * sizeof(*(dst))))

#endif
