package banditvault.legacyforge;

import java.util.ArrayList;
import java.util.List;

import org.objectweb.asm.ClassReader;
import org.objectweb.asm.ClassVisitor;
import org.objectweb.asm.ClassWriter;
import org.objectweb.asm.Label;
import org.objectweb.asm.MethodVisitor;
import org.objectweb.asm.Opcodes;

import net.minecraftforge.fml.common.asm.transformers.deobf.FMLDeobfuscatingRemapper;

/**
 * Skips vanilla render/HUD frames while the client is still missing its
 * player, instead of crashing.
 *
 * Vanilla 1.12.2 renders the client world and the HUD as soon as
 * {@code World != null}, but the render-view entity only exists once the
 * join/player state is fully set up. On Xbox the slow UWP storage widens that
 * race to hundreds of milliseconds (and a failed {@code loadWorld} can leave
 * it permanently), and the first frames throw NullPointerExceptions deep in
 * the render path - the well-known vanilla crash reports for this race all
 * show "All players: 0 total / Chunk stats: MultiplayerChunkCache: 0, 0" with
 * the NPE in World.getSkyColorBody, and Forge's GuiIngameForge HUD also
 * dereferences the null player.
 *
 * Guarded methods (matched by SRG name through FML's remapper, so it works on
 * notch-named production bytecode too):
 *   - EntityRenderer.func_78471_a (renderWorld)          -> world render
 *   - GuiIngameForge.func_175180_a (renderGameOverlay)   -> HUD overlay
 *
 * Each is renamed aside and re-emitted as a try/catch wrapper: a
 * NullPointerException escaping during the load race skips that frame and is
 * logged with its stack trace. Menus render outside these methods and keep
 * working.
 */
public final class LegacyWorldRenderGuard implements net.minecraft.launchwrapper.IClassTransformer {
    private static final String ENTITY_RENDERER = "net/minecraft/client/renderer/EntityRenderer";
    private static final String ENTITY_RENDERER_DOTTED = "net.minecraft.client.renderer.EntityRenderer";
    private static final String GUI_INGAME_FORGE = "net/minecraftforge/client/GuiIngameForge";
    private static final String GUI_INGAME_FORGE_DOTTED = "net.minecraftforge.client.GuiIngameForge";

    private static final String RENDER_WORLD = "func_78471_a";
    private static final String HUD_OVERLAY = "func_175180_a";
    private static final String NULL_POINTER = "java/lang/NullPointerException";

    /** One guarded method: runtime name, wrapper-visible name, rename target. */
    private static final class GuardTarget {
        final String methodName;
        final String guardedName;
        GuardTarget(String methodName, String guardedName) {
            this.methodName = methodName;
            this.guardedName = guardedName;
        }
    }

    private static boolean matchesClass(String value, String slashed, String dotted) {
        return slashed.equals(value) || dotted.equals(value);
    }

    /** Maps a notch method name to its SRG name; unchanged if already SRG. */
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

        final String owner;
        final List<GuardTarget> targets = new ArrayList<GuardTarget>();
        if (matchesClass(name, ENTITY_RENDERER, ENTITY_RENDERER_DOTTED)
            || matchesClass(transformedName, ENTITY_RENDERER, ENTITY_RENDERER_DOTTED)) {
            owner = ENTITY_RENDERER;
            targets.add(new GuardTarget(RENDER_WORLD, "bandit$renderWorldBody"));
        } else if (matchesClass(name, GUI_INGAME_FORGE, GUI_INGAME_FORGE_DOTTED)
            || matchesClass(transformedName, GUI_INGAME_FORGE, GUI_INGAME_FORGE_DOTTED)) {
            owner = GUI_INGAME_FORGE;
            targets.add(new GuardTarget(HUD_OVERLAY, "bandit$hudOverlayBody"));
        } else {
            return basicClass;
        }

        try {
            // Captures per patched method: {access, methodName, descriptor,
            // guardedName}. Wrappers are emitted in visitEnd().
            final List<int[]> patchedMeta = new ArrayList<int[]>();
            final List<String[]> patchedNames = new ArrayList<String[]>();
            ClassReader reader = new ClassReader(basicClass);
            ClassWriter writer = new ClassWriter(reader, ClassWriter.COMPUTE_MAXS);

            reader.accept(new ClassVisitor(Opcodes.ASM5, writer) {
                @Override
                public MethodVisitor visitMethod(
                    int access, String methodName, String descriptor,
                    String signature, String[] exceptions) {

                    // Coremod transformers can see notch names in production;
                    // map through FML's remapper and accept either form.
                    final String mappedName = safeMapMethodName(
                        reader.getClassName(), methodName, descriptor);
                    for (GuardTarget target : targets) {
                        if (target.methodName.equals(methodName)
                            || target.methodName.equals(mappedName)) {
                            patchedMeta.add(new int[] {access});
                            patchedNames.add(new String[] {
                                methodName, descriptor, target.guardedName});
                            // Rename the original body aside; the wrapper
                            // emitted in visitEnd() takes over the real name.
                            return super.visitMethod(
                                access, target.guardedName, descriptor,
                                signature, exceptions);
                        }
                    }
                    return super.visitMethod(
                        access, methodName, descriptor, signature, exceptions);
                }

                @Override
                public void visitEnd() {
                    for (int i = 0; i < patchedMeta.size(); i++) {
                        String[] entry = patchedNames.get(i);
                        int access = patchedMeta.get(i)[0];
                        MethodVisitor mv = super.visitMethod(
                            access, entry[0], entry[1], null, null);
                        emitWrapper(mv, owner, entry[1], entry[2]);
                        mv.visitEnd();
                    }
                    super.visitEnd();
                }
            }, 0);

            if (patchedMeta.isEmpty()) {
                System.err.println(
                    "[BanditVault] World render guard: no guard target found in "
                    + (name != null ? name : transformedName)
                    + "; class left unchanged.");
                return basicClass;
            }
            System.err.println(
                "[BanditVault] World render guard installed for "
                + (name != null ? name : transformedName)
                + " (" + patchedMeta.size() + " method(s))");
            return writer.toByteArray();
        } catch (Throwable error) {
            System.err.println(
                "[BanditVault] World render guard failed; class left unchanged: " + error);
            return basicClass;
        }
    }

    /**
     * try { this.guardedBody(args...); } catch (NullPointerException ex) {
     *     System.err.println("[BanditVault] Skipped render frame during load race ...");
     *     ex.printStackTrace();
     *     return default;
     * }
     */
    private static void emitWrapper(
        MethodVisitor mv, String owner, String descriptor, String guardedName) {

        mv.visitCode();
        Label tryStart = new Label();
        Label tryEnd = new Label();
        Label handler = new Label();
        mv.visitTryCatchBlock(tryStart, tryEnd, handler, NULL_POINTER);

        mv.visitLabel(tryStart);
        mv.visitVarInsn(Opcodes.ALOAD, 0);
        int localIndex = 1;
        int paramsEnd = descriptor.indexOf(')');
        String params = descriptor.substring(1, paramsEnd);
        int i = 0;
        while (i < params.length()) {
            char type = params.charAt(i);
            if (type == 'L') {
                i = params.indexOf(';', i);
                mv.visitVarInsn(Opcodes.ALOAD, localIndex);
                localIndex += 1;
            } else if (type == '[') {
                while (i < params.length() && params.charAt(i) == '[') {
                    i++;
                }
                if (i < params.length() && params.charAt(i) == 'L') {
                    i = params.indexOf(';', i);
                }
                mv.visitVarInsn(Opcodes.ALOAD, localIndex);
                localIndex += 1;
            } else {
                switch (type) {
                    case 'J':
                        mv.visitVarInsn(Opcodes.LLOAD, localIndex);
                        localIndex += 2;
                        break;
                    case 'F':
                        mv.visitVarInsn(Opcodes.FLOAD, localIndex);
                        localIndex += 1;
                        break;
                    case 'D':
                        mv.visitVarInsn(Opcodes.DLOAD, localIndex);
                        localIndex += 2;
                        break;
                    default: // B, C, S, Z, I
                        mv.visitVarInsn(Opcodes.ILOAD, localIndex);
                        localIndex += 1;
                        break;
                }
            }
            i++;
        }
        mv.visitMethodInsn(
            Opcodes.INVOKESPECIAL, owner, guardedName, descriptor, false);
        emitReturn(mv, descriptor.charAt(paramsEnd + 1));
        mv.visitLabel(tryEnd);

        mv.visitLabel(handler);
        mv.visitFrame(Opcodes.F_SAME1, 0, null, 1, new Object[] {NULL_POINTER});
        mv.visitVarInsn(Opcodes.ASTORE, localIndex);
        mv.visitFieldInsn(
            Opcodes.GETSTATIC, "java/lang/System", "err", "Ljava/io/PrintStream;");
        mv.visitLdcInsn(
            "[BanditVault] Skipped render frame during load race"
            + " (player/view entity not spawned yet).");
        mv.visitMethodInsn(
            Opcodes.INVOKEVIRTUAL, "java/io/PrintStream", "println",
            "(Ljava/lang/String;)V", false);
        mv.visitVarInsn(Opcodes.ALOAD, localIndex);
        mv.visitMethodInsn(
            Opcodes.INVOKEVIRTUAL, NULL_POINTER, "printStackTrace", "()V", false);
        emitDefaultReturn(mv, descriptor.charAt(paramsEnd + 1));
        mv.visitMaxs(0, 0); // ClassWriter(COMPUTE_MAXS) fills these in.
    }

    private static void emitReturn(MethodVisitor mv, char returnType) {
        switch (returnType) {
            case 'V': mv.visitInsn(Opcodes.RETURN); break;
            case 'J': mv.visitInsn(Opcodes.LRETURN); break;
            case 'F': mv.visitInsn(Opcodes.FRETURN); break;
            case 'D': mv.visitInsn(Opcodes.DRETURN); break;
            case 'L':
            case '[': mv.visitInsn(Opcodes.ARETURN); break;
            default: mv.visitInsn(Opcodes.IRETURN); break;
        }
    }

    private static void emitDefaultReturn(MethodVisitor mv, char returnType) {
        switch (returnType) {
            case 'V': mv.visitInsn(Opcodes.RETURN); break;
            case 'J': mv.visitInsn(Opcodes.LCONST_0); mv.visitInsn(Opcodes.LRETURN); break;
            case 'F': mv.visitInsn(Opcodes.FCONST_0); mv.visitInsn(Opcodes.FRETURN); break;
            case 'D': mv.visitInsn(Opcodes.DCONST_0); mv.visitInsn(Opcodes.DRETURN); break;
            case 'L':
            case '[': mv.visitInsn(Opcodes.ACONST_NULL); mv.visitInsn(Opcodes.ARETURN); break;
            default: mv.visitInsn(Opcodes.ICONST_0); mv.visitInsn(Opcodes.IRETURN); break;
        }
    }
}
