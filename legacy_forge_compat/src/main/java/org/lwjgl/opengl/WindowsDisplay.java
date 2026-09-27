package org.lwjgl.opengl;

import java.awt.Canvas;
import java.nio.ByteBuffer;
import java.nio.FloatBuffer;
import java.nio.IntBuffer;

import banditvault.legacyforge.LegacyUwpGlfwBridge;
import org.lwjgl.LWJGLException;

/**
 * Minimal LWJGL 2 DisplayImplementation for Xbox/UWP.
 *
 * The stock LWJGL 2 WindowsDisplay implementation is built around HWND/HDC,
 * RegisterClassEx/CreateWindowEx and WGL drawable management. The launcher
 * already owns a CoreWindow and a Mesa WGL context through its UWP GLFW shim,
 * so this implementation preserves the Display API while delegating the actual
 * window/context lifecycle to that shim.
 */
final class WindowsDisplay implements DisplayImplementation {
    private static final int WIDTH = 1920;
    private static final int HEIGHT = 1080;

    private DisplayMode currentMode = new DisplayMode(WIDTH, HEIGHT);
    private Canvas parent;
    private boolean closeRequested;
    private boolean visible = true;
    private boolean active = true;
    private boolean dirty;
    private boolean resized;
    private boolean created;
    private int width = WIDTH;
    private int height = HEIGHT;
    private int x;
    private int y;

    @Override
    public void createWindow(
        DrawableLWJGL drawable,
        DisplayMode mode,
        Canvas parent,
        int x,
        int y) throws LWJGLException {

        this.parent = parent;
        this.x = x;
        this.y = y;
        // Minecraft requests its legacy default mode (854x480), but Mesa
        // presents the full-screen UWP CoreWindow surface. Rendering 854x480
        // into that surface shows the game as a tiny corner. Pass 0x0 so the
        // shim keeps tracking the CoreWindow size, then adopt the real raw
        // framebuffer size as the display mode.
        LegacyUwpGlfwBridge.createWindow(0, 0, Display.getTitle());
        int[] fb = LegacyUwpGlfwBridge.getFramebufferSize();
        this.width = fb[0] > 0 ? fb[0] : WIDTH;
        this.height = fb[1] > 0 ? fb[1] : HEIGHT;
        this.currentMode = new DisplayMode(width, height);
        this.closeRequested = false;
        this.visible = true;
        this.active = true;
        this.dirty = true;
        // Kick LWJGL's Display.update() -> wasResized() handshake so Minecraft
        // adopts this size on its first frame instead of its 854x480 default.
        this.resized = true;
        created = true;

        System.err.println(
            "[BanditVault] LWJGL 2 WindowsDisplay using CoreWindow UWP backend "
                + width + "x" + height);
    }

    @Override
    public void destroyWindow() {
        LegacyUwpGlfwBridge.destroyWindow();
        created = false;
        visible = false;
        active = false;
    }

    @Override
    public void switchDisplayMode(DisplayMode mode) throws LWJGLException {
        if (mode != null) {
            currentMode = mode;
            width = mode.getWidth();
            height = mode.getHeight();
            resized = true;
        }
    }

    @Override
    public void resetDisplayMode() {
        // The UWP CoreWindow owns the display mode.
    }

    @Override
    public int getGammaRampLength() {
        return 0;
    }

    @Override
    public void setGammaRamp(FloatBuffer gammaRamp) throws LWJGLException {
        // Gamma control is not supported through the UWP CoreWindow path.
    }

    @Override
    public String getAdapter() {
        return "BanditVault UWP Mesa";
    }

    @Override
    public String getVersion() {
        return "Mesa WGL UWP";
    }

    @Override
    public DisplayMode init() throws LWJGLException {
        // The CoreWindow surface is the real display on Xbox/UWP. Report its
        // raw-pixel size so desktop/fullscreen code paths use sane values.
        int[] fb = LegacyUwpGlfwBridge.getFramebufferSize();
        width = fb[0] > 0 ? fb[0] : WIDTH;
        height = fb[1] > 0 ? fb[1] : HEIGHT;
        currentMode = new DisplayMode(width, height);
        System.err.println(
            "[BanditVault] LWJGL 2 display surface resolved to " + width + "x" + height);
        return currentMode;
    }

    @Override
    public void setTitle(String title) {
        LegacyUwpGlfwBridge.setTitle(title);
    }

    @Override
    public boolean isCloseRequested() {
        boolean saved = closeRequested || LegacyUwpGlfwBridge.shouldClose();
        closeRequested = false;
        return saved;
    }

    @Override
    public boolean isVisible() {
        return visible;
    }

    @Override
    public boolean isActive() {
        return active;
    }

    @Override
    public boolean isDirty() {
        boolean saved = dirty;
        dirty = false;
        return saved;
    }

    @Override
    public PeerInfo createPeerInfo(
        PixelFormat pixelFormat,
        ContextAttribs attribs) throws LWJGLException {
        return new WindowsDisplayPeerInfo(false);
    }

    @Override
    public void update() {
        LegacyUwpGlfwBridge.pollEvents();
        if (LegacyUwpGlfwBridge.shouldClose()) {
            closeRequested = true;
            visible = false;
            active = false;
        } else {
            visible = true;
            active = true;
        }
        dirty = true;
        // resized is consumed only by wasResized() (LWJGL Display.update()
        // samples it every frame); clearing it here would eat the signal.
    }

    @Override
    public void reshape(int x, int y, int width, int height) {
        this.x = x;
        this.y = y;
        if (width > 0) {
            this.width = width;
        }
        if (height > 0) {
            this.height = height;
        }
        resized = true;
        dirty = true;
    }

    @Override
    public DisplayMode[] getAvailableDisplayModes() throws LWJGLException {
        return new DisplayMode[] {new DisplayMode(width, height)};
    }

    @Override
    public int getPbufferCapabilities() {
        return 0;
    }

    @Override
    public boolean isBufferLost(PeerInfo handle) {
        return false;
    }

    @Override
    public PeerInfo createPbuffer(
        int width,
        int height,
        PixelFormat pixelFormat,
        ContextAttribs attribs,
        IntBuffer pixelFormatCaps,
        IntBuffer pBufferAttribs) throws LWJGLException {
        throw new LWJGLException("Pbuffers are not supported by the Xbox/UWP legacy backend");
    }

    @Override
    public void setPbufferAttrib(PeerInfo handle, int attrib, int value) {
    }

    @Override
    public void bindTexImageToPbuffer(PeerInfo handle, int buffer) {
    }

    @Override
    public void releaseTexImageFromPbuffer(PeerInfo handle, int buffer) {
    }

    @Override
    public int setIcon(ByteBuffer[] icons) {
        return 0;
    }

    @Override
    public void setResizable(boolean resizable) {
    }

    @Override
    public boolean wasResized() {
        boolean saved = resized;
        resized = false;
        return saved;
    }

    @Override
    public int getWidth() {
        return width;
    }

    @Override
    public int getHeight() {
        return height;
    }

    @Override
    public int getX() {
        return x;
    }

    @Override
    public int getY() {
        return y;
    }

    @Override
    public float getPixelScaleFactor() {
        return 1.0f;
    }

    // ---------------------------------------------------------------------
    // InputImplementation. The native UWP input path is owned by the launcher's
    // CoreWindow/GameInput shim. These methods keep LWJGL 2's legacy input
    // objects alive without invoking the unsupported Win32 input backend.
    // ---------------------------------------------------------------------

    @Override
    public boolean hasWheel() {
        return true;
    }

    @Override
    public int getButtonCount() {
        return 8;
    }

    @Override
    public void createMouse() throws LWJGLException {
    }

    @Override
    public void destroyMouse() {
    }

    @Override
    public void pollMouse(IntBuffer coordBuffer, ByteBuffer buttons) {
        if (coordBuffer != null && coordBuffer.capacity() >= 3) {
            coordBuffer.put(0, 0);
            coordBuffer.put(1, 0);
            coordBuffer.put(2, 0);
        }
        if (buttons != null) {
            for (int i = 0; i < buttons.capacity(); i++) {
                buttons.put(i, (byte)0);
            }
        }
    }

    @Override
    public void readMouse(ByteBuffer buffer) {
        // No buffered events yet. Leave the event buffer empty.
    }

    @Override
    public void grabMouse(boolean grab) {
    }

    @Override
    public int getNativeCursorCapabilities() {
        return 0;
    }

    @Override
    public void setCursorPosition(int x, int y) {
    }

    @Override
    public void setNativeCursor(Object handle) throws LWJGLException {
    }

    @Override
    public int getMinCursorSize() {
        return 1;
    }

    @Override
    public int getMaxCursorSize() {
        return 1;
    }

    @Override
    public void createKeyboard() throws LWJGLException {
    }

    @Override
    public void destroyKeyboard() {
    }

    @Override
    public void pollKeyboard(ByteBuffer keyDownBuffer) {
        if (keyDownBuffer != null) {
            while (keyDownBuffer.hasRemaining()) {
                keyDownBuffer.put((byte)0);
            }
            keyDownBuffer.rewind();
        }
    }

    @Override
    public void readKeyboard(ByteBuffer buffer) {
        if (buffer != null) {
            while (buffer.hasRemaining()) {
                buffer.put((byte)0);
            }
            buffer.flip();
        }
    }

    @Override
    public Object createCursor(
        int width,
        int height,
        int xHotspot,
        int yHotspot,
        int numImages,
        IntBuffer images,
        IntBuffer delays) throws LWJGLException {
        return null;
    }

    @Override
    public void destroyCursor(Object cursorHandle) {
    }

    @Override
    public boolean isInsideWindow() {
        return created && active;
    }
}
