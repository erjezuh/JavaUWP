package banditvault.audio;

/**
 * Tiny JNI bridge to the glfw shim's bandit microphone capture
 * (WASAPI shared-mode capture on Xbox/UWP).
 */
final class BanditMic {
    private static boolean libraryLoaded;
    private static String loadError = "not attempted";

    static {
        try {
            System.loadLibrary("glfw");
            libraryLoaded = true;
            loadError = "";
        } catch (Throwable t) {
            libraryLoaded = false;
            loadError = String.valueOf(t);
            System.out.println("[BanditVault] audio input: glfw.dll not loaded (" + t + "); microphone unavailable");
        }
    }

    private BanditMic() {}

    static boolean isAvailable() {
        return libraryLoaded;
    }

    static String loadError() {
        return loadError;
    }

    static native int nativeOpen(int sampleRate, int channels, int bitsPerSample);

    static native int nativeAvailable();

    static native int nativeRead(byte[] buf, int offset, int length, int timeoutMs);

    static native void nativeClose();
}
