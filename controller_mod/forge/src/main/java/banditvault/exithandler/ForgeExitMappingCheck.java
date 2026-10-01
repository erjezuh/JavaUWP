package banditvault.exithandler;

import net.minecraft.client.Minecraft;

/**
 * Compile-time mapping guard, deliberately outside any @Mixin class.
 *
 * If the SRG id for Minecraft.run() (m_91383_) ever changes, javac fails the
 * build here instead of shipping a mod whose mixin silently does nothing
 * (which would close the whole launcher when the player quits the game).
 * The method reference is stored but never invoked.
 */
final class ForgeExitMappingCheck {
    @SuppressWarnings("unused")
    private static final Runnable kRunMethodMappingCheck = ((Minecraft) null)::m_91383_;

    private ForgeExitMappingCheck() {}
}
