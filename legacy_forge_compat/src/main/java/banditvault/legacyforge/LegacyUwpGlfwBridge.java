package banditvault.legacyforge;

import com.sun.jna.Library;
import com.sun.jna.Native;
import com.sun.jna.Pointer;
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
    }

    private static GlfwLibrary library;
    private static Pointer window;
    private static boolean initialized;

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
        System.err.println(
            "[BanditVault] Legacy LWJGL window routed through UWP GLFW/CoreWindow shim: "
                + width + "x" + height);
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
        return library != null &&
            window != null &&
            !Pointer.NULL.equals(window) &&
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
