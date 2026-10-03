package banditvault.legacyforge;

import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;

import org.objectweb.asm.ClassReader;
import org.objectweb.asm.ClassVisitor;
import org.objectweb.asm.ClassWriter;
import org.objectweb.asm.MethodVisitor;
import org.objectweb.asm.Opcodes;

import net.minecraft.launchwrapper.IClassTransformer;

/**
 * Retries paulscode's LWJGL OpenAL library init when the audio endpoint is
 * not ready yet (MC-9974 family: "AL lib: (EE) MMDevApiOpenPlayback: Device
 * init failed" -> Paulscode "Switching to No Sound (Silent Mode)").
 *
 * In 1.12.2 the chain is SoundSystem.CommandNewLibrary ->
 * LibraryLWJGLOpenAL.init() -> AL.create(); when that throws, the CommandThread
 * dies and the game runs silent. The community workaround (F3+T) re-runs that
 * init later, when the device finally answers. This does the same thing
 * automatically: init() is renamed aside and re-emitted as a wrapper that calls
 * the body up to three times (0s / 0.75s / 2s).
 *
 * Paulscode classes are NOT obfuscated, so only real names are matched. If this
 * transform ever fails it returns the original bytes: paulscode already catches
 * library failures and keeps the game running in silent mode, so the worst case
 * is "still no sound", never a broken launch. Set MC_SOUND_RETRY=0 to disable.
 */
public final class LegacyOpenAlRetry implements IClassTransformer {
    private static final String TARGET = "paulscode.sound.libraries.LibraryLWJGLOpenAL";
    private static final String HOOK = "banditvault/legacyforge/LegacyOpenAlRetry";
    private static final String BODY = "banditvault$initBody";
    private static final int ATTEMPTS = 3;
    private static final long[] WAIT_MS = { 0L, 750L, 2000L };

    private static boolean disabled() {
        try {
            return "0".equals(System.getenv("MC_SOUND_RETRY"));
        } catch (Throwable t) {
            return false;
        }
    }

    @Override
    public byte[] transform(String name, String transformedName, byte[] basicClass) {
        if (basicClass == null) {
            return null;
        }
        final String slashed = name == null ? "" : name.replace('.', '/');
        if (!TARGET.replace('.', '/').equals(slashed) && !TARGET.equals(name)) {
            return basicClass;
        }
        if (disabled()) {
            System.err.println("[BanditVault] OpenAL retry disabled (MC_SOUND_RETRY=0).");
            return basicClass;
        }
        try {
            ClassReader reader = new ClassReader(basicClass);
            ClassWriter writer = new ClassWriter(reader, 0);
            final boolean[] found = { false };
            ClassVisitor rename = new ClassVisitor(Opcodes.ASM5, writer) {
                @Override
                public MethodVisitor visitMethod(int access, String mName, String desc,
                        String signature, String[] exceptions) {
                    if ("init".equals(mName) && "()V".equals(desc)) {
                        found[0] = true;
                        return super.visitMethod(access, BODY, desc, signature, exceptions);
                    }
                    return super.visitMethod(access, mName, desc, signature, exceptions);
                }
            };
            reader.accept(rename, 0);
            if (!found[0]) {
                System.err.println("[BanditVault] LibraryLWJGLOpenAL.init() not found; keeping vanilla.");
                return basicClass;
            }
            MethodVisitor mv = writer.visitMethod(Opcodes.ACC_PUBLIC, "init", "()V", null,
                    new String[] { "paulscode/sound/SoundSystemException" });
            mv.visitCode();
            mv.visitVarInsn(Opcodes.ALOAD, 0);
            mv.visitMethodInsn(Opcodes.INVOKESTATIC, HOOK, "runInit", "(Ljava/lang/Object;)V", false);
            mv.visitInsn(Opcodes.RETURN);
            mv.visitMaxs(1, 1);
            mv.visitEnd();
            System.err.println("[BanditVault] OpenAL init retry wrapper installed.");
            return writer.toByteArray();
        } catch (Throwable t) {
            System.err.println("[BanditVault] OpenAL retry transform failed (keeping vanilla): " + t);
            return basicClass;
        }
    }

    /** Called from the wrapper; retries the renamed init body on device failure. */
    public static void runInit(Object library) {
        Throwable last = null;
        Method body = null;
        try {
            body = library.getClass().getDeclaredMethod(BODY);
            body.setAccessible(true);
        } catch (Throwable t) {
            System.err.println("[BanditVault] OpenAL retry cannot find init body: " + t);
            throw new RuntimeException(t);
        }
        for (int attempt = 0; attempt < ATTEMPTS; attempt++) {
            if (attempt > 0) {
                try {
                    Thread.sleep(WAIT_MS[attempt]);
                } catch (InterruptedException ie) {
                    Thread.currentThread().interrupt();
                    break;
                }
                // A partial first attempt can leave AL created but broken;
                // destroy() no-ops when nothing was created.
                tryDestroyAl(library);
                System.err.println("[BanditVault] OpenAL init retry " + attempt + " (device was not ready).");
            }
            try {
                body.invoke(library);
                if (attempt > 0) {
                    System.err.println("[BanditVault] OpenAL init SUCCEEDED on retry " + attempt + "!");
                }
                return;
            } catch (InvocationTargetException ite) {
                last = ite.getCause() != null ? ite.getCause() : ite;
                System.err.println("[BanditVault] OpenAL init attempt " + attempt + " failed: " + last);
            } catch (Throwable t) {
                last = t;
                System.err.println("[BanditVault] OpenAL init attempt " + attempt + " error: " + t);
            }
        }
        if (last instanceof RuntimeException) {
            throw (RuntimeException) last;
        }
        if (last instanceof Error) {
            throw (Error) last;
        }
        throw new RuntimeException(last);
    }

    private static void tryDestroyAl(Object library) {
        try {
            Class<?> al = Class.forName("org.lwjgl.openal.AL", false, library.getClass().getClassLoader());
            Method destroy = al.getMethod("destroy");
            destroy.invoke(null);
        } catch (Throwable ignored) {
        }
    }
}
