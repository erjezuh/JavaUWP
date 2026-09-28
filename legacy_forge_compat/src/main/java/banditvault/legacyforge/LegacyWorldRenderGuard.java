package banditvault.legacyforge;

import org.objectweb.asm.ClassReader;
import org.objectweb.asm.ClassVisitor;
import org.objectweb.asm.ClassWriter;
import org.objectweb.asm.Label;
import org.objectweb.asm.MethodVisitor;
import org.objectweb.asm.Opcodes;

import net.minecraftforge.fml.common.asm.transformers.deobf.FMLDeobfuscatingRemapper;

/**
 * Skips world-render frames during the vanilla 1.12.2 first-frame race.
 *
 * Vanilla renders the client world as soon as {@code World != null}, but the
 * render-view entity only exists once the join/player packets are processed.
 * On a fast PC that race is over before the first frame; on Xbox the slow UWP
 * storage widens it to hundreds of milliseconds and the first world render
 * throws NullPointerExceptions deep in the render path. The well-known vanilla
 * crash reports for this race all show "All players: 0 total / Chunk stats:
 * MultiplayerChunkCache: 0, 0" with the NPE in World.getSkyColorBody.
 *
 * The guard renames {@code EntityRenderer.func_78471_a} (renderWorld; SRG
 * name, stable for 1.12.2) and re-emits it as a try/catch wrapper, so a
 * NullPointerException escaping the world render simply skips that frame
 * until the view entity exists. Menus and loading screens render outside this
 * method and keep working. Any skipped frame is logged with its stack trace.
 */
public final class LegacyWorldRenderGuard implements net.minecraft.launchwrapper.IClassTransformer {
    private static final String TARGET_CLASS = "net/minecraft/client/renderer/EntityRenderer";
    private static final String TARGET_CLASS_DOTTED = "net.minecraft.client.renderer.EntityRenderer";
    private static final String RENDER_WORLD = "func_78471_a";
    private static final String RENDER_WORLD_DESC = "(FJ)V";
    private static final String RENDER_WORLD_GUARDED = "bandit$renderWorldBody";
    private static final String NULL_POINTER = "java/lang/NullPointerException";

    private static boolean matchesClass(String value) {
        return TARGET_CLASS.equals(value) || TARGET_CLASS_DOTTED.equals(value);
    }

    /** Maps a notch method name to its SRG name; returns it unchanged if it
     *  already is SRG or if the remapper is not available. */
    private static String safeMapMethodName(String owner, String name, String desc) {
        try {
            return FMLDeobfuscatingRemapper.INSTANCE.mapMethodName(owner, name, desc);
        } catch (Throwable ignored) {
            return name;
        }
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
            final boolean[] patched = new boolean[] {false};
            final int[] originalAccess = new int[] {Opcodes.ACC_PROTECTED};
            ClassReader reader = new ClassReader(basicClass);
            ClassWriter writer = new ClassWriter(reader, 0);

            reader.accept(new ClassVisitor(Opcodes.ASM5, writer) {
                @Override
                public MethodVisitor visitMethod(
                    int access, String methodName, String descriptor,
                    String signature, String[] exceptions) {

                    // Coremod transformers run before FML's deobfuscation in
                    // production, so Minecraft methods can still carry their
                    // notch names here (e.g. "b" instead of "func_78471_a").
                    // Map through FML's remapper and accept either name.
                    final String mappedName = safeMapMethodName(
                        reader.getClassName(), methodName, descriptor);
                    if ((RENDER_WORLD.equals(methodName) || RENDER_WORLD.equals(mappedName))
                        && RENDER_WORLD_DESC.equals(descriptor)) {
                        patched[0] = true;
                        originalAccess[0] = access;
                        // Rename the original body; the wrapper emitted in
                        // visitEnd() takes over the public name.
                        return super.visitMethod(
                            access, RENDER_WORLD_GUARDED, descriptor, signature, exceptions);
                    }
                    return super.visitMethod(
                        access, methodName, descriptor, signature, exceptions);
                }

                @Override
                public void visitEnd() {
                    if (patched[0]) {
                        // Keep the original visibility so external callers
                        // (mods invoking renderWorld) stay valid.
                        MethodVisitor mv = super.visitMethod(
                            originalAccess[0], RENDER_WORLD,
                            RENDER_WORLD_DESC, null, null);
                        emitWrapper(mv);
                    }
                    super.visitEnd();
                }
            }, 0);

            if (!patched[0]) {
                System.err.println(
                    "[BanditVault] World render guard: " + RENDER_WORLD + RENDER_WORLD_DESC
                    + " not found in " + name + "; world render left unchanged.");
                return basicClass;
            }
            System.err.println("[BanditVault] World render guard installed for " + name);
            return writer.toByteArray();
        } catch (Throwable error) {
            System.err.println(
                "[BanditVault] World render guard failed; world render left unchanged: " + error);
            return basicClass;
        }
    }

    /**
     * void func_78471_a(float, long) {
     *     try {
     *         this.bandit$renderWorldBody(p0, p1);
     *     } catch (NullPointerException ex) {
     *         System.err.println("[BanditVault] Skipped world render frame ...");
     *         ex.printStackTrace();
     *     }
     * }
     */
    private static void emitWrapper(MethodVisitor mv) {
        mv.visitCode();
        Label tryStart = new Label();
        Label tryEnd = new Label();
        Label handler = new Label();
        mv.visitTryCatchBlock(tryStart, tryEnd, handler, NULL_POINTER);

        mv.visitLabel(tryStart);
        mv.visitVarInsn(Opcodes.ALOAD, 0);
        mv.visitVarInsn(Opcodes.FLOAD, 1);
        mv.visitVarInsn(Opcodes.LLOAD, 2);
        mv.visitMethodInsn(
            Opcodes.INVOKESPECIAL, TARGET_CLASS, RENDER_WORLD_GUARDED,
            RENDER_WORLD_DESC, false);
        mv.visitInsn(Opcodes.RETURN);
        mv.visitLabel(tryEnd);

        mv.visitLabel(handler);
        mv.visitFrame(
            Opcodes.F_SAME1, 0, null, 1, new Object[] {NULL_POINTER});
        mv.visitVarInsn(Opcodes.ASTORE, 4);
        mv.visitFieldInsn(
            Opcodes.GETSTATIC, "java/lang/System", "err", "Ljava/io/PrintStream;");
        mv.visitLdcInsn(
            "[BanditVault] Skipped world render frame during load race"
            + " (render view entity not spawned yet).");
        mv.visitMethodInsn(
            Opcodes.INVOKEVIRTUAL, "java/io/PrintStream", "println",
            "(Ljava/lang/String;)V", false);
        mv.visitVarInsn(Opcodes.ALOAD, 4);
        mv.visitMethodInsn(
            Opcodes.INVOKEVIRTUAL, NULL_POINTER, "printStackTrace", "()V", false);
        mv.visitInsn(Opcodes.RETURN);

        // Stack: ALOAD0+FLOAD1+LLOAD2 = 4 slots. Locals: this, float, long
        // (two slots), exception reference = 5 slots.
        mv.visitMaxs(4, 5);
        mv.visitEnd();
    }
}
