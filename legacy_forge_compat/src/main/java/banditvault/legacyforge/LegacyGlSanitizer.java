package banditvault.legacyforge;

import java.nio.IntBuffer;

import org.lwjgl.opengl.GL11;
import org.lwjgl.opengl.GL15;
import org.lwjgl.opengl.GL32;

/**
 * Turns Minecraft 1.12.2's GL_QUADS draws into native triangle draws.
 *
 * Root cause of the terrain fps ceiling (F3 data: identical ~170us cost per
 * visible chunk-section whether or not draw calls are batched): MC 1.12.2
 * submits every face as GL_QUADS, which no GPU implements. The driver must
 * convert each quad stream to triangles on the CPU before every draw (the
 * documented silent CPU fallback for unsupported primitives), and that
 * per-vertex work scales with visible geometry - exactly the observed
 * sky-fast / terrain-slow / leaves-worst pattern. Modern Minecraft draws
 * triangles natively and runs 200-300 fps on this same Mesa/D3D12 stack.
 *
 * The replacement draws the identical pixels through
 * glDrawElementsBaseVertex(GL_TRIANGLES, ...) with a static quad->triangle
 * index pattern (0,1,2, 2,3,0 per quad) and basevertex=first, so no
 * per-frame index building occurs and the fallback never runs. Spec-
 * equivalent output; only the primitive presentation changes.
 *
 * Kill switch: MC_QUADS_AS_TRIANGLES=0 (read once at class init).
 */
public final class LegacyGlSanitizer {
    private static final int GL_QUADS = 0x0007;

    private static final boolean quadsEnabled = readEnabled();
    private static int elementBufferId;
    private static IntBuffer pattern;
    private static int patternQuads;
    private static int uploadedQuads;

    private LegacyGlSanitizer() {
    }

    private static boolean readEnabled() {
        try {
            String value = System.getenv("MC_QUADS_AS_TRIANGLES");
            return value == null || !value.equals("0");
        } catch (Throwable ignored) {
            return true;
        }
    }

    /**
     * Handles a glDrawArrays call. Returns true when the draw was fully
     * performed as indexed triangles; false means the caller must forward the
     * original glDrawArrays (non-quad modes, disabled, or any failure).
     */
    public static boolean dispatchDrawArrays(int mode, int first, int count) {
        if (!quadsEnabled || mode != GL_QUADS || count < 4) {
            return false;
        }
        try {
            final int quads = count / 4;
            ensurePattern(quads);
            ensureElementBuffer(quads);
            GL11.glBindBuffer(GL15.GL_ELEMENT_ARRAY_BUFFER, elementBufferId);
            GL32.glDrawElementsBaseVertex(
                GL11.GL_TRIANGLES, quads * 6, GL11.GL_UNSIGNED_INT, 0L, first);
            return true;
        } catch (Throwable ignored) {
            return false;
        }
    }

    /** Executes a glMultiDrawArrays as validated single draws. */
    public static void multiDrawAsSingles(int mode, IntBuffer first, IntBuffer count) {
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
                if (f >= 0 && c > 0 && f <= Integer.MAX_VALUE - c) {
                    if (!dispatchDrawArrays(mode, f, c)) {
                        GL11.glDrawArrays(mode, f, c);
                    }
                }
            }
        } catch (Throwable ignored) {
            // Never break rendering harder than the call we replace.
        }
    }

    private static synchronized void ensurePattern(int quads) {
        if (pattern != null && patternQuads >= quads) {
            return;
        }
        int capacity = Math.max(quads, 4096);
        while (capacity < quads) {
            capacity *= 2;
        }
        final IntBuffer grown = IntBuffer.allocate(capacity * 6);
        for (int q = 0; q < capacity; q++) {
            final int v = q * 4;
            grown.put(v);
            grown.put(v + 1);
            grown.put(v + 2);
            grown.put(v + 2);
            grown.put(v + 3);
            grown.put(v);
        }
        grown.flip();
        pattern = grown;
        patternQuads = capacity;
        if (elementBufferId != 0) {
            try {
                GL15.glDeleteBuffers(elementBufferId);
            } catch (Throwable ignored) {
                // Best effort; a leaked buffer is harmless.
            }
            elementBufferId = 0;
        }
        uploadedQuads = 0;
    }

    private static synchronized void ensureElementBuffer(int quads) {
        if (elementBufferId == 0) {
            elementBufferId = GL15.glGenBuffers();
            uploadedQuads = 0;
        }
        if (uploadedQuads < quads) {
            GL11.glBindBuffer(GL15.GL_ELEMENT_ARRAY_BUFFER, elementBufferId);
            pattern.limit(quads * 6);
            pattern.position(0);
            GL15.glBufferData(
                GL15.GL_ELEMENT_ARRAY_BUFFER, pattern, GL15.GL_STATIC_DRAW);
            uploadedQuads = quads;
        }
    }
}
