package banditvault.xboxcompat.mixin;

import banditvault.xboxcompat.UwpRuntime;
import banditvault.xboxcompat.XboxCompatLog;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Pseudo;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;
import java.util.Collection;
import java.util.Collections;

// Sodium 0.5 and 0.6+ probe before Minecraft starts. D3DKMT is a desktop WDDM
// API, not the Mesa/D3D12 rendering path used by the Xbox AppContainer.
@Pseudo
@Mixin(targets = {
    "me.jellysquid.mods.sodium.client.compatibility.environment.probe.GraphicsAdapterProbe",
    "net.caffeinemc.mods.sodium.client.compatibility.environment.probe.GraphicsAdapterProbe"
}, remap = false)
public abstract class SodiumGraphicsAdapterMixin {
    @Inject(method = "findAdapters()V", at = @At("HEAD"), cancellable = true, require = 0)
    private static void banditvault$skipDesktopAdapterProbe(CallbackInfo ci) {
        if (!UwpRuntime.isSandboxed()) return;
        XboxCompatLog.log("Skipping Sodium desktop adapter probe (D3DKMT/OSHI) in UWP; Mesa OpenGL remains enabled");
        ci.cancel();
    }

    // Some Sodium versions leave ADAPTERS null until a successful probe. Return
    // an empty collection, not a fabricated desktop GPU/driver identity.
    @Inject(method = "getAdapters()Ljava/util/Collection;", at = @At("HEAD"), cancellable = true, require = 0)
    private static void banditvault$unavailableDesktopAdapters(CallbackInfoReturnable<Collection<?>> cir) {
        if (UwpRuntime.isSandboxed()) cir.setReturnValue(Collections.emptyList());
    }
}
