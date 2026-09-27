package org.lwjgl.opengl;

import java.nio.ByteBuffer;
import org.lwjgl.LWJGLException;

/**
 * UWP-safe PeerInfo used only by the patched legacy LaunchClassLoader.
 * The actual drawable/context is owned by the CoreWindow GLFW shim.
 */
final class WindowsDisplayPeerInfo extends PeerInfo {
    private final boolean egl;

    WindowsDisplayPeerInfo(boolean egl) throws LWJGLException {
        super(ByteBuffer.allocateDirect(16));
        this.egl = egl;
        if (egl) {
            org.lwjgl.opengles.GLContext.loadOpenGLLibrary();
        } else {
            GLContext.loadOpenGLLibrary();
        }
    }

    void initDC(long hwnd, long hdc) throws LWJGLException {
        // UWP has no Win32 HWND/HDC drawable for this path.
    }

    protected void doLockAndInitHandle() throws LWJGLException {
        // The direct ByteBuffer handle is permanent for the lifetime of PeerInfo.
    }

    protected void doUnlock() throws LWJGLException {
        // No native lock is required.
    }

    public void destroy() {
        super.destroy();
        if (egl) {
            org.lwjgl.opengles.GLContext.unloadOpenGLLibrary();
        } else {
            GLContext.unloadOpenGLLibrary();
        }
    }
}
