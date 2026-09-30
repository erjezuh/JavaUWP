package banditvault.legacyforge;

import java.io.File;
import java.io.FileInputStream;
import java.io.InputStream;
import java.security.KeyStore;
import java.security.Security;

/**
 * Repairs the JVM trust store before the first HTTPS request (skins, session
 * profile lookup, Forge version checks).
 *
 * Two independent breakages were observed on the packaged Java 8 runtime:
 *  - xbox_security.properties used to force keystore.type=pkcs12 while some
 *    Java 8 runtimes ship a JKS cacerts, and KeyStore.getInstance("pkcs12")
 *    has no JKS fallback: every https request died with
 *    "DerInputStream.getLength(): lengthTag=109, too big";
 *  - a broken jssecacerts sitting next to cacerts shadows a healthy cacerts,
 *    because the SSL stack prefers it blindly.
 *
 * This probes both files in both formats, points javax.net.ssl.trustStore at
 * the first store that actually loads with sane anchors, and logs exactly
 * what it found so an unusable store is visible in mc_launch.log.
 */
public final class LegacySslFixer {
    private static boolean done;

    public static synchronized void ensure() {
        if (done) {
            return;
        }
        done = true;
        try {
            run();
        } catch (Throwable t) {
            System.err.println("[BanditVault] SSL trust store fixup failed: " + t);
        }
    }

    private static void run() throws Exception {
        // keystore.type drives KeyStore.getDefaultType() for everything else
        // (mods included). "jks" with keystore.type.compat=true accepts both
        // JKS and PKCS12 stores; pkcs12 without fallback only accepts PKCS12.
        Security.setProperty("keystore.type", "jks");

        File secDir = new File(
            System.getProperty("java.home"), "lib" + File.separator + "security");
        File[] files = {
            new File(secDir, "jssecacerts"),
            new File(secDir, "cacerts")
        };
        String[] types = { "jks", "pkcs12" };
        String[] passes = { "changeit", null };

        for (File file : files) {
            if (!file.isFile()) {
                continue;
            }
            for (String type : types) {
                for (String pass : passes) {
                    int entries = tryLoad(file, type, pass);
                    if (entries > 0) {
                        System.setProperty(
                            "javax.net.ssl.trustStore", file.getAbsolutePath());
                        System.setProperty(
                            "javax.net.ssl.trustStoreType", type);
                        if (pass != null) {
                            System.setProperty(
                                "javax.net.ssl.trustStorePassword", pass);
                        }
                        System.err.println(
                            "[BanditVault] SSL trust store fixed: " + file.getName()
                            + " type=" + type + " entries=" + entries);
                        return;
                    }
                }
            }
            System.err.println(
                "[BanditVault] Trust store unusable: " + file.getAbsolutePath()
                + " size=" + file.length() + " head=" + headHex(file));
        }
        System.err.println(
            "[BanditVault] No usable SSL trust store found;"
            + " HTTPS will fail (skins, session lookup)");
    }

    private static int tryLoad(File file, String type, String pass) {
        try (InputStream in = new FileInputStream(file)) {
            KeyStore store = KeyStore.getInstance(type);
            store.load(in, pass == null ? null : pass.toCharArray());
            return store.size();
        } catch (Throwable ignored) {
            return 0;
        }
    }

    private static String headHex(File file) {
        try (InputStream in = new FileInputStream(file)) {
            byte[] buf = new byte[8];
            int read = in.read(buf);
            StringBuilder hex = new StringBuilder();
            for (int i = 0; i < read; i++) {
                hex.append(String.format("%02X", buf[i]));
            }
            return hex.toString();
        } catch (Throwable ignored) {
            return "?";
        }
    }

    private LegacySslFixer() {
    }
}
