package banditvault.legacyforge;

import com.sun.jna.Library;
import com.sun.jna.Native;
import com.sun.jna.Pointer;
import com.sun.jna.ptr.DoubleByReference;
import com.sun.jna.ptr.IntByReference;
import org.lwjgl.LWJGLException;

/**
 * Connects legacy LWJGL 2 to the launcher's existing CoreWindow/UWP GLFW shim.
 *
 * The shim owns the actual UWP WGL context. LWJGL 2 only needs a current
 * OpenGL context on the Minecraft thread; its GL entry-point resolver can then
 * use the Mesa opengl32.dll already loaded by the process.
 */
public final class LegacyUwpGlfwBridge {
    private interface GlfwLibrary extends Library {
        int glfwInit();
        Pointer glfwCreateWindow(int width, int height, String title, Pointer monitor, Pointer share);
        void glfwDestroyWindow(Pointer window);
        void glfwMakeContextCurrent(Pointer window);
        void glfwSwapBuffers(Pointer window);
        void glfwSwapInterval(int interval);
        int glfwWindowShouldClose(Pointer window);
        Pointer glfwGetCurrentContext();
        void glfwSetWindowShouldClose(Pointer window, int value);
        void glfwSetWindowTitle(Pointer window, String title);
        void glfwPollEvents();
        void glfwGetFramebufferSize(
            Pointer window, IntByReference width, IntByReference height);
        long BanditShimPresentedFrames();
        void glfwBanditGetKeyStates(byte[] states);
        int glfwGetMouseButton(Pointer window, int button);
        void glfwGetCursorPos(Pointer window, DoubleByReference x, DoubleByReference y);
        void glfwGetWindowSize(Pointer window, IntByReference width, IntByReference height);
        int glfwBanditReadChars(int[] out, int maxCount);
        void glfwBanditGetAndClearScroll(DoubleByReference x, DoubleByReference y);
        void glfwSetInputMode(Pointer window, int mode, int value);
    }

    /** GLFW input-mode constants (match the shim's glfw3.h). */
    public static final int GLFW_CURSOR = 0x00033001;
    public static final int GLFW_CURSOR_NORMAL = 0x00034001;
    public static final int GLFW_CURSOR_DISABLED = 0x00034003;

    private static GlfwLibrary library;
    private static Pointer window;
    private static boolean initialized;
    private static Thread fpsLogger;

    private LegacyUwpGlfwBridge() {
    }

    public static synchronized void createWindow(int width, int height, String title)
        throws LWJGLException {
        load();

        if (!initialized) {
            if (library.glfwInit() == 0) {
                throw new LWJGLException("UWP GLFW shim glfwInit() failed");
            }
            initialized = true;
        }

        if (window == null) {
            window = library.glfwCreateWindow(
                width,
                height,
                title != null ? title : "Minecraft",
                null,
                null);
            if (window == null) {
                throw new LWJGLException("UWP GLFW shim glfwCreateWindow() failed");
            }
        }

        makeCurrent();
        int[] fb = getFramebufferSize();
        System.err.println(
            "[BanditVault] Legacy LWJGL window routed through UWP GLFW/CoreWindow shim: "
                + fb[0] + "x" + fb[1]);
        startPresentBenchmark();
    }

    /**
     * Logs presented-frames-per-second to stderr (mc_launch.log) so each test
     * run doubles as a benchmark record. Samples every 60s after a 45s grace
     * period, then stops so long sessions do not spam the log.
     */
    private static synchronized void startPresentBenchmark() {
        if (fpsLogger != null) {
            return;
        }
        fpsLogger = new Thread(new Runnable() {
            @Override
            public void run() {
                int samples = 0;
                long lastFrames = 0;
                long lastMs = System.currentTimeMillis();
                while (samples < 20) {
                    try {
                        Thread.sleep(samples == 0 ? 45000L : 60000L);
                    } catch (InterruptedException e) {
                        return;
                    }
                    synchronized (LegacyUwpGlfwBridge.class) {
                        if (library == null || window == null) {
                            return;
                        }
                        long frames;
                        try {
                            frames = library.BanditShimPresentedFrames();
                        } catch (Throwable error) {
                            return;
                        }
                        long now = System.currentTimeMillis();
                        long elapsed = Math.max(1L, now - lastMs);
                        long fps = (frames - lastFrames) * 1000L / elapsed;
                        System.err.println(
                            "[BanditVault] Present benchmark: " + fps + " fps (frames="
                                + frames + ", sample=" + (samples + 1) + "/20)");
                        lastFrames = frames;
                        lastMs = now;
                        ++samples;
                    }
                }
            }
        }, "BanditVault PresentBenchmark");
        fpsLogger.setDaemon(true);
        fpsLogger.start();
    }

    /** Real size, in raw pixels, of the CoreWindow surface Mesa presents to. */
    public static synchronized int[] getFramebufferSize() {
        try {
            load();
        } catch (Throwable error) {
            return new int[] {0, 0};
        }
        if (!initialized) {
            if (library.glfwInit() == 0) {
                return new int[] {0, 0};
            }
            initialized = true;
        }
        IntByReference width = new IntByReference(0);
        IntByReference height = new IntByReference(0);
        library.glfwGetFramebufferSize(window, width, height);
        return new int[] {width.getValue(), height.getValue()};
    }

    /** Copies the shim's GLFW key states (512 bytes, indexed by GLFW key code). */
    public static synchronized void getKeyStates(byte[] states) {
        if (library == null || states == null) {
            return;
        }
        try {
            library.glfwBanditGetKeyStates(states);
        } catch (Throwable error) {
            // Keep the poll path alive if a native call fails.
        }
    }

    public static synchronized boolean isMouseButtonPressed(int button) {
        return library != null &&
            window != null &&
            library.glfwGetMouseButton(window, button) != 0;
    }

    /** Cursor position in the shim's window coordinate space. */
    public static synchronized double[] getCursorPos() {
        DoubleByReference x = new DoubleByReference(0.0);
        DoubleByReference y = new DoubleByReference(0.0);
        if (library != null) {
            library.glfwGetCursorPos(window, x, y);
        }
        return new double[] {x.getValue(), y.getValue()};
    }

    public static synchronized int[] getWindowSize() {
        IntByReference width = new IntByReference(0);
        IntByReference height = new IntByReference(0);
        if (library != null) {
            library.glfwGetWindowSize(window, width, height);
        }
        return new int[] {width.getValue(), height.getValue()};
    }

    /**
     * Mirrors Minecraft's mouse grab state into the shim's cursor mode so the
     * shim knows when absolute pointer positions may steer the cursor (menus)
     * and when only deltas matter (gameplay). Without this the shim stays in
     * its default grabbed mode forever and menus get delta-only tracking.
     */
    public static synchronized void setCursorGrabbed(boolean grabbed) {
        if (library == null || window == null) {
            return;
        }
        try {
            library.glfwSetInputMode(
                window, GLFW_CURSOR,
                grabbed ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
        } catch (Throwable error) {
            // Keep the poll path alive if a native call fails.
        }
    }

    /** Drains pending Unicode code points from the shim's char queue. */
    public static synchronized int[] readChars(int maxCount) {
        if (library == null || maxCount <= 0) {
            return new int[0];
        }
        int[] out = new int[maxCount];
        int read;
        try {
            read = library.glfwBanditReadChars(out, maxCount);
        } catch (Throwable error) {
            return new int[0];
        }
        if (read <= 0) {
            return new int[0];
        }
        int[] result = new int[read];
        System.arraycopy(out, 0, result, 0, read);
        return result;
    }

    /** Read-and-clear of the accumulated scroll (in wheel notches). */
    public static synchronized double[] getAndClearScroll() {
        DoubleByReference x = new DoubleByReference(0.0);
        DoubleByReference y = new DoubleByReference(0.0);
        if (library != null) {
            library.glfwBanditGetAndClearScroll(x, y);
        }
        return new double[] {x.getValue(), y.getValue()};
    }

    public static synchronized void destroyWindow() {
        if (library != null && window != null) {
            library.glfwDestroyWindow(window);
        }
        window = null;
    }

    public static synchronized void makeCurrent() throws LWJGLException {
        load();
        if (window == null) {
            throw new LWJGLException("UWP GLFW window has not been created");
        }
        library.glfwMakeContextCurrent(window);
    }

    public static synchronized void releaseCurrent() {
        if (library != null) {
            library.glfwMakeContextCurrent(null);
        }
    }

    public static synchronized boolean isCurrent() {
        // The shim itself tracks the owning thread. Calling glfwMakeContextCurrent
        // is deliberately avoided here because querying it must not change state.
        return library != null && window != null && library.glfwGetCurrentContext() != null;
    }

    public static synchronized void swapBuffers() throws LWJGLException {
        if (library == null || window == null) {
            throw new LWJGLException("UWP GLFW window is unavailable");
        }
        library.glfwSwapBuffers(window);
    }

    public static synchronized void swapInterval(int interval) {
        if (library != null) {
            library.glfwSwapInterval(interval);
        }
    }

    public static synchronized void pollEvents() {
        if (library != null) {
            library.glfwPollEvents();
        }
    }

    public static synchronized boolean shouldClose() {
        // JNA defines Pointer.NULL as literally null ("Convenience constant,
        // same as null"), so it must never be dereferenced. JNA already maps a
        // NULL native return to a Java null reference; window != null covers it.
        return library != null &&
            window != null &&
            library.glfwWindowShouldClose(window) != 0;
    }

    public static synchronized void setShouldClose(boolean close) {
        if (library != null && window != null) {
            library.glfwSetWindowShouldClose(window, close ? 1 : 0);
        }
    }

    public static synchronized void setTitle(String title) {
        if (library != null && window != null) {
            library.glfwSetWindowTitle(window, title != null ? title : "Minecraft");
        }
    }

    private static synchronized void load() throws LWJGLException {
        if (library != null) {
            return;
        }

        try {
            library = Native.loadLibrary("glfw", GlfwLibrary.class);
            System.err.println(
                "[BanditVault] Loaded UWP GLFW shim through JNA from java.library.path");
        } catch (Throwable error) {
            throw new LWJGLException(
                "Could not load UWP GLFW shim through JNA: " + error);
        }
    }
}
