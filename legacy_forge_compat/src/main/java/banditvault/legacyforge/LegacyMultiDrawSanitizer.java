package banditvault.legacyforge;

import org.objectweb.asm.ClassReader;
import org.objectweb.asm.ClassVisitor;
import org.objectweb.asm.ClassWriter;
import org.objectweb.asm.MethodVisitor;
import org.objectweb.asm.Opcodes;

/**
 * Installs the {@link LegacyGlSanitizer} check at the entry of
 * {@code org.lwjgl.opengl.GL14.glMultiDrawArrays(int, IntBuffer, IntBuffer)}.
 *
 * Why GL14 and not OptiFine's classes: LWJGL's API signature is fixed and
 * public (no obfuscation guessing), the org.lwjgl exclusion has already been
 * removed from LaunchClassLoader for this target (see LegacyForgeCorePlugin),
 * and this single choke point covers every caller - OptiFine's VboRegion,
 * future mods, everything.
 *
 * The insertion is a balanced INVOKESTATIC at method entry: no branches are
 * added, so existing stack map frames stay valid. If the expected method is
 * absent (different LWJGL build) the class is returned unchanged and the
 * transform is a no-op by construction.
 */
public final class LegacyMultiDrawSanitizer
    implements net.minecraft.launchwrapper.IClassTransformer {

    private static final String GL14 = "org/lwjgl/opengl/GL14";
    private static final String GL14_DOTTED = "org.lwjgl.opengl.GL14";
    private static final String MULTI_DRAW = "glMultiDrawArrays";
    private static final String MULTI_DRAW_DESC =
        "(ILjava/nio/IntBuffer;Ljava/nio/IntBuffer;)V";
    private static final String SANITIZER = "banditvault/legacyforge/LegacyGlSanitizer";
    private static final String SANITIZER_DESC =
        "(Ljava/nio/IntBuffer;Ljava/nio/IntBuffer;)V";

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
            final boolean[] patched = {false};
            ClassReader reader = new ClassReader(basicClass);
            ClassWriter writer = new ClassWriter(reader, ClassWriter.COMPUTE_MAXS);
            reader.accept(new ClassVisitor(Opcodes.ASM5, writer) {
                @Override
                public MethodVisitor visitMethod(
                    int access, String methodName, String descriptor,
                    String signature, String[] exceptions) {

                    MethodVisitor mv = super.visitMethod(
                        access, methodName, descriptor, signature, exceptions);
                    if (mv == null
                        || !MULTI_DRAW.equals(methodName)
                        || !MULTI_DRAW_DESC.equals(descriptor)) {
                        return mv;
                    }
                    patched[0] = true;
                    return new MethodVisitor(Opcodes.ASM5, mv) {
                        @Override
                        public void visitCode() {
                            super.visitCode();
                            // LegacyGlSanitizer.sanitizeMultiDrawRanges(first, count);
                            super.visitVarInsn(Opcodes.ALOAD, 1);
                            super.visitVarInsn(Opcodes.ALOAD, 2);
                            super.visitMethodInsn(
                                Opcodes.INVOKESTATIC, SANITIZER,
                                "sanitizeMultiDrawRanges", SANITIZER_DESC, false);
                        }
                    };
                }
            }, 0);

            if (!patched[0]) {
                System.err.println(
                    "[BanditVault] GL14 seen but glMultiDrawArrays(IntBuffer,IntBuffer) not found; leaving unchanged.");
                return basicClass;
            }
            System.err.println(
                "[BanditVault] glMultiDrawArrays range sanitizer installed in GL14.");
            return writer.toByteArray();
        } catch (Throwable ignored) {
            System.err.println(
                "[BanditVault] GL14 multi-draw sanitizer transform failed: " + ignored);
            return basicClass;
        }
    }
}
