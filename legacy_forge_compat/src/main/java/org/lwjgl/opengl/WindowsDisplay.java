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
        this.width = mode != null ? mode.getWidth() : WIDTH;
        this.height = mode != null ? mode.getHeight() : HEIGHT;
        this.currentMode = mode != null ? mode : new DisplayMode(WIDTH, HEIGHT);
        this.closeRequested = false;
        this.visible = true;
        this.active = true;
        this.dirty = true;
        this.resized = false;

        LegacyUwpGlfwBridge.createWindow(width, height, Display.getTitle());
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
        currentMode = new DisplayMode(WIDTH, HEIGHT);
        width = WIDTH;
        height = HEIGHT;
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
        resized = false;
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
        return new DisplayMode[] {new DisplayMode(WIDTH, HEIGHT)};
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
    // InputImplementation. Real keyboard/mouse support for the Xbox/UWP path.
    //
    // The shim tracks live CoreWindow/GameInput input state (GLFW key codes,
    // mouse buttons, cursor position, chars, scroll). This class polls that
    // state and translates it into the LWJGL 2 polling/event contracts:
    //  - pollKeyboard fills a 256-byte keyDown buffer (nonzero = pressed) and
    //    queues key transition events.
    //  - readKeyboard appends fixed 18-byte records: int key, byte state,
    //    int char, long nanos, byte repeat. It must NOT flip the buffer.
    //  - pollMouse fills coord[0..2] = x, y, dwheel. When the mouse is grabbed
    //    x/y are deltas; when free they are absolute display coordinates.
    //  - readMouse appends fixed 22-byte records: byte button, byte state,
    //    int x/dx, int y/dy, int dwheel, long nanos.
    // ---------------------------------------------------------------------

    // ---------------------------------------------------------------------
    // Input state: GLFW (shim) to LWJGL 2 Keyboard.KEY_* (DirectInput) keycode
    // mapping. Index = GLFW key code, value = LWJGL 2 keycode, -1 = unmapped.
    // ---------------------------------------------------------------------
    private static final short[] GLFW_TO_LWJGL = new short[512];
    private static final int KEYBOARD_SIZE = 256;
    private static final int BUTTON_COUNT = 8;
    private static final long REPEAT_DELAY_NANOS = 400_000_000L;
    private static final long REPEAT_RATE_NANOS = 35_000_000L;

    static {
        for (int i = 0; i < GLFW_TO_LWJGL.length; i++) {
            GLFW_TO_LWJGL[i] = -1;
        }
        // Letters (GLFW A..Z = 65..90 map to QWERTY DIK positions, not order).
        GLFW_TO_LWJGL[65] = 30;  // A
        GLFW_TO_LWJGL[66] = 48;  // B
        GLFW_TO_LWJGL[67] = 46;  // C
        GLFW_TO_LWJGL[68] = 32;  // D
        GLFW_TO_LWJGL[69] = 18;  // E
        GLFW_TO_LWJGL[70] = 33;  // F
        GLFW_TO_LWJGL[71] = 34;  // G
        GLFW_TO_LWJGL[72] = 35;  // H
        GLFW_TO_LWJGL[73] = 23;  // I
        GLFW_TO_LWJGL[74] = 36;  // J
        GLFW_TO_LWJGL[75] = 37;  // K
        GLFW_TO_LWJGL[76] = 38;  // L
        GLFW_TO_LWJGL[77] = 50;  // M
        GLFW_TO_LWJGL[78] = 49;  // N
        GLFW_TO_LWJGL[79] = 24;  // O
        GLFW_TO_LWJGL[80] = 25;  // P
        GLFW_TO_LWJGL[81] = 16;  // Q
        GLFW_TO_LWJGL[82] = 19;  // R
        GLFW_TO_LWJGL[83] = 31;  // S
        GLFW_TO_LWJGL[84] = 20;  // T
        GLFW_TO_LWJGL[85] = 22;  // U
        GLFW_TO_LWJGL[86] = 47;  // V
        GLFW_TO_LWJGL[87] = 17;  // W
        GLFW_TO_LWJGL[88] = 45;  // X
        GLFW_TO_LWJGL[89] = 21;  // Y
        GLFW_TO_LWJGL[90] = 44;  // Z
        // Number row.
        GLFW_TO_LWJGL[48] = 11;  // 0
        GLFW_TO_LWJGL[49] = 2;   // 1
        GLFW_TO_LWJGL[50] = 3;   // 2
        GLFW_TO_LWJGL[51] = 4;   // 3
        GLFW_TO_LWJGL[52] = 5;   // 4
        GLFW_TO_LWJGL[53] = 6;   // 5
        GLFW_TO_LWJGL[54] = 7;   // 6
        GLFW_TO_LWJGL[55] = 8;   // 7
        GLFW_TO_LWJGL[56] = 9;   // 8
        GLFW_TO_LWJGL[57] = 10;  // 9
        // Punctuation.
        GLFW_TO_LWJGL[32] = 57;  // SPACE
        GLFW_TO_LWJGL[39] = 40;  // APOSTROPHE
        GLFW_TO_LWJGL[44] = 51;  // COMMA
        GLFW_TO_LWJGL[45] = 12;  // MINUS
        GLFW_TO_LWJGL[46] = 52;  // PERIOD
        GLFW_TO_LWJGL[47] = 53;  // SLASH
        GLFW_TO_LWJGL[59] = 39;  // SEMICOLON
        GLFW_TO_LWJGL[61] = 13;  // EQUAL
        GLFW_TO_LWJGL[91] = 26;  // LEFT_BRACKET
        GLFW_TO_LWJGL[92] = 43;  // BACKSLASH
        GLFW_TO_LWJGL[93] = 27;  // RIGHT_BRACKET
        GLFW_TO_LWJGL[96] = 41;  // GRAVE_ACCENT
        // Editing / navigation.
        GLFW_TO_LWJGL[256] = 1;   // ESCAPE
        GLFW_TO_LWJGL[257] = 28;  // ENTER
        GLFW_TO_LWJGL[258] = 15;  // TAB
        GLFW_TO_LWJGL[259] = 14;  // BACKSPACE
        GLFW_TO_LWJGL[260] = 210; // INSERT
        GLFW_TO_LWJGL[261] = 211; // DELETE
        GLFW_TO_LWJGL[262] = 205; // RIGHT
        GLFW_TO_LWJGL[263] = 203; // LEFT
        GLFW_TO_LWJGL[264] = 208; // DOWN
        GLFW_TO_LWJGL[265] = 200; // UP
        GLFW_TO_LWJGL[266] = 201; // PAGE_UP
        GLFW_TO_LWJGL[267] = 209; // PAGE_DOWN
        GLFW_TO_LWJGL[268] = 199; // HOME
        GLFW_TO_LWJGL[269] = 207; // END
        // Locks / system.
        GLFW_TO_LWJGL[280] = 58;  // CAPS_LOCK
        GLFW_TO_LWJGL[281] = 70;  // SCROLL_LOCK
        GLFW_TO_LWJGL[282] = 69;  // NUM_LOCK
        GLFW_TO_LWJGL[283] = 183; // PRINT_SCREEN
        GLFW_TO_LWJGL[284] = 197; // PAUSE
        // Function keys.
        GLFW_TO_LWJGL[290] = 59;  // F1
        GLFW_TO_LWJGL[291] = 60;  // F2
        GLFW_TO_LWJGL[292] = 61;  // F3
        GLFW_TO_LWJGL[293] = 62;  // F4
        GLFW_TO_LWJGL[294] = 63;  // F5
        GLFW_TO_LWJGL[295] = 64;  // F6
        GLFW_TO_LWJGL[296] = 65;  // F7
        GLFW_TO_LWJGL[297] = 66;  // F8
        GLFW_TO_LWJGL[298] = 67;  // F9
        GLFW_TO_LWJGL[299] = 68;  // F10
        GLFW_TO_LWJGL[300] = 87;  // F11
        GLFW_TO_LWJGL[301] = 88;  // F12
        GLFW_TO_LWJGL[302] = 100; // F13
        GLFW_TO_LWJGL[303] = 101; // F14
        GLFW_TO_LWJGL[304] = 102; // F15
        // Keypad.
        GLFW_TO_LWJGL[320] = 82;  // KP_0
        GLFW_TO_LWJGL[321] = 79;  // KP_1
        GLFW_TO_LWJGL[322] = 80;  // KP_2
        GLFW_TO_LWJGL[323] = 81;  // KP_3
        GLFW_TO_LWJGL[324] = 75;  // KP_4
        GLFW_TO_LWJGL[325] = 76;  // KP_5
        GLFW_TO_LWJGL[326] = 77;  // KP_6
        GLFW_TO_LWJGL[327] = 71;  // KP_7
        GLFW_TO_LWJGL[328] = 72;  // KP_8
        GLFW_TO_LWJGL[329] = 73;  // KP_9
        GLFW_TO_LWJGL[330] = 83;  // KP_DECIMAL
        GLFW_TO_LWJGL[331] = 181; // KP_DIVIDE
        GLFW_TO_LWJGL[332] = 55;  // KP_MULTIPLY
        GLFW_TO_LWJGL[333] = 74;  // KP_SUBTRACT
        GLFW_TO_LWJGL[334] = 78;  // KP_ADD
        GLFW_TO_LWJGL[335] = 156; // KP_ENTER
        GLFW_TO_LWJGL[336] = 141; // KP_EQUAL
        // Modifiers.
        GLFW_TO_LWJGL[340] = 42;  // LEFT_SHIFT
        GLFW_TO_LWJGL[341] = 29;  // LEFT_CONTROL
        GLFW_TO_LWJGL[342] = 56;  // LEFT_ALT
        GLFW_TO_LWJGL[343] = 219; // LEFT_SUPER
        GLFW_TO_LWJGL[344] = 54;  // RIGHT_SHIFT
        GLFW_TO_LWJGL[345] = 157; // RIGHT_CONTROL
        GLFW_TO_LWJGL[346] = 184; // RIGHT_ALT
        GLFW_TO_LWJGL[347] = 220; // RIGHT_SUPER
        GLFW_TO_LWJGL[348] = 221; // MENU
    }

    private final byte[] glfwKeyStates = new byte[512];
    private final boolean[] keyDown = new boolean[KEYBOARD_SIZE];
    private final boolean[] lastKeyDown = new boolean[KEYBOARD_SIZE];
    private final long[] keyRepeatAt = new long[KEYBOARD_SIZE];
    private final int[] pendingKeyEvents = new int[512 * 4];
    private int pendingKeyEventCount;
    private final int[] queuedChars = new int[256];
    private int queuedCharCount;
    private final boolean[] buttonDown = new boolean[BUTTON_COUNT];
    private final boolean[] lastButtonDown = new boolean[BUTTON_COUNT];
    private final int[] pendingMouseEvents = new int[512 * 5];
    private int pendingMouseEventCount;
    private boolean mouseGrabbed;
    private int lastMouseX;
    private int lastMouseY;
    private int lastEventMouseX;
    private int lastEventMouseY;

    private void pushKeyboardEvent(int key, boolean state, int character, boolean repeat) {
        if ((pendingKeyEventCount + 1) * 4 > pendingKeyEvents.length) {
            return;
        }
        int index = pendingKeyEventCount * 4;
        pendingKeyEvents[index] = key;
        pendingKeyEvents[index + 1] = state ? 1 : 0;
        pendingKeyEvents[index + 2] = character;
        pendingKeyEvents[index + 3] = repeat ? 1 : 0;
        pendingKeyEventCount++;
    }

    private void pushMouseEvent(int button, boolean state, int xValue, int yValue, int dwheel) {
        if ((pendingMouseEventCount + 1) * 5 > pendingMouseEvents.length) {
            return;
        }
        int index = pendingMouseEventCount * 5;
        pendingMouseEvents[index] = button;
        pendingMouseEvents[index + 1] = state ? 1 : 0;
        pendingMouseEvents[index + 2] = xValue;
        pendingMouseEvents[index + 3] = yValue;
        pendingMouseEvents[index + 4] = dwheel;
        pendingMouseEventCount++;
    }

    private int pollQueuedChar() {
        if (queuedCharCount <= 0) {
            return 0;
        }
        int character = queuedChars[0];
        System.arraycopy(queuedChars, 1, queuedChars, 0, --queuedCharCount);
        return character;
    }

    @Override
    public boolean hasWheel() {
        return true;
    }

    @Override
    public int getButtonCount() {
        return BUTTON_COUNT;
    }

    @Override
    public void createMouse() throws LWJGLException {
    }

    @Override
    public void destroyMouse() {
    }

    @Override
    public void pollMouse(IntBuffer coordBuffer, ByteBuffer buttons) {
        double[] cursor = LegacyUwpGlfwBridge.getCursorPos();
        int[] windowSize = LegacyUwpGlfwBridge.getWindowSize();

        // The shim tracks the cursor in its window coordinate space, which is
        // created at the display size (1920x1080). Map defensively into
        // Minecraft's display coordinate space so GuiScreen math lines up.
        int rawX = (int)Math.round(cursor[0]);
        int rawY = (int)Math.round(cursor[1]);
        if (windowSize[0] > 0 && windowSize[0] != width) {
            rawX = (int)Math.round(cursor[0] * (double)width / (double)windowSize[0]);
        }
        if (windowSize[1] > 0 && windowSize[1] != height) {
            rawY = (int)Math.round(cursor[1] * (double)height / (double)windowSize[1]);
        }
        // LWJGL 2 Mouse coordinates use a bottom-left origin: Mouse.getY()
        // grows UPWARD (like OpenGL), and Minecraft itself converts GUI
        // coordinates with (displayHeight - Mouse.getY()). The shim tracks the
        // cursor top-down like the screen, so convert exactly once here at the
        // LWJGL boundary. Without this the in-game cursor moves inverted
        // up/down relative to the pointer.
        rawY = height - 1 - rawY;

        // Scrolling: wheel notches -> Windows-style +-120 per notch.
        double[] scroll = LegacyUwpGlfwBridge.getAndClearScroll();
        int dwheel = (int)Math.round(scroll[1] * 120.0);

        for (int button = 0; button < BUTTON_COUNT; button++) {
            buttonDown[button] = LegacyUwpGlfwBridge.isMouseButtonPressed(button);
        }

        int pollX;
        int pollY;
        if (mouseGrabbed) {
            pollX = rawX - lastMouseX;
            pollY = rawY - lastMouseY;
        } else {
            pollX = rawX;
            pollY = rawY;
        }
        lastMouseX = rawX;
        lastMouseY = rawY;

        // Buffered events follow Mouse.next()'s contract: deltas when grabbed,
        // absolute coordinates when free.
        int eventX = mouseGrabbed ? pollX : rawX;
        int eventY = mouseGrabbed ? pollY : rawY;

        if (eventX != lastEventMouseX || eventY != lastEventMouseY) {
            pushMouseEvent(-1, false, eventX, eventY, 0);
            lastEventMouseX = eventX;
            lastEventMouseY = eventY;
        }
        for (int button = 0; button < BUTTON_COUNT; button++) {
            if (buttonDown[button] != lastButtonDown[button]) {
                pushMouseEvent(button, buttonDown[button], eventX, eventY, 0);
                lastButtonDown[button] = buttonDown[button];
            }
        }
        if (dwheel != 0) {
            pushMouseEvent(-1, false, eventX, eventY, dwheel);
        }

        if (coordBuffer != null && coordBuffer.capacity() >= 3) {
            coordBuffer.put(0, pollX);
            coordBuffer.put(1, pollY);
            coordBuffer.put(2, dwheel);
        }
        if (buttons != null) {
            for (int button = 0; button < BUTTON_COUNT && button < buttons.capacity(); button++) {
                buttons.put(button, (byte)(buttonDown[button] ? 1 : 0));
            }
        }
    }

    @Override
    public void readMouse(ByteBuffer buffer) {
        // Append 22-byte records (byte button, byte state, int x, int y,
        // int dwheel, long nanos) until the buffer or the queue runs out.
        while (pendingMouseEventCount > 0 && buffer != null && buffer.remaining() >= 22) {
            buffer.put((byte)pendingMouseEvents[0]);
            buffer.put((byte)pendingMouseEvents[1]);
            buffer.putInt(pendingMouseEvents[2]);
            buffer.putInt(pendingMouseEvents[3]);
            buffer.putInt(pendingMouseEvents[4]);
            buffer.putLong(System.nanoTime());
            pendingMouseEventCount--;
            System.arraycopy(
                pendingMouseEvents, 5, pendingMouseEvents, 0, pendingMouseEventCount * 5);
        }
    }

    @Override
    public void grabMouse(boolean grab) {
        // Always mirror the grab state into the shim, even when our own flag
        // already matches: the shim starts in grabbed mode and needs the first
        // explicit ungrab (menu) call to enable absolute pointer tracking.
        LegacyUwpGlfwBridge.setCursorGrabbed(grab);
        if (grab == mouseGrabbed) {
            return;
        }
        mouseGrabbed = grab;
        double[] cursor = LegacyUwpGlfwBridge.getCursorPos();
        lastMouseX = (int)Math.round(cursor[0]);
        // Same bottom-left LWJGL space as pollMouse (see the conversion there).
        lastMouseY = height - 1 - (int)Math.round(cursor[1]);
        // In grab mode events report deltas starting from zero; in free mode
        // they report absolute coordinates.
        lastEventMouseX = mouseGrabbed ? 0 : lastMouseX;
        lastEventMouseY = mouseGrabbed ? 0 : lastMouseY;
    }

    @Override
    public int getNativeCursorCapabilities() {
        return 0;
    }

    @Override
    public void setCursorPosition(int x, int y) {
        // The physical/virtual cursor position is owned by the shim. Minecraft
        // recenters its own virtual mouse coordinates around this point; the
        // delta reporting stays consistent without moving anything.
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
        // Drain pending characters produced by real keyboards and the
        // on-screen keyboard into this cycle's queue.
        int[] chars = LegacyUwpGlfwBridge.readChars(256);
        queuedCharCount = 0;
        if (chars != null) {
            for (int i = 0; i < chars.length && queuedCharCount < queuedChars.length; i++) {
                queuedChars[queuedCharCount++] = chars[i];
            }
        }

        // Sample the shim's key state (indexed by GLFW key code) and translate
        // to LWJGL 2 keycodes.
        LegacyUwpGlfwBridge.getKeyStates(glfwKeyStates);
        for (int key = 0; key < KEYBOARD_SIZE; key++) {
            keyDown[key] = false;
        }
        for (int glfwKey = 0; glfwKey < glfwKeyStates.length; glfwKey++) {
            if (glfwKeyStates[glfwKey] == 0) {
                continue;
            }
            short lwjglKey = GLFW_TO_LWJGL[glfwKey];
            if (lwjglKey >= 0 && lwjglKey < KEYBOARD_SIZE) {
                keyDown[lwjglKey] = true;
            }
        }

        long now = System.nanoTime();
        for (int key = 0; key < KEYBOARD_SIZE; key++) {
            if (keyDown[key] != lastKeyDown[key]) {
                if (keyDown[key]) {
                    pushKeyboardEvent(key, true, pollQueuedChar(), false);
                    keyRepeatAt[key] = now + REPEAT_DELAY_NANOS;
                } else {
                    pushKeyboardEvent(key, false, 0, false);
                }
                lastKeyDown[key] = keyDown[key];
            } else if (keyDown[key] && now >= keyRepeatAt[key]) {
                pushKeyboardEvent(key, true, pollQueuedChar(), true);
                keyRepeatAt[key] = now + REPEAT_RATE_NANOS;
            }
        }

        // Characters not matched to a key transition (e.g. the on-screen
        // keyboard) still must reach Minecraft as text input. Emit them as
        // keyless events so GuiChat/GuiEditSign receive them.
        for (int i = 0; i < queuedCharCount; i++) {
            pushKeyboardEvent(0, true, queuedChars[i], false);
            pushKeyboardEvent(0, false, 0, false);
        }
        queuedCharCount = 0;

        if (keyDownBuffer != null) {
            for (int key = 0; key < KEYBOARD_SIZE && key < keyDownBuffer.capacity(); key++) {
                keyDownBuffer.put(key, (byte)(keyDown[key] ? 1 : 0));
            }
        }
    }

    @Override
    public void readKeyboard(ByteBuffer buffer) {
        // Append 18-byte records (int key, byte state, int char, long nanos,
        // byte repeat) until the buffer or the queue runs out. Do not flip;
        // Keyboard.read() flips afterwards.
        while (pendingKeyEventCount > 0 && buffer != null && buffer.remaining() >= 18) {
            buffer.putInt(pendingKeyEvents[0]);
            buffer.put((byte)pendingKeyEvents[1]);
            buffer.putInt(pendingKeyEvents[2]);
            buffer.putLong(System.nanoTime());
            buffer.put((byte)pendingKeyEvents[3]);
            pendingKeyEventCount--;
            System.arraycopy(
                pendingKeyEvents, 4, pendingKeyEvents, 0, pendingKeyEventCount * 4);
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
