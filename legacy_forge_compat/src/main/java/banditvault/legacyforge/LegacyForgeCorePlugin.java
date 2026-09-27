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
        return new String[] {"banditvault.legacyforge.LegacyZipFsTransformer"};
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
             * transformer can then rewrite WindowsDisplay before it initializes.
             *
             * The field is private in LaunchClassLoader, hence reflection.
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
            }
        } catch (Throwable ignored) {
            /*
             * If the field layout differs, leave LaunchWrapper untouched rather
             * than breaking the entire legacy launch. The transformer remains
             * harmless when LWJGL is already provided by the parent loader.
             */
        }
    }
}
