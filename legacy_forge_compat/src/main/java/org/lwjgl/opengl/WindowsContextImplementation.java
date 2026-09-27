package org.lwjgl.opengl;

import java.nio.ByteBuffer;
import java.nio.IntBuffer;

import banditvault.legacyforge.LegacyUwpGlfwBridge;
import org.lwjgl.LWJGLException;

/**
 * Same binary name as stock LWJGL 2's WindowsContextImplementation.
 * It is supplied through legacy launcher-overrides so ContextGL creates
 * a context using the launcher's UWP WGL backend instead of Win32 WGL setup.
 */
final class WindowsContextImplementation implements ContextImplementation {
    public ByteBuffer create(PeerInfo peer_info, IntBuffer attribs, ByteBuffer shared_context_handle)
        throws LWJGLException {
        LegacyUwpGlfwBridge.makeCurrent();
        return ByteBuffer.allocateDirect(16);
    }

    public void swapBuffers() throws LWJGLException {
        LegacyUwpGlfwBridge.swapBuffers();
    }

    public void releaseDrawable(ByteBuffer context_handle) throws LWJGLException {
        // The UWP CoreWindow remains the drawable for the application lifetime.
    }

    public void releaseCurrentContext() throws LWJGLException {
        LegacyUwpGlfwBridge.releaseCurrent();
    }

    public void update(ByteBuffer context_handle) {
        // CoreWindow resize/state is owned by the UWP shim.
    }

    public void makeCurrent(PeerInfo peer_info, ByteBuffer handle) throws LWJGLException {
        LegacyUwpGlfwBridge.makeCurrent();
    }

    public boolean isCurrent(ByteBuffer handle) throws LWJGLException {
        return LegacyUwpGlfwBridge.isCurrent();
    }

    public void setSwapInterval(int value) {
        LegacyUwpGlfwBridge.swapInterval(value);
    }

    public void destroy(PeerInfo peer_info, ByteBuffer handle) throws LWJGLException {
        LegacyUwpGlfwBridge.destroyWindow();
    }
}
