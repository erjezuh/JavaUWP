package banditvault.legacyforge;

import java.util.ArrayList;
import java.util.List;

import org.objectweb.asm.ClassReader;
import org.objectweb.asm.ClassVisitor;
import org.objectweb.asm.ClassWriter;
import org.objectweb.asm.MethodVisitor;
import org.objectweb.asm.Opcodes;

/**
 * Replaces {@code org.lwjgl.opengl.GL14.glMultiDrawArrays(int, IntBuffer,
 * IntBuffer)} with an equivalent loop of {@code glDrawArrays} calls.
 *
 * Why a full body replacement and not parameter sanitizing: two access
 * violations inside Mesa's multi-draw path (hs_err confirmed on this device)
 * survived range validation - the call itself is the crash surface. The GL
 * spec defines glMultiDrawArrays as equivalent to the sequence of
 * glDrawArrays calls with shared state, so the replacement renders
 * identically while only ever entering Mesa through the crash-free
 * single-draw path. OptiFine's Render Regions keeps its buffer merging
 * (one VBO bind per region per layer); only the draw submission loops.
 *
 * The original body is renamed aside (never called) and a tiny replacement is
 * emitted under the public name - the same rename-aside pattern used by
 * LegacyWorldRenderGuard. Exact public LWJGL signature, so no obfuscation
 * guessing; if the method is absent the class is returned unchanged.
 */
public final class LegacyMultiDrawSanitizer
    implements net.minecraft.launchwrapper.IClassTransformer {

    private static final String GL14 = "org/lwjgl/opengl/GL14";
    private static final String GL14_DOTTED = "org.lwjgl.opengl.GL14";
    private static final String MULTI_DRAW = "glMultiDrawArrays";
    private static final String MULTI_DRAW_DESC =
        "(ILjava/nio/IntBuffer;Ljava/nio/IntBuffer;)V";
    private static final String RENAMED = "bandit$multiDrawArraysBody";
    private static final String HELPER = "banditvault/legacyforge/LegacyGlSanitizer";
    private static final String HELPER_DESC =
        "(ILjava/nio/IntBuffer;Ljava/nio/IntBuffer;)V";

    private static boolean matchesClass(String value) {
        return GL14.equals(value) || GL14_DOTTED.equals(value);
    }

    @Override
    public byte[] transform(String name, String transformedName, byte[] basicClass) {
        if (basicClass == null) {
            return null;
        }
        if (!matchesClass(name) && !matchesClass(transformedName)) {
            return basicClass;
        }

        try {
            // Captures {access} of the renamed method; the replacement is
            // emitted in visitEnd().
            final List<int[]> patchedMeta = new ArrayList<int[]>();
            ClassReader reader = new ClassReader(basicClass);
            ClassWriter writer = new ClassWriter(reader, ClassWriter.COMPUTE_MAXS);

            reader.accept(new ClassVisitor(Opcodes.ASM5, writer) {
                @Override
                public MethodVisitor visitMethod(
                    int access, String methodName, String descriptor,
                    String signature, String[] exceptions) {

                    if (!MULTI_DRAW.equals(methodName)
                        || !MULTI_DRAW_DESC.equals(descriptor)) {
                        return super.visitMethod(
                            access, methodName, descriptor, signature, exceptions);
                    }
                    patchedMeta.add(new int[] {access});
                    // Rename the original body aside; the replacement emitted
                    // in visitEnd() takes over the public name.
                    return super.visitMethod(
                        access, RENAMED, descriptor, signature, exceptions);
                }

                @Override
                public void visitEnd() {
                    if (!patchedMeta.isEmpty()) {
                        final int access = patchedMeta.get(0)[0];
                        MethodVisitor mv = super.visitMethod(
                            access, MULTI_DRAW, MULTI_DRAW_DESC, null, null);
                        mv.visitCode();
                        // LegacyGlSanitizer.multiDrawAsSingles(mode, first, count);
                        mv.visitVarInsn(Opcodes.ILOAD, 0);
                        mv.visitVarInsn(Opcodes.ALOAD, 1);
                        mv.visitVarInsn(Opcodes.ALOAD, 2);
                        mv.visitMethodInsn(
                            Opcodes.INVOKESTATIC, HELPER,
                            "multiDrawAsSingles", HELPER_DESC, false);
                        mv.visitInsn(Opcodes.RETURN);
                        mv.visitMaxs(0, 0);
                        mv.visitEnd();
                    }
                    super.visitEnd();
                }
            }, 0);

            if (patchedMeta.isEmpty()) {
                System.err.println(
                    "[BanditVault] GL14 seen but glMultiDrawArrays(IntBuffer,IntBuffer) not found; leaving unchanged.");
                return basicClass;
            }
            System.err.println(
                "[BanditVault] glMultiDrawArrays replaced with crash-free glDrawArrays loop.");
            return writer.toByteArray();
        } catch (Throwable ignored) {
            System.err.println(
                "[BanditVault] GL14 multi-draw replacement transform failed: " + ignored);
            return basicClass;
        }
    }
}
