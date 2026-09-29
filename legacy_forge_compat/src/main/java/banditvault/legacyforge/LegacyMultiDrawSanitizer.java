package banditvault.legacyforge;

import java.util.ArrayList;
import java.util.List;

import org.objectweb.asm.ClassReader;
import org.objectweb.asm.ClassVisitor;
import org.objectweb.asm.ClassWriter;
import org.objectweb.asm.Label;
import org.objectweb.asm.MethodVisitor;
import org.objectweb.asm.Opcodes;

/**
 * Routes Minecraft 1.12.2's draw calls through the crash-free, native-
 * primitive path in {@link LegacyGlSanitizer}.
 *
 * Two entry points are rewritten (exact public LWJGL signatures; no
 * obfuscation guessing):
 *
 * 1. {@code GL11.glDrawArrays(int, int, int)} - wrapper that lets the
 *    sanitizer convert GL_QUADS draws into glDrawElementsBaseVertex triangle
 *    draws (the per-vertex CPU quad fallback is the terrain fps ceiling);
 *    anything else is forwarded to the original body, renamed aside.
 *
 * 2. {@code GL14.glMultiDrawArrays(int, IntBuffer, IntBuffer)} - OptiFine
 *    Render Regions reaches Mesa through this call and it is a confirmed
 *    native-crash surface even with sanitized ranges (hs_err), so the body is
 *    replaced by a loop of single draws; the GL spec defines the multi draw
 *    as exactly that sequence with shared state.
 *
 * Both use the proven rename-aside pattern from LegacyWorldRenderGuard:
 * the original body is renamed and a tiny replacement is emitted under the
 * public name. If a method is absent the class is returned unchanged.
 */
public final class LegacyMultiDrawSanitizer
    implements net.minecraft.launchwrapper.IClassTransformer {

    private static final String GL11 = "org/lwjgl/opengl/GL11";
    private static final String GL11_DOTTED = "org.lwjgl.opengl.GL11";
    private static final String GL14 = "org/lwjgl/opengl/GL14";
    private static final String GL14_DOTTED = "org.lwjgl.opengl.GL14";
    private static final String HELPER = "banditvault/legacyforge/LegacyGlSanitizer";

    private static final String DRAW_ARRAYS = "glDrawArrays";
    private static final String DRAW_ARRAYS_DESC = "(III)V";
    private static final String DRAW_ARRAYS_BODY = "bandit$glDrawArraysBody";
    private static final String MULTI_DRAW = "glMultiDrawArrays";
    private static final String MULTI_DRAW_DESC =
        "(ILjava/nio/IntBuffer;Ljava/nio/IntBuffer;)V";
    private static final String MULTI_DRAW_BODY = "bandit$multiDrawArraysBody";

    /** One rewritten method: owner class, method, descriptor, renamed body. */
    private static final class Target {
        final String classSlashed;
        final String classDotted;
        final String methodName;
        final String descriptor;
        final String renamed;
        Target(String classSlashed, String classDotted,
               String methodName, String descriptor, String renamed) {
            this.classSlashed = classSlashed;
            this.classDotted = classDotted;
            this.methodName = methodName;
            this.descriptor = descriptor;
            this.renamed = renamed;
        }
        boolean matchesClass(String value) {
            return classSlashed.equals(value) || classDotted.equals(value);
        }
    }

    private static final Target[] TARGETS = {
        new Target(GL11, GL11_DOTTED, DRAW_ARRAYS, DRAW_ARRAYS_DESC, DRAW_ARRAYS_BODY),
        new Target(GL14, GL14_DOTTED, MULTI_DRAW, MULTI_DRAW_DESC, MULTI_DRAW_BODY),
    };

    @Override
    public byte[] transform(String name, String transformedName, byte[] basicClass) {
        if (basicClass == null) {
            return null;
        }

        final List<Target> targets = new ArrayList<Target>();
        for (Target target : TARGETS) {
            if (target.matchesClass(name) || target.matchesClass(transformedName)) {
                targets.add(target);
            }
        }
        if (targets.isEmpty()) {
            return basicClass;
        }

        try {
            // Per patched method: {targetIndex}. Replacement is emitted in
            // visitEnd() under the public name.
            final List<int[]> patched = new ArrayList<int[]>();
            ClassReader reader = new ClassReader(basicClass);
            // Deliberately NOT ClassWriter(reader, ...): that constructor is
            // the "mostly add" copy optimization, and per its javadoc the
            // COMPUTE_* flags "do not affect methods that are copied as is" -
            // combined with rename-aside and one hand-written frame it emitted
            // a GL11 that ASM's own reader rejected (ArrayIndexOutOfBoundsException
            // in readFrameType) while FML's DeobfuscationTransformer re-parsed
            // it, killing the game with NoClassDefFoundError: GL11 at
            // Display.create. COMPUTE_FRAMES rebuilds every stack map frame
            // from the actual bytecode; the javadoc guarantees manual
            // visitFrame calls are ignored, so no hand-written frame can
            // corrupt the output again.
            ClassWriter writer = new ClassWriter(
                ClassWriter.COMPUTE_FRAMES | ClassWriter.COMPUTE_MAXS) {
                @Override
                protected String getCommonSuperClass(String type1, String type2) {
                    // Never Class.forName while transforming: computing frames
                    // for GL11 must not trigger loading GL11 (or anything) via
                    // LaunchClassLoader mid-transform. java/lang/Object is a
                    // correct - if coarse - common supertype.
                    return "java/lang/Object";
                }
            };

            reader.accept(new ClassVisitor(Opcodes.ASM5, writer) {
                @Override
                public MethodVisitor visitMethod(
                    int access, String methodName, String descriptor,
                    String signature, String[] exceptions) {

                    for (int i = 0; i < targets.size(); i++) {
                        Target target = targets.get(i);
                        if (target.methodName.equals(methodName)
                            && target.descriptor.equals(descriptor)) {
                            patched.add(new int[] {i, access});
                            return super.visitMethod(
                                access, target.renamed, descriptor,
                                signature, exceptions);
                        }
                    }
                    return super.visitMethod(
                        access, methodName, descriptor, signature, exceptions);
                }

                @Override
                public void visitEnd() {
                    for (int[] meta : patched) {
                        final Target target = targets.get(meta[0]);
                        emitReplacement(super.visitMethod(
                            meta[1], target.methodName, target.descriptor,
                            null, null), target);
                    }
                    super.visitEnd();
                }
            }, 0);

            if (patched.isEmpty()) {
                System.err.println(
                    "[BanditVault] GL11/GL14 seen but draw entry points not found; leaving unchanged.");
                return basicClass;
            }
            byte[] output = writer.toByteArray();
            System.err.println(
                "[BanditVault] Draw path installed: quads -> native triangles, multi-draw -> crash-free loop.");
            return output;
        } catch (Throwable failed) {
            System.err.println(
                "[BanditVault] GL draw-path transform failed: " + failed);
            failed.printStackTrace();
            return basicClass;
        }
    }

    private static void emitReplacement(MethodVisitor mv, Target target) {
        mv.visitCode();
        if (DRAW_ARRAYS.equals(target.methodName)) {
            // if (LegacyGlSanitizer.dispatchDrawArrays(mode, first, count)) return;
            mv.visitVarInsn(Opcodes.ILOAD, 0);
            mv.visitVarInsn(Opcodes.ILOAD, 1);
            mv.visitVarInsn(Opcodes.ILOAD, 2);
            mv.visitMethodInsn(
                Opcodes.INVOKESTATIC, HELPER, "dispatchDrawArrays",
                "(III)Z", false);
            Label skip = new Label();
            mv.visitJumpInsn(Opcodes.IFEQ, skip);
            mv.visitInsn(Opcodes.RETURN);
            mv.visitLabel(skip);
            // No manual visitFrame here on purpose: COMPUTE_FRAMES derives the
            // branch-target frame (locals int,int,int / empty stack) from the
            // bytecode itself.
            // GL11.bandit$glDrawArraysBody(mode, first, count);
            mv.visitVarInsn(Opcodes.ILOAD, 0);
            mv.visitVarInsn(Opcodes.ILOAD, 1);
            mv.visitVarInsn(Opcodes.ILOAD, 2);
            mv.visitMethodInsn(
                Opcodes.INVOKESTATIC, GL11, DRAW_ARRAYS_BODY,
                DRAW_ARRAYS_DESC, false);
            mv.visitInsn(Opcodes.RETURN);
        } else {
            // LegacyGlSanitizer.multiDrawAsSingles(mode, first, count);
            mv.visitVarInsn(Opcodes.ILOAD, 0);
            mv.visitVarInsn(Opcodes.ALOAD, 1);
            mv.visitVarInsn(Opcodes.ALOAD, 2);
            mv.visitMethodInsn(
                Opcodes.INVOKESTATIC, HELPER, "multiDrawAsSingles",
                MULTI_DRAW_DESC, false);
            mv.visitInsn(Opcodes.RETURN);
        }
        mv.visitMaxs(0, 0);
        mv.visitEnd();
    }
}
