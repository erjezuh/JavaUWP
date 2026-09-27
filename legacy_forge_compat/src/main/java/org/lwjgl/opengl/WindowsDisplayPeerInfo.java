package org.lwjgl.opengl;

import java.nio.ByteBuffer;
import org.lwjgl.LWJGLException;

/**
 * UWP-safe PeerInfo for the legacy LWJGL 2 display backend.
 *
 * No HWND/HDC is created here. The actual drawable is the launcher's
 * CoreWindow and its WGL context, owned by LegacyUwpGlfwBridge.
 */
final class WindowsDisplayPeerInfo extends PeerInfo {
    WindowsDisplayPeerInfo(boolean egl) throws LWJGLException {
        super(ByteBuffer.allocateDirect(16));
    }

    void initDC(long hwnd, long hdc) throws LWJGLException {
        // No desktop HWND/HDC exists on the UWP path.
    }

    @Override
    protected void doLockAndInitHandle() throws LWJGLException {
        // The direct buffer is already initialized and remains valid.
    }

    @Override
    protected void doUnlock() throws LWJGLException {
        // No native peer lock is required.
    }
}
