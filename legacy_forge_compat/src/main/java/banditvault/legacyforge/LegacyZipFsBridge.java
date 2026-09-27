package banditvault.legacyforge;

import java.io.IOException;
import java.net.URI;
import java.nio.file.FileSystem;
import java.nio.file.FileSystemNotFoundException;
import java.nio.file.FileSystems;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.Map;

public final class LegacyZipFsBridge {
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
         * Java 8's ZipFileSystemProvider has a Path overload which opens the
         * archive directly. Its URI overload first calls Path.toRealPath(),
         * which is denied for the Xbox/UWP LocalState path. Use the Path
         * overload deliberately.
         */
        try {
            return FileSystems.newFileSystem(jarPath, env);
        } catch (FileSystemNotFoundException e) {
            throw e;
        }
    }
}
