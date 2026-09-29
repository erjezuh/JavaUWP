package banditvault.legacyforge;

import java.nio.IntBuffer;

import org.lwjgl.opengl.GL11;

/**
 * Crash-free replacement for {@code glMultiDrawArrays}.
 *
 * Two independent crash reports on this device (hs_err confirmed) show the
 * access violation inside Mesa's glMultiDrawArrays path even after sanitizing
 * the range arrays, and OptiFine's Render Regions is a documented crash class
 * on top of it (issues #2779/#7757). The call itself is the problem.
 *
 * {@link #multiDrawAsSingles} performs the exact same rendering as
 * glMultiDrawArrays(mode, first[], count[]) - the GL spec defines the multi
 * draw as the equivalent sequence of glDrawArrays calls with shared state -
 * but only ever enters Mesa through glDrawArrays, the path vanilla has used
 * crash-free all along. OptiFine's region VBO merging (one buffer bind per
 * region per layer) is preserved: the expensive part, per-chunk buffer binds
 * and attribute setup, stays collapsed; only the draw submission becomes a
 * loop. Contiguous ranges are merged so typical region batches collapse to
 * few draws. Invalid ranges (negative/overflowing, the documented OptiFine
 * bug) are dropped. Never throws.
 */
public final class LegacyGlSanitizer {
    private LegacyGlSanitizer() {
    }

    /**
     * Executes the multi-draw as validated single draws against the currently
     * bound vertex buffer.
     */
    public static void multiDrawAsSingles(int mode, IntBuffer first, IntBuffer count) {
        try {
            if (first == null || count == null) {
                return;
            }
            final int firstBase = first.position();
            final int countBase = count.position();
            final int n = Math.min(first.remaining(), count.remaining());
            int runStart = -1;
            int runEnd = -1;
            for (int i = 0; i <= n; i++) {
                int f = 0;
                int c = -1;
                if (i < n) {
                    f = first.get(firstBase + i);
                    c = count.get(countBase + i);
                    if (f < 0 || c < 0 || (c > 0 && f > Integer.MAX_VALUE - c)) {
                        c = -1; // invalid range: drop it
                    }
                }
                final boolean merges = c > 0 && runEnd >= 0 && f == runEnd;
                if (merges) {
                    runEnd = f + c;
                    continue;
                }
                if (runStart >= 0 && runEnd > runStart) {
                    GL11.glDrawArrays(mode, runStart, runEnd - runStart);
                }
                runStart = (c > 0) ? f : -1;
                runEnd = (c > 0) ? f + c : -1;
            }
        } catch (Throwable ignored) {
            // Never break rendering harder than the call we replace.
        }
    }
}
