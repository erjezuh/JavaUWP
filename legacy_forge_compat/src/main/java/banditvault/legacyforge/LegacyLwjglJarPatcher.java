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
import org.objectweb.asm.ClassReader;
import org.objectweb.asm.ClassVisitor;
import org.objectweb.asm.MethodVisitor;
import org.objectweb.asm.Opcodes;

public final class LegacyLwjglJarPatcher {
    private static final String TARGET = "org/lwjgl/opengl/WindowsDisplay.class";

    public static void main(String[] args) throws Exception {
        if (args.length < 1 || args.length > 2) {
            throw new IllegalArgumentException(
                "Usage: LegacyLwjglJarPatcher <lwjgl-2.9.4.jar> [patched-WindowsDisplay.class]");
        }

        File jarFile = new File(args[0]);
        if (!jarFile.isFile()) {
            throw new IOException("LWJGL jar missing: " + jarFile);
        }

        File classOutput = args.length == 2 ? new File(args[1]) : null;
        if (classOutput != null && classOutput.getParentFile() != null) {
            Files.createDirectories(classOutput.getParentFile().toPath());
        }

        File tempFile = new File(jarFile.getAbsolutePath() + ".uwp-patched.tmp");
        boolean rewriteJar = false;

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

            if (transformed == null) {
                transformed = original;
            }

            if (!isPatched(transformed)) {
                throw new IOException(
                    "WindowsDisplay patch did not remove native getCurrentDisplayMode().");
            }

            if (classOutput != null) {
                Files.write(classOutput.toPath(), transformed);
                System.out.println(
                    "[BanditVault] Exported patched WindowsDisplay class: " + classOutput);
            }

            /*
             * Rebuild the jar whenever it still has the stock signed/sealing
             * manifest, or whenever the bytecode actually changed. This also
             * makes repeated builds deterministic after a previous patch.
             */
            rewriteJar = hadManifest || transformed != original;

            if (!rewriteJar) {
                System.out.println(
                    "[BanditVault] LWJGL 2 WindowsDisplay already UWP-patched and unsealed: "
                        + jarFile);
                return;
            }

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

        if (!rewriteJar) {
            Files.deleteIfExists(tempFile.toPath());
            return;
        }

        Files.move(
            tempFile.toPath(),
            jarFile.toPath(),
            StandardCopyOption.REPLACE_EXISTING);
        System.out.println(
            "[BanditVault] Patched LWJGL 2.9.4 WindowsDisplay for UWP: " + jarFile);
    }

    private static boolean isPatched(byte[] bytes) {
        final boolean[] nativeMethod = new boolean[] {false};
        ClassReader reader = new ClassReader(bytes);
        reader.accept(new ClassVisitor(Opcodes.ASM5) {
            @Override
            public MethodVisitor visitMethod(
                int access, String name, String descriptor,
                String signature, String[] exceptions) {
                if ("getCurrentDisplayMode".equals(name)
                    && "()Lorg/lwjgl/opengl/DisplayMode;".equals(descriptor)
                    && (access & Opcodes.ACC_NATIVE) != 0) {
                    nativeMethod[0] = true;
                }
                return null;
            }
        }, ClassReader.SKIP_CODE | ClassReader.SKIP_DEBUG | ClassReader.SKIP_FRAMES);
        return !nativeMethod[0];
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
