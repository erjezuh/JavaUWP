package banditvault.legacyforge;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.util.Enumeration;
import java.util.jar.JarEntry;
import java.util.jar.JarFile;
import java.util.jar.JarOutputStream;

public final class LegacyLwjglJarPatcher {
    private static final String TARGET = "org/lwjgl/opengl/WindowsDisplay.class";

    public static void main(String[] args) throws Exception {
        if (args.length != 1) {
            throw new IllegalArgumentException("Usage: LegacyLwjglJarPatcher <lwjgl-2.9.4.jar>");
        }

        File jarFile = new File(args[0]);
        if (!jarFile.isFile()) {
            throw new IOException("LWJGL jar missing: " + jarFile);
        }

        File tempFile = new File(jarFile.getAbsolutePath() + ".uwp-patched.tmp");
        boolean changed = false;

        try (JarFile input = new JarFile(jarFile)) {
            JarEntry targetEntry = input.getJarEntry(TARGET);
            if (targetEntry == null) {
                throw new IOException("LWJGL jar does not contain " + TARGET);
            }

            boolean hadManifest = input.getManifest() != null;
            byte[] original = readAll(input.getInputStream(targetEntry));
            byte[] transformed = new LegacyZipFsTransformer().transform(
                "org.lwjgl.opengl.WindowsDisplay",
                "org.lwjgl.opengl.WindowsDisplay",
                original);

            // A previously patched jar may still carry the stock sealing manifest.
            // Rebuild it once more so the compatibility copy is actually unsealed.
            if (transformed == null || transformed == original) {
                if (!hadManifest) {
                    System.out.println("[BanditVault] LWJGL 2 WindowsDisplay already UWP-patched: " + jarFile);
                    return;
                }
                transformed = original;
            }

            changed = true;
            try (JarOutputStream output = new JarOutputStream(new FileOutputStream(tempFile))) {
                Enumeration<JarEntry> entries = input.entries();
                while (entries.hasMoreElements()) {
                    JarEntry entry = entries.nextElement();
                    String entryName = entry.getName();

                    // The stock LWJGL jar seals org.lwjgl.*. We are replacing
                    // bytecode, so emit an unsealed, unsigned compatibility jar.
                    if ("META-INF/MANIFEST.MF".equalsIgnoreCase(entryName) ||
                        (entryName.startsWith("META-INF/") &&
                         (entryName.endsWith(".SF") || entryName.endsWith(".RSA") || entryName.endsWith(".DSA")))) {
                        continue;
                    }

                    JarEntry copy = new JarEntry(entryName);

                    if (entry.getTime() != -1L) {
                        copy.setTime(entry.getTime());
                    }

                    output.putNextEntry(copy);
                    if (TARGET.equals(entry.getName())) {
                        output.write(transformed);
                    } else {
                        try (InputStream in = input.getInputStream(entry)) {
                            byte[] buffer = new byte[8192];
                            int read;
                            while ((read = in.read(buffer)) != -1) {
                                output.write(buffer, 0, read);
                            }
                        }
                    }
                    output.closeEntry();
                }
            }
        }

        if (!changed) {
            Files.deleteIfExists(tempFile.toPath());
            return;
        }

        Files.move(
            tempFile.toPath(),
            jarFile.toPath(),
            StandardCopyOption.REPLACE_EXISTING);
        System.out.println("[BanditVault] Patched LWJGL 2.9.4 WindowsDisplay for UWP: " + jarFile);
    }

    private static byte[] readAll(InputStream input) throws IOException {
        try (InputStream in = input) {
            java.io.ByteArrayOutputStream out = new java.io.ByteArrayOutputStream();
            byte[] buffer = new byte[8192];
            int read;
            while ((read = in.read(buffer)) != -1) {
                out.write(buffer, 0, read);
            }
            return out.toByteArray();
        }
    }
}
