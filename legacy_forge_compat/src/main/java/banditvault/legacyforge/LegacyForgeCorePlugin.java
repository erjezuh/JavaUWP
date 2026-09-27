package banditvault.legacyforge;

import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.lang.reflect.Modifier;
import java.security.CodeSource;
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
        forceLegacyWindowsDisplayLoad();
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
                System.err.println("[BanditVault] Removed LaunchWrapper org.lwjgl class-loader exclusion for legacy UWP.");
            }
        } catch (Throwable ignored) {
            System.err.println("[BanditVault] Could not remove LaunchWrapper org.lwjgl exclusion: " + ignored);
            /*
             * If the field layout differs, leave LaunchWrapper untouched rather
             * than breaking the entire legacy launch. The transformer remains
             * harmless when LWJGL is already provided by the parent loader.
             */
        }
    }

    private static void forceLegacyWindowsDisplayLoad() {
        try {
            if (Launch.classLoader == null) {
                System.err.println("[BanditVault] LaunchClassLoader unavailable; cannot preload WindowsDisplay.");
                return;
            }

            /*
             * Do not initialize the class here. Loading through LaunchClassLoader
             * is intentional: it must resolve the patched LWJGL compatibility
             * class through LaunchWrapper, allowing LegacyZipFsTransformer to
             * rewrite the native getCurrentDisplayMode() before Display.<clinit>.
             */
            Class<?> windowsDisplay = Launch.classLoader.loadClass("org.lwjgl.opengl.WindowsDisplay");
            Method getCurrentDisplayMode = windowsDisplay.getDeclaredMethod(
                "getCurrentDisplayMode");

            boolean nativeMethod = Modifier.isNative(getCurrentDisplayMode.getModifiers());
            CodeSource source = windowsDisplay.getProtectionDomain() != null
                ? windowsDisplay.getProtectionDomain().getCodeSource()
                : null;

            System.err.println(
                "[BanditVault] Preloaded WindowsDisplay via LaunchClassLoader; native="
                    + nativeMethod
                    + " source="
                    + (source != null && source.getLocation() != null
                        ? source.getLocation()
                        : "<unknown>"));

            if (nativeMethod) {
                System.err.println(
                    "[BanditVault] WARNING: WindowsDisplay is still native; "
                        + "legacy LWJGL override was not transformed.");
            }
        } catch (Throwable error) {
            System.err.println(
                "[BanditVault] Could not preload legacy WindowsDisplay: " + error);
        }
    }
}
