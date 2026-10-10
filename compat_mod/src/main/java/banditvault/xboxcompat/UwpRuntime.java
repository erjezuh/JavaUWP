package banditvault.xboxcompat;

/** Only the packaged launcher enables these native API guards. */
public final class UwpRuntime {
    private UwpRuntime() {
    }

    public static boolean isSandboxed() {
        return Boolean.getBoolean("banditvault.uwp");
    }
}
