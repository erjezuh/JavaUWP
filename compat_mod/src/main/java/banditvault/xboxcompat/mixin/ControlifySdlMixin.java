package banditvault.xboxcompat.mixin;

import banditvault.xboxcompat.UwpRuntime;
import banditvault.xboxcompat.XboxCompatLog;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Pseudo;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

@Pseudo
@Mixin(targets = "dev.isxander.controlify.driver.sdl.SDLNativesLoader", remap = false)
public abstract class ControlifySdlMixin {
    // Controlify 2.4.x moved SDL loading out of askNatives, but still has the
    // GLFWControllerManager fallback. No fake SDL3 library or success result.
    @Inject(method = "tryLoad()Z", at = @At("HEAD"), cancellable = true, require = 0)
    private static void banditvault$skipDesktopSdl(CallbackInfoReturnable<Boolean> cir) {
        if (!UwpRuntime.isSandboxed()) return;
        XboxCompatLog.log("Controlify: SDL unavailable in UWP; selecting GLFW fallback");
        cir.setReturnValue(false);
    }
}
