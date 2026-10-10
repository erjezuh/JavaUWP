package banditvault.xboxcompat.mixin;

import banditvault.xboxcompat.UwpRuntime;
import banditvault.xboxcompat.XboxCompatLog;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Pseudo;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Pseudo
@Mixin(targets = "dev.isxander.controlify.hid.ControllerHIDService", remap = false)
public abstract class ControlifyHidMixin {
    @Shadow private boolean disabled;
    @Shadow private boolean firstFetch;

    @Inject(method = "start()V", at = @At("HEAD"), cancellable = true, require = 0)
    private void banditvault$skipDesktopHid(CallbackInfo ci) {
        if (!UwpRuntime.isSandboxed()) return;
        // fetchType uses its normal generic-controller fallback when disabled;
        // leaving this false would dereference an uninitialised HID service.
        disabled = true;
        firstFetch = false; // no repeated missing-HID toast for this intentional backend choice
        XboxCompatLog.log("Controlify: HID hardware scan unavailable in UWP; gamepad input uses GLFW");
        ci.cancel();
    }
}
