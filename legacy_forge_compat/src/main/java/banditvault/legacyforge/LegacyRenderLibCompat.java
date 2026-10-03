package banditvault.legacyforge;

import org.objectweb.asm.ClassReader;
import org.objectweb.asm.ClassWriter;
import org.objectweb.asm.Opcodes;
import org.objectweb.asm.tree.ClassNode;
import org.objectweb.asm.tree.InsnNode;
import org.objectweb.asm.tree.LabelNode;
import org.objectweb.asm.tree.MethodNode;
import org.objectweb.asm.tree.TryCatchBlockNode;

import net.minecraft.launchwrapper.IClassTransformer;

/**
 * RenderLib 1.4.x patches RenderGlobal#renderEntities looking for instruction
 * sequences that only exist in the method as patched by OptiFine G5
 * (TileEntitySignRenderer.updateTextRenderDistance(), ReflectorMethod.exists(),
 * ...). With older OptiFine builds (E3/F5) the InsnFinder lookups throw
 * NoSuchElementException and the WHOLE RenderGlobal class fails to load, which
 * takes OptiFine's Reflector down with it ("Unable to launch"). This is exactly
 * Meldexun/RenderLib issue #61; upstream's own proposed fix is to skip the
 * renderEntities patch gracefully when the expected instructions are absent.
 *
 * This transformer applies that fix locally: the registration lambda is wrapped
 * in a try/catch so a failed lookup skips just that optional patch. RenderLib's
 * other patches (and all of Nothirium's chunk renderer) are untouched. On
 * OptiFine G5 the lambda completes normally and nothing changes at all.
 *
 * Kill switch: MC_RENDERLIB_GUARD=0.
 */
public final class LegacyRenderLibCompat implements IClassTransformer {

    private static final boolean enabled =
        !"0".equals(System.getenv("MC_RENDERLIB_GUARD"))
        && !"0".equals(System.getProperty("MC_RENDERLIB_GUARD", "1"));

    private static boolean logged;

    @Override
    public byte[] transform(String name, String transformedName, byte[] basicClass) {
        if (basicClass == null || !enabled) {
            return basicClass;
        }
        final String target = "meldexun.renderlib.asm.RenderLibClassTransformer";
        if (!target.equals(transformedName) && !target.equals(name)) {
            return basicClass;
        }
        try {
            final ClassReader reader = new ClassReader(basicClass);
            final ClassNode classNode = new ClassNode();
            reader.accept(classNode, 0);

            boolean wrapped = false;
            for (MethodNode method : classNode.methods) {
                if (!method.name.startsWith("lambda$registerTransformers")) {
                    continue;
                }
                if (method.instructions == null || method.instructions.size() == 0) {
                    continue;
                }
                // Wrap the whole body: try { ...original... } catch (Exception e) { return; }
                // The lambda returns void, so a plain RETURN is the clean "skip".
                final LabelNode begin = new LabelNode();
                final LabelNode end = new LabelNode();
                final LabelNode handler = new LabelNode();
                method.instructions.insert(begin);
                method.instructions.add(end);
                method.instructions.add(handler);
                method.instructions.add(new InsnNode(Opcodes.RETURN));
                method.tryCatchBlocks.add(
                    new TryCatchBlockNode(begin, end, handler, "java/lang/Exception"));
                wrapped = true;
            }

            if (!wrapped) {
                return basicClass;
            }
            if (!logged) {
                logged = true;
                System.err.println("[BanditVault] RenderLib renderEntities patch guarded: "
                    + "missing OptiFine instructions now skip just that patch instead of "
                    + "killing RenderGlobal (RenderLib issue #61).");
            }

            final ClassWriter writer = new ClassWriter(reader, ClassWriter.COMPUTE_FRAMES);
            classNode.accept(writer);
            return writer.toByteArray();
        } catch (Throwable t) {
            System.err.println("[BanditVault] RenderLib guard failed, class left unchanged: " + t);
            return basicClass;
        }
    }
}
