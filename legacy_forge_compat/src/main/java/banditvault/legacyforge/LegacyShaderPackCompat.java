package banditvault.legacyforge;

import java.io.BufferedInputStream;
import java.io.BufferedOutputStream;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;
import java.util.zip.ZipOutputStream;

/**
 * Patches shader packs that call the GLSL 1.30+ builtin texture(...) from
 * shaders compiled as GLSL 110/120. NVIDIA's compiler accepts that; Mesa's is
 * strict ("error: no function with name 'texture'") so the program fails to
 * link and the pack renders black (OptiFine log: "[Shaders] Error compiling
 * fragment shader ... no function with name 'texture'" -> "Invalid program
 * final").
 *
 * The fix is textual and conservative: only the exact identifier "texture"
 * immediately followed by "(" (never texture2D/texture3D/textureLod/...) is
 * rewritten to texture2D(, the GLSL 110 builtin this pack style already uses
 * everywhere else. Only "old style" sources are touched (they use
 * gl_FragColor/varying/texture2D and declare no #version 130+).
 *
 * Runs once at coremod setup, long before OptiFine reads the packs. Originals
 * are kept as *.bandit-orig.bak and the patch is idempotent. Set
 * MC_SHADER_PATCH=0 to disable.
 */
public final class LegacyShaderPackCompat {
    private static boolean done;

    private LegacyShaderPackCompat() {
    }

    public static synchronized void ensure() {
        if (done) {
            return;
        }
        done = true;
        try {
            if ("0".equals(System.getenv("MC_SHADER_PATCH"))) {
                System.err.println("[BanditVault] Shader pack patch disabled (MC_SHADER_PATCH=0).");
                return;
            }
            run();
        } catch (Throwable t) {
            System.err.println("[BanditVault] Shader pack patch failed (packs untouched): " + t);
        }
    }

    private static void run() throws Exception {
        File gameDir = new File(System.getProperty("user.dir", "."));
        File shaderPacks = new File(gameDir, "shaderpacks");
        if (!shaderPacks.isDirectory()) {
            return;
        }
        File[] children = shaderPacks.listFiles();
        if (children == null) {
            return;
        }
        for (File child : children) {
            String name = child.getName().toLowerCase(Locale.ROOT);
            if (name.endsWith(".bandit-orig.bak") || name.endsWith(".bandit-patch.tmp")) {
                continue;
            }
            if (child.isFile() && name.endsWith(".zip")) {
                patchZip(child);
            } else if (child.isDirectory()) {
                patchFolder(child);
            }
        }
    }

    // ---- the actual source fix -------------------------------------------

    private static boolean isIdentChar(char c) {
        return Character.isLetterOrDigit(c) || c == '_';
    }

    /** true for sources written in the GLSL 110/120 style (safe to rewrite). */
    private static boolean looksOldStyle(String source) {
        int version = -1;
        final int p = source.indexOf("#version");
        if (p >= 0) {
            int q = p + "#version".length();
            while (q < source.length() && Character.isWhitespace(source.charAt(q))) {
                q++;
            }
            int end = q;
            while (end < source.length() && Character.isDigit(source.charAt(end))) {
                end++;
            }
            if (end > q) {
                try {
                    version = Integer.parseInt(source.substring(q, end));
                } catch (NumberFormatException ignored) {
                }
            }
        }
        if (version >= 130) {
            return false;
        }
        return source.contains("gl_FragColor") || source.contains("texture2D(")
            || source.contains("varying ") || source.contains("attribute ")
            || source.contains("shadow2D(");
    }

    /**
     * Rewrites bare texture( calls to texture2D(. Word-boundary aware: never
     * touches texture2D(/texture3D(/textureLod(/textureProj(/shadow2D( etc.
     */
    private static String applyFixes(String source, int[] counter) {
        final String needle = "texture(";
        StringBuilder out = new StringBuilder(source.length() + 16);
        int i = 0;
        while (i < source.length()) {
            final int idx = source.indexOf(needle, i);
            if (idx < 0) {
                out.append(source, i, source.length());
                break;
            }
            out.append(source, i, idx);
            final boolean boundary = idx == 0 || !isIdentChar(source.charAt(idx - 1));
            if (boundary) {
                out.append("texture2D(");
                counter[0]++;
            } else {
                out.append(needle);
            }
            i = idx + needle.length();
        }
        return out.toString();
    }

    /** Returns the patched text, or null when nothing changed. */
    private static String patchText(String text, int[] counter) {
        if (text.indexOf("texture(") < 0 || !looksOldStyle(text)) {
            return null;
        }
        String patched = applyFixes(text, counter);
        return patched.equals(text) ? null : patched;
    }

    // ---- zip packs -------------------------------------------------------

    private static void patchZip(File zip) throws Exception {
        Map<String, byte[]> entries = new LinkedHashMap<String, byte[]>();
        ZipInputStream in = new ZipInputStream(new BufferedInputStream(new FileInputStream(zip)));
        try {
            ZipEntry entry;
            byte[] buf = new byte[65536];
            while ((entry = in.getNextEntry()) != null) {
                ByteArrayOutputStream bos = new ByteArrayOutputStream();
                int n;
                while ((n = in.read(buf)) > 0) {
                    bos.write(buf, 0, n);
                }
                entries.put(entry.getName(), bos.toByteArray());
            }
        } finally {
            in.close();
        }

        int total = 0;
        List<String> touched = new ArrayList<String>();
        for (Map.Entry<String, byte[]> en : entries.entrySet()) {
            if (!isShaderText(en.getKey())) {
                continue;
            }
            String text = new String(en.getValue(), StandardCharsets.UTF_8);
            int[] counter = new int[1];
            String patched = patchText(text, counter);
            if (patched != null) {
                en.setValue(patched.getBytes(StandardCharsets.UTF_8));
                total += counter[0];
                touched.add(en.getKey() + " x" + counter[0]);
            }
        }
        if (total == 0) {
            return;
        }

        File backup = new File(zip.getParentFile(), zip.getName() + ".bandit-orig.bak");
        if (!backup.exists()) {
            copyFile(zip, backup);
        }
        File tmp = new File(zip.getParentFile(), zip.getName() + ".bandit-patch.tmp");
        ZipOutputStream out = new ZipOutputStream(new BufferedOutputStream(new FileOutputStream(tmp)));
        try {
            for (Map.Entry<String, byte[]> en : entries.entrySet()) {
                out.putNextEntry(new ZipEntry(en.getKey()));
                out.write(en.getValue());
            }
        } finally {
            out.close();
        }
        if (zip.delete() && tmp.renameTo(zip)) {
            System.err.println("[BanditVault] Shader pack " + zip.getName()
                + ": patched " + total + " texture() call(s) -> texture2D( in " + touched
                + " (original: " + backup.getName() + ")");
        } else {
            tmp.delete();
            System.err.println("[BanditVault] Shader pack " + zip.getName()
                + ": could not replace the zip; pack left untouched.");
        }
    }

    // ---- folder packs ----------------------------------------------------

    private static void patchFolder(File dir) throws Exception {
        File[] children = dir.listFiles();
        if (children == null) {
            return;
        }
        for (File child : children) {
            if (child.isDirectory()) {
                patchFolder(child);
                continue;
            }
            if (!isShaderText(child.getName()) || child.getName().endsWith(".bandit-orig.bak")) {
                continue;
            }
            byte[] raw = readFile(child);
            String text = new String(raw, StandardCharsets.UTF_8);
            int[] counter = new int[1];
            String patched = patchText(text, counter);
            if (patched == null) {
                continue;
            }
            File backup = new File(child.getParentFile(), child.getName() + ".bandit-orig.bak");
            if (!backup.exists()) {
                copyFile(child, backup);
            }
            FileOutputStream out = new FileOutputStream(child);
            try {
                out.write(patched.getBytes(StandardCharsets.UTF_8));
            } finally {
                out.close();
            }
            System.err.println("[BanditVault] Shader pack file " + child.getName()
                + ": patched " + counter[0] + " texture() call(s) -> texture2D("
                + " (original: " + backup.getName() + ")");
        }
    }

    // ---- helpers ---------------------------------------------------------

    private static boolean isShaderText(String name) {
        String lower = name.toLowerCase(Locale.ROOT);
        return lower.endsWith(".fsh") || lower.endsWith(".vsh") || lower.endsWith(".glsl");
    }

    private static byte[] readFile(File file) throws Exception {
        ByteArrayOutputStream bos = new ByteArrayOutputStream();
        FileInputStream in = new FileInputStream(file);
        try {
            byte[] buf = new byte[65536];
            int n;
            while ((n = in.read(buf)) > 0) {
                bos.write(buf, 0, n);
            }
        } finally {
            in.close();
        }
        return bos.toByteArray();
    }

    private static void copyFile(File from, File to) throws Exception {
        FileOutputStream out = new FileOutputStream(to);
        try {
            out.write(readFile(from));
        } finally {
            out.close();
        }
    }
}
