package banditvault.xboxcompat;

import banditvault.xboxcompat.mixin.ControlifyGlfwMixin;
import banditvault.xboxcompat.mixin.ControlifyHidMixin;
import banditvault.xboxcompat.mixin.ControlifyLegacySdlMixin;
import banditvault.xboxcompat.mixin.ControlifySdlMixin;
import banditvault.xboxcompat.mixin.OshiGraphicsCardMixin;
import banditvault.xboxcompat.mixin.SodiumGraphicsAdapterMixin;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.util.Collection;
import java.util.concurrent.CompletableFuture;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

/** Callback unit tests; these do not pretend to run Minecraft or a GPU. */
public final class NativeGuardTests {
    private static void check(boolean condition, String message) {
        if (!condition) throw new AssertionError(message);
    }

    private static void call(Class<?> type, Object instance, String name, CallbackInfo callback) throws Exception {
        Class<?> callbackType = callback instanceof CallbackInfoReturnable
                ? CallbackInfoReturnable.class : CallbackInfo.class;
        Method method = type.getDeclaredMethod(name, callbackType);
        method.setAccessible(true);
        method.invoke(instance, callback);
    }

    private static Field hidField(String name) throws Exception {
        Field field = ControlifyHidMixin.class.getDeclaredField(name);
        field.setAccessible(true);
        return field;
    }

    private static void test(boolean uwp) throws Exception {
        if (uwp) System.setProperty("banditvault.uwp", "true");
        else System.clearProperty("banditvault.uwp");
        check(UwpRuntime.isSandboxed() == uwp, "UWP marker");

        CallbackInfo probe = new CallbackInfo("findAdapters", true);
        call(SodiumGraphicsAdapterMixin.class, null, "banditvault$skipDesktopAdapterProbe", probe);
        check(probe.isCancelled() == uwp, "Only UWP skips the desktop GPU probe");

        CallbackInfoReturnable<Collection<?>> adapters = new CallbackInfoReturnable<>("getAdapters", true);
        call(SodiumGraphicsAdapterMixin.class, null, "banditvault$unavailableDesktopAdapters", adapters);
        check(adapters.isCancelled() == uwp, "Adapter collection guard");
        if (uwp) check(adapters.getReturnValue() != null && adapters.getReturnValue().isEmpty(), "No fabricated adapters or null collection");

        CallbackInfoReturnable<Collection<?>> cards = new CallbackInfoReturnable<>("getGraphicsCards", true);
        call(OshiGraphicsCardMixin.class, new OshiGraphicsCardMixin() {}, "banditvault$skipWmiGraphicsProbe", cards);
        check(cards.isCancelled() == uwp, "WMI graphics inventory guard");
        if (uwp) check(cards.getReturnValue().isEmpty(), "WMI inventory unavailable");

        CallbackInfoReturnable<CompletableFuture<Boolean>> ask = new CallbackInfoReturnable<>("askNatives", true);
        call(ControlifyGlfwMixin.class, new ControlifyGlfwMixin() {}, "banditvault$useUwpGlfw", ask);
        check(ask.isCancelled() == uwp, "Controlify onboarding guard");
        if (uwp) check(ask.getReturnValue().isDone() && !ask.getReturnValue().join(), "Select the existing GLFW fallback");

        CallbackInfoReturnable<Boolean> sdl = new CallbackInfoReturnable<>("tryLoad", true);
        call(ControlifySdlMixin.class, null, "banditvault$skipDesktopSdl", sdl);
        check(sdl.isCancelled() == uwp, "Controlify 2.4 SDL guard");
        if (uwp) check(Boolean.FALSE.equals(sdl.getReturnValue()), "Never report SDL as loaded");

        CallbackInfoReturnable<Boolean> offline = new CallbackInfoReturnable<>("tryOfflineLoadAndStart", true);
        call(ControlifyLegacySdlMixin.class, null, "banditvault$skipDesktopSdl", offline);
        check(offline.isCancelled() == uwp, "Controlify 2.0 direct load guard");
        if (uwp) check(Boolean.FALSE.equals(offline.getReturnValue()), "No desktop SDL load");

        CallbackInfoReturnable<CompletableFuture<Boolean>> download = new CallbackInfoReturnable<>("maybeLoad", true);
        call(ControlifyLegacySdlMixin.class, null, "banditvault$skipDesktopSdlDownload", download);
        check(download.isCancelled() == uwp, "Controlify 2.0 direct download guard");
        if (uwp) check(download.getReturnValue().isDone() && !download.getReturnValue().join(), "No download or incomplete future");

        ControlifyHidMixin hid = new ControlifyHidMixin() {};
        hidField("firstFetch").setBoolean(hid, true);
        CallbackInfo start = new CallbackInfo("start", true);
        call(ControlifyHidMixin.class, hid, "banditvault$skipDesktopHid", start);
        check(start.isCancelled() == uwp, "HID start guard");
        check(hidField("disabled").getBoolean(hid) == uwp, "No access to an uninitialised HID service");
        check(hidField("firstFetch").getBoolean(hid) != uwp, "Intentional fallback does not display the missing-HID toast");
    }

    public static void main(String[] args) throws Exception {
        test(false);
        test(true);
        test(false); // no sticky global flag changes affecting desktop runs
        System.out.println("NATIVE_GUARD_TESTS_OK");
    }
}
