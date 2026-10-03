package banditvault.legacyforge;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.IntBuffer;

import org.lwjgl.opengl.GL11;
import org.lwjgl.opengl.GL15;
import org.lwjgl.opengl.GL32;

/**
 * Turns Minecraft 1.12.2's GL_QUADS draws into native triangle draws and
 * executes multi draws as minimal single-draw sequences.
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
 * Draw-call budget: this backend taxes every GL call, so a converted draw
 * costs exactly ONE GL call - glDrawElementsBaseVertex - the same count as
 * the glDrawArrays it replaces. The element buffer is created once and LEFT
 * BOUND between draws (nothing else in vanilla 1.12/OptiFine touches
 * GL_ELEMENT_ARRAY_BUFFER); MC_IBO_UNBIND=1 restores a per-draw unbind for
 * mods that bind their own element buffers or use client-side
 * glDrawElements. Adjacent glMultiDrawArrays ranges (same mode, contiguous
 * vertices) are merged into single draws before submission.
 *
 * Kill switches (read once at class init): MC_QUADS_AS_TRIANGLES=0 (forward
 * quads unchanged), MC_IBO_UNBIND=1 (safe element-buffer unbinding).
 */
public final class LegacyGlSanitizer {
    private static final int GL_QUADS = 0x0007;

    private static final boolean quadsEnabled =
        readEnv("MC_QUADS_AS_TRIANGLES", true);
    private static final boolean unbindEachDraw =
        readEnv("MC_IBO_UNBIND", false) || nothiriumPresent();

    /** Nothirium rewrites chunk rendering and binds its own element buffers. */
    private static boolean nothiriumPresent() {
        try {
            java.io.File mods = new java.io.File(System.getProperty("user.dir", "."), "mods");
            java.io.File[] files = mods.listFiles();
            if (files == null) {
                return false;
            }
            for (java.io.File file : files) {
                if (file.getName().toLowerCase(java.util.Locale.ROOT).contains("nothirium")) {
                    return true;
                }
            }
        } catch (Throwable ignored) {
        }
        return false;
    }

    static {
        // Build/config fingerprint in mc_launch.log: proves which draw path
        // is active and which kill switches were set at class-init time.
        System.err.println(
            "[BanditVault] Draw path config: quadsEnabled=" + quadsEnabled
            + " unbindEachDraw=" + unbindEachDraw);
    }

    private static int elementBufferId;
    private static IntBuffer pattern;
    private static int patternQuads;
    private static int uploadedQuads;
    private static boolean iboBound;
    private static boolean loggedFirstConversion;
    private static boolean failureLogged;
    private static volatile boolean conversionBroken;
    private static int dispatchTraceLeft = 16;
    private static boolean multiTraceDone;
    private static volatile boolean shadersActive;
    private static long shadersRefreshNanos;
    private static java.lang.reflect.Field shaderPackLoadedField;

    private LegacyGlSanitizer() {
    }

    private static boolean readEnv(String name, boolean defaultValue) {
        try {
            String value = System.getenv(name);
            return value == null ? defaultValue : !value.equals("0");
        } catch (Throwable ignored) {
            return defaultValue;
        }
    }

    /**
     * True while an OptiFine shader pack is loaded. Shaders bind their own
     * element buffers and issue client-side glDrawElements; a leftover static
     * pattern IBO from this class would be interpreted as an index offset and
     * renders black geometry with GL_INVALID_OPERATION (1282) spam. While a
     * pack is active the pattern IBO is unbound after every converted draw.
     * Polled at most once a second; reflection failure means "not active".
     */
    private static boolean shadersPackLoaded() {
        final long now = System.nanoTime();
        if (now - shadersRefreshNanos < 1000000000L) {
            return shadersActive;
        }
        shadersRefreshNanos = now;
        boolean loaded = false;
        try {
            if (shaderPackLoadedField == null) {
                final Class<?> cls = Class.forName("shadersmod.client.Shaders", false,
                    LegacyGlSanitizer.class.getClassLoader());
                shaderPackLoadedField = cls.getField("shaderPackLoaded");
            }
            loaded = shaderPackLoadedField.getBoolean(null);
        } catch (Throwable ignored) {
            loaded = false;
        }
        if (loaded != shadersActive) {
            shadersActive = loaded;
            System.err.println(
                "[BanditVault] OptiFine shaders active=" + loaded
                + " (per-draw IBO unbind " + (loaded ? "enabled" : "disabled") + ")");
        }
        return loaded;
    }

    /**
     * Handles a glDrawArrays call. Returns true when the draw was fully
     * performed as indexed triangles; false means the caller must forward the
     * original glDrawArrays (non-quad modes, disabled, or any failure).
     */
    public static boolean dispatchDrawArrays(int mode, int first, int count) {
        boolean handled = false;
        Throwable failure = null;
        if (quadsEnabled && !conversionBroken && mode == GL_QUADS && count >= 4) {
            try {
                drawQuadsAsTriangles(first, count);
                handled = true;
            } catch (Throwable failed) {
                failure = failed;
                // The failure is deterministic (buffer/GL setup), so retrying
                // every draw would cost one exception per call and make fps
                // worse than plain quads. Fall back permanently after logging.
                conversionBroken = true;
            }
        }
        if (dispatchTraceLeft > 0) {
            dispatchTraceLeft--;
            System.err.println(
                "[BanditVault] draw dispatch: mode=" + mode + " first=" + first
                + " count=" + count + " -> "
                + (handled ? "triangles" : "forwarded")
                + (failure != null ? " (" + failure + ")" : ""));
        }
        if (failure != null && !failureLogged) {
            failureLogged = true;
            System.err.println(
                "[BanditVault] quads conversion failed, forwarding to original:");
            failure.printStackTrace();
        }
        return handled;
    }

    /**
     * Executes a glMultiDrawArrays as validated single draws. Adjacent
     * ranges are merged first (same mode, contiguous vertices), so a packed
     * OptiFine region becomes a handful of draws instead of one per chunk
     * segment.
     */
    public static void multiDrawAsSingles(int mode, IntBuffer first, IntBuffer count) {
        try {
            if (first == null || count == null) {
                return;
            }
            final int firstBase = first.position();
            final int countBase = count.position();
            final int n = Math.min(first.remaining(), count.remaining());
            if (!multiTraceDone) {
                multiTraceDone = true;
                System.err.println(
                    "[BanditVault] multi-draw seen: ranges=" + n
                    + " first0=" + (n > 0 ? first.get(firstBase) : -1)
                    + " count0=" + (n > 0 ? count.get(countBase) : -1));
            }
            int runStart = -1;
            int runCount = 0;
            for (int i = 0; i <= n; i++) {
                int f = 0;
                int c = 0;
                boolean valid = false;
                if (i < n) {
                    f = first.get(firstBase + i);
                    c = count.get(countBase + i);
                    valid = f >= 0 && c > 0 && f <= Integer.MAX_VALUE - c;
                }
                if (valid && runStart >= 0 && f == runStart + runCount) {
                    runCount += c;
                    continue;
                }
                if (runStart >= 0) {
                    drawOne(mode, runStart, runCount);
                }
                runStart = valid ? f : -1;
                runCount = valid ? c : 0;
            }
        } catch (Throwable ignored) {
            // Never break rendering harder than the call we replace.
        }
    }

    private static void drawOne(int mode, int first, int count) {
        if (!dispatchDrawArrays(mode, first, count)) {
            GL11.glDrawArrays(mode, first, count);
        }
    }

    /**
     * Exactly one GL call per converted draw: the element buffer is bound
     * once (creation/upload) and left bound unless MC_IBO_UNBIND=1.
     */
    private static void drawQuadsAsTriangles(int first, int count) {
        final int quads = count / 4;
        ensurePattern(quads);
        ensureElementBuffer(quads);
        if (!iboBound) {
            GL15.glBindBuffer(GL15.GL_ELEMENT_ARRAY_BUFFER, elementBufferId);
            iboBound = true;
        }
        if (!loggedFirstConversion) {
            loggedFirstConversion = true;
            System.err.println(
                "[BanditVault] quads -> triangles conversion active (first="
                + first + " count=" + count + ")");
        }
        GL32.glDrawElementsBaseVertex(
            GL11.GL_TRIANGLES, quads * 6, GL11.GL_UNSIGNED_INT, 0L, first);
        if (unbindEachDraw || shadersPackLoaded()) {
            GL15.glBindBuffer(GL15.GL_ELEMENT_ARRAY_BUFFER, 0);
            iboBound = false;
        }
    }

    private static synchronized void ensurePattern(int quads) {
        if (pattern != null && patternQuads >= quads) {
            return;
        }
        // 1.5x headroom so pattern growth is amortized; no doubling overflow.
        int capacity = Math.max(quads + quads / 2, 4096);
        // LWJGL 2 requires DIRECT buffers for glBufferData (heap
        // IntBuffer.allocate throws "IntBuffer is not direct").
        final IntBuffer grown = ByteBuffer
            .allocateDirect(capacity * 6 * 4)
            .order(ByteOrder.nativeOrder())
            .asIntBuffer();
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
            // Deleting the bound buffer resets the binding to zero.
            iboBound = false;
        }
        uploadedQuads = 0;
    }

    private static synchronized void ensureElementBuffer(int quads) {
        if (elementBufferId == 0) {
            elementBufferId = GL15.glGenBuffers();
            uploadedQuads = 0;
            iboBound = false;
        }
        if (uploadedQuads < patternQuads) {
            GL15.glBindBuffer(GL15.GL_ELEMENT_ARRAY_BUFFER, elementBufferId);
            iboBound = true;
            // Upload the whole pattern once; draws index a prefix of it.
            pattern.position(0);
            pattern.limit(patternQuads * 6);
            GL15.glBufferData(
                GL15.GL_ELEMENT_ARRAY_BUFFER, pattern, GL15.GL_STATIC_DRAW);
            uploadedQuads = patternQuads;
        }
    }
}
