package banditvault.legacyforge;

import java.io.IOException;
import java.net.URI;
import java.nio.file.FileSystem;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.Map;

public final class LegacyZipFsBridge {
    private static final com.sun.nio.zipfs.ZipFileSystemProvider PROVIDER =
        new com.sun.nio.zipfs.ZipFileSystemProvider();

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
         * the archive directly and does not perform that canonicalization.
         */
        return PROVIDER.newFileSystem(jarPath, env);
    }
}
