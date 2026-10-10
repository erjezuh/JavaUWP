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
@Mixin(targets = "dev.isxander.controlify.Controlify", remap = false)
public abstract class ControlifyGlfwMixin {
    // Controlify 2.0.x already implements a GLFW fallback when this future is
    // false. Do not prompt for/download a desktop SDL DLL that UWP cannot use.
    @Inject(method = "askNatives()Ljava/util/concurrent/CompletableFuture;", at = @At("HEAD"), cancellable = true, require = 0)
    private void banditvault$useUwpGlfw(CallbackInfoReturnable<CompletableFuture<Boolean>> cir) {
        if (!UwpRuntime.isSandboxed()) return;
        XboxCompatLog.log("Controlify: using the UWP GLFW gamepad backend instead of desktop SDL natives");
        cir.setReturnValue(CompletableFuture.completedFuture(false));
    }
}
