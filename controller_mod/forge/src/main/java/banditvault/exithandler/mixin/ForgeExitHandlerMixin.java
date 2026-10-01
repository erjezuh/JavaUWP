package banditvault.exithandler.mixin;

import banditvault.exithandler.ReturnToLauncherSignal;
import net.minecraft.client.Minecraft;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(value = Minecraft.class, remap = false)
public abstract class ForgeExitHandlerMixin {
    @Inject(method = "m_91383_", at = @At("TAIL"), remap = false)
    private void banditvault$returnToLauncherWhenMainLoopExits(CallbackInfo ci) {
        System.out.println("[BanditVault] Minecraft main loop exited; signaling launcher return");
        throw new ReturnToLauncherSignal();
    }
}
