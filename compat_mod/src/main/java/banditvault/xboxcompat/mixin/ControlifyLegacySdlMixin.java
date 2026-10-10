package banditvault.xboxcompat.mixin;

import banditvault.xboxcompat.UwpRuntime;
import banditvault.xboxcompat.XboxCompatLog;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Pseudo;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;
import java.util.concurrent.CompletableFuture;

@Pseudo
@Mixin(targets = "dev.isxander.controlify.driver.sdl.SDL3NativesManager", remap = false)
public abstract class ControlifyLegacySdlMixin {
    // Also cover direct loads from Controlify's settings UI, not only onboarding.
    @Inject(method = "tryOfflineLoadAndStart()Z", at = @At("HEAD"), cancellable = true, require = 0)
    private static void banditvault$skipDesktopSdl(CallbackInfoReturnable<Boolean> cir) {
        if (!UwpRuntime.isSandboxed()) return;
        XboxCompatLog.log("Controlify: skipping desktop SDL3 load in UWP");
        cir.setReturnValue(false);
    }

    @Inject(method = "maybeLoad()Ljava/util/concurrent/CompletableFuture;", at = @At("HEAD"), cancellable = true, require = 0)
    private static void banditvault$skipDesktopSdlDownload(CallbackInfoReturnable<CompletableFuture<Boolean>> cir) {
        if (!UwpRuntime.isSandboxed()) return;
        XboxCompatLog.log("Controlify: skipping desktop SDL3 download in UWP");
        cir.setReturnValue(CompletableFuture.completedFuture(false));
    }
}
