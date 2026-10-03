package banditvault.legacyforge;

import java.lang.reflect.Field;
import java.util.Map;
import java.util.Set;
import net.minecraft.launchwrapper.Launch;
import net.minecraftforge.fml.relauncher.IFMLLoadingPlugin;

@IFMLLoadingPlugin.MCVersion("1.12.2")
@IFMLLoadingPlugin.Name("BanditVault Legacy Forge Xbox Compat")
@IFMLLoadingPlugin.TransformerExclusions({"banditvault.legacyforge"})
public final class LegacyForgeCorePlugin implements IFMLLoadingPlugin {
    @Override
    public String[] getASMTransformerClass() {
        return new String[] {
            "banditvault.legacyforge.LegacyZipFsTransformer",
            "banditvault.legacyforge.LegacyWorldRenderGuard",
            "banditvault.legacyforge.LegacyMultiDrawSanitizer",
            "banditvault.legacyforge.LegacyRenderLibCompat",
            "banditvault.legacyforge.LegacyOpenAlRetry"
        };
    }

    @Override
    public String getModContainerClass() {
        return null;
    }

    @Override
    public String getSetupClass() {
        return null;
    }

    @Override
    public void injectData(Map<String, Object> data) {
        // First HTTPS request happens well after coremod setup; repair the
        // trust store now so skins and session lookups can authenticate.
        LegacySslFixer.ensure();
        // Shader packs are read much later; rewrite Mesa-hostile GLSL now.
        LegacyShaderPackCompat.ensure();
        removeLwjglClassLoaderExclusion();
    }

    @Override
    public String getAccessTransformerClass() {
        return null;
    }

    private static void removeLwjglClassLoaderExclusion() {
        try {
            /*
             * LaunchWrapper deliberately delegates org.lwjgl.* to the parent
             * classloader, so IClassTransformer never sees WindowsDisplay.
             * Remove only that one exclusion for the legacy target. The
             * transformable class is supplied through launcher-overrides before
             * Minecraft's client classes begin initialization.
             */
            if (Launch.classLoader == null) {
                return;
            }

            Field field = Launch.classLoader.getClass().getDeclaredField("classLoaderExceptions");
            field.setAccessible(true);

            Object value = field.get(Launch.classLoader);
            if (value instanceof Set) {
                @SuppressWarnings("rawtypes")
                Set exclusions = (Set) value;
                exclusions.remove("org.lwjgl.");
                System.err.println("[BanditVault] Removed LaunchWrapper org.lwjgl class-loader exclusion for legacy UWP.");
            }
        } catch (Throwable ignored) {
            System.err.println("[BanditVault] Could not remove LaunchWrapper org.lwjgl exclusion: " + ignored);
        }
    }
}
