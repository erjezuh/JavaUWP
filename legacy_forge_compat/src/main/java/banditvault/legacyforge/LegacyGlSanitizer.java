package banditvault.legacyforge;

import java.nio.IntBuffer;

/**
 * Defensive validation for {@code glMultiDrawArrays} range arrays.
 *
 * OptiFine's Render Regions ({@code net.optifine.render.VboRegion.finishDraw})
 * computes the per-range {@code first}/{@code count} arrays itself and can emit
 * negative or overflowing values (documented OptiFine bug class: issues #2779
 * and #7757, and the Ars Nouveau report with the identical
 * {@code glMultiDrawArrays <- VboRegion.finishDraw} call chain). Mesa reacts to
 * those corrupt ranges by dereferencing a null object inside libgallium_wgl
 * (EXCEPTION_ACCESS_VIOLATION, hs_err confirmed on this device).
 *
 * The vanilla draw path never calls glMultiDrawArrays (per-chunk glDrawArrays)
 * and is immune; this sanitizer is what makes the OptiFine multi-draw path
 * safe to keep using. Invalid ranges are dropped (their count is zeroed) in
 * place, without touching buffer positions. Any buffer oddity is swallowed so
 * rendering can never break harder than the call being passed through
 * unchanged.
 */
public final class LegacyGlSanitizer {
    private LegacyGlSanitizer() {
    }

    /**
     * Zeroes the {@code count} entry of every range that is negative or whose
     * {@code first + count} would overflow a signed 32-bit vertex index.
     */
    public static void sanitizeMultiDrawRanges(IntBuffer first, IntBuffer count) {
        try {
            if (first == null || count == null) {
                return;
            }
            final int firstBase = first.position();
            final int countBase = count.position();
            final int n = Math.min(first.remaining(), count.remaining());
            for (int i = 0; i < n; i++) {
                final int f = first.get(firstBase + i);
                final int c = count.get(countBase + i);
                if (f < 0 || c < 0 || (c > 0 && f > Integer.MAX_VALUE - c)) {
                    count.put(countBase + i, 0);
                }
            }
        } catch (Throwable ignored) {
            // Pass the call through unchanged rather than break rendering.
        }
    }
}
