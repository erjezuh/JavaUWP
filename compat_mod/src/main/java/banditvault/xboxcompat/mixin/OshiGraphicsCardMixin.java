package banditvault.xboxcompat.mixin;

import banditvault.xboxcompat.UwpRuntime;
import banditvault.xboxcompat.XboxCompatLog;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Pseudo;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;
import java.util.Collections;
import java.util.List;

// This is diagnostic GPU enumeration only, not OpenGL capability detection.
// WMI tries to load Ole32 and query the desktop registry, neither available here.
@Pseudo
@Mixin(targets = "oshi.hardware.platform.windows.WindowsHardwareAbstractionLayer", remap = false)
public abstract class OshiGraphicsCardMixin {
    // Intercept the HAL, before WindowsGraphicsCard's native static initializers run.
    @Inject(method = "getGraphicsCards()Ljava/util/List;", at = @At("HEAD"), cancellable = true, require = 0)
    private void banditvault$skipWmiGraphicsProbe(CallbackInfoReturnable<List<?>> cir) {
        if (!UwpRuntime.isSandboxed()) return;
        XboxCompatLog.log("Skipping OSHI WMI/Ole32 graphics inventory in UWP");
        cir.setReturnValue(Collections.emptyList());
    }
}
