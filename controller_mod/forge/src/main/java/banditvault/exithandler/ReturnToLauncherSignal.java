package banditvault.exithandler;

/**
 * Marker exception the native launcher recognises: the Minecraft main loop
 * finished (the player quit the game) and control should return to the
 * launcher menu instead of exiting the whole app process.
 */
public final class ReturnToLauncherSignal extends RuntimeException {
    public static final String MARKER = "BanditVaultReturnToLauncher";

    public ReturnToLauncherSignal() {
        super(MARKER);
    }

    @Override
    public synchronized Throwable fillInStackTrace() {
        return this;
    }
}
