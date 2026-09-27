package banditvault.legacyforge;

import java.io.IOException;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.net.URI;
import java.nio.file.FileSystem;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.Map;

public final class LegacyZipFsBridge {
    private static final Class<?> PROVIDER_CLASS = loadProviderClass();
    private static final Method PATH_NEW_FILE_SYSTEM = findPathNewFileSystem();

    private LegacyZipFsBridge() {
    }

    public static FileSystem newFileSystem(URI uri, Map<String, ?> env)
        throws IOException {
        String spec = uri.getRawSchemeSpecificPart();
        int separator = spec.indexOf("!/");
        if (separator < 0) {
            throw new IllegalArgumentException("Invalid jar URI: " + uri);
        }

        URI jarUri = URI.create(spec.substring(0, separator));
        Path jarPath = Paths.get(jarUri).toAbsolutePath().normalize();

        /*
         * Do not call FileSystems.newFileSystem(URI, ...).
         * Java 8's URI overload enters ZipFileSystemProvider.newFileSystem(URI,...),
         * which canonicalizes the archive with Path.toRealPath(). That operation is
         * denied for Xbox/UWP LocalState paths. The provider's Path overload opens
         * the archive directly and avoids that canonicalization.
         *
         * The provider is resolved reflectively so this coremod can still compile
         * with the launcher's modern JDK and only depends on the Java 8 class at
         * runtime, where it is supplied by the legacy zipfs.jar.
         */
        if (PROVIDER_CLASS == null || PATH_NEW_FILE_SYSTEM == null) {
            throw new IOException("Java 8 ZipFS provider Path overload is unavailable");
        }

        try {
            Object provider = PROVIDER_CLASS.newInstance();
            return (FileSystem) PATH_NEW_FILE_SYSTEM.invoke(provider, jarPath, env);
        } catch (InvocationTargetException e) {
            Throwable cause = e.getCause();
            if (cause instanceof IOException) {
                throw (IOException) cause;
            }
            if (cause instanceof RuntimeException) {
                throw (RuntimeException) cause;
            }
            throw new IOException("Java 8 ZipFS provider failed", cause);
        } catch (ReflectiveOperationException e) {
            throw new IOException("Could not invoke Java 8 ZipFS provider", e);
        }
    }

    private static Class<?> loadProviderClass() {
        try {
            return Class.forName("com.sun.nio.zipfs.ZipFileSystemProvider");
        } catch (ClassNotFoundException e) {
            return null;
        }
    }

    private static Method findPathNewFileSystem() {
        if (PROVIDER_CLASS == null) {
            return null;
        }
        try {
            return PROVIDER_CLASS.getMethod("newFileSystem", Path.class, Map.class);
        } catch (NoSuchMethodException e) {
            return null;
        }
    }
}
